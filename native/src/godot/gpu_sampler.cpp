#include "gpu_sampler.h"

#include "util/parallel.h"

#include <godot_cpp/classes/rd_shader_source.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader_include.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

using namespace godot;

namespace sdf::godot_bind {

namespace {

constexpr int kCorners = AdfSampler::kCorners, kCentres = AdfSampler::kCentres;
constexpr std::uint32_t kMaxGroups = 65535; // workgroups per dispatch dimension

double ms_since(std::chrono::steady_clock::time_point start) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// The interpreter: sdf_eval_tape (sdf_live.gdshaderinc) over storage buffers, and a
// workgroup per job. Edits are packed as SdfBody packs its edits texture: a head texel
// (code, r, r2, material) and the primitive's five.
const char *kInterpreter = R"GLSL(
struct Job {
	vec4 lo_voxel; // the brick's lower corner, and its sample spacing
	uvec4 tape;    // offset and count in tape[], whether the base primitive is in play
};
layout(set = 0, binding = 0, std430) restrict readonly buffer Edits { vec4 edits[]; };
layout(set = 0, binding = 1, std430) restrict readonly buffer Tape { uint tape[]; };
layout(set = 0, binding = 2, std430) restrict readonly buffer Jobs { Job jobs[]; };
layout(set = 0, binding = 3, std430) restrict writeonly buffer Corners { vec4 corners[]; };
layout(set = 0, binding = 4, std430) restrict writeonly buffer Centres { float centres[]; };
layout(push_constant, std430) uniform Params {
	vec4 base_p0;
	vec4 base_p1;
	vec4 base_p2;
	vec4 base_p3;
	vec4 base_p4;
	int base_type;
	float base_material;
	uint job_base;
	uint unused;
} params;

const uint SDF_RESET = 0x80000000u; // Octree::kResetBit

vec4 sdf_eval(vec3 p, uvec4 t) {
	float d = SDF_BIG;
	if (t.z != 0u) {
		d = sdf_primitive(p, params.base_type, params.base_p0, params.base_p1, params.base_p2, params.base_p3, params.base_p4);
	}
	vec3 mat = vec3(params.base_material, params.base_material, 0.0);
	for (uint k = 0u; k < t.y; k++) {
		uint entry = tape[t.x + k];
		int e = int(entry & ~SDF_RESET) * 6;
		vec4 head = edits[e];
		int packed = int(head.x);
		int op = (packed >> 3) & 7;
		if ((entry & SDF_RESET) != 0u) {
			d = op == SDF_OP_UNION ? SDF_BIG : -SDF_BIG;
		}
		float pd = sdf_primitive(p, packed & 7, edits[e + 1], edits[e + 2], edits[e + 3], edits[e + 4], edits[e + 5]);
		vec4 r = sdf_apply_edit(d, mat, pd, op, (packed >> 6) & 7, head.y, head.z, (packed >> 9) & 3, head.w);
		d = r.x;
		mat = r.yzw;
	}
	return vec4(d, mat);
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

void main() {
	uint job = params.job_base + gl_WorkGroupID.x;
	Job j = jobs[job];
	for (uint i = gl_LocalInvocationID.x; i < 855u; i += 64u) {
		if (i < 512u) {
			vec3 c = vec3(float(i & 7u), float((i >> 3u) & 7u), float(i >> 6u));
			corners[job * 512u + i] = sdf_eval(j.lo_voxel.xyz + c * j.lo_voxel.w, j.tape);
		} else {
			uint k = i - 512u;
			vec3 c = vec3(float(k % 7u), float((k / 7u) % 7u), float(k / 49u));
			centres[job * 343u + k] = sdf_eval(j.lo_voxel.xyz + (c + 0.5) * j.lo_voxel.w, j.tape).x;
		}
	}
}
)GLSL";

PackedByteArray bytes(const void *data, std::size_t size) {
	PackedByteArray out;
	out.resize(int64_t(size));
	if (size) {
		std::memcpy(out.ptrw(), data, size);
	}
	return out;
}

bool holds_layer(const Body &body, const Octree::Leaf &cell) {
	for (std::uint32_t entry : cell.tape) {
		if (body.edits()[entry & ~Octree::kResetBit].op == Op::Layer) {
			return true;
		}
	}
	return false;
}

} // namespace

struct GpuSampler::Batch {
	std::vector<float> edits;         // 24 per edit
	std::vector<std::uint32_t> tape;  // every job's tape, one after another
	std::vector<std::uint32_t> jobs;  // 8 words per job (see Job in kInterpreter)
	std::uint32_t params[24] = {};    // the push constants
	std::size_t count = 0;
};

std::shared_ptr<GpuSampler> GpuSampler::shared() {
	static std::mutex mutex;
	static std::weak_ptr<GpuSampler> instance;
	static bool failed = false;
	std::lock_guard<std::mutex> lock(mutex);
	if (std::shared_ptr<GpuSampler> s = instance.lock()) {
		return s;
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	if (failed || !rs || !rs->get_rendering_device()) {
		return nullptr; // Compatibility (OpenGL) or headless: no RenderingDevice
	}
	// The includes the Live shader is built from, as the project holds them (loaded as
	// ShaderInclude resources, which works in exported builds too).
	String source = "#version 450\n#define SDF_FN\n#define OUT(T) out T\n#define INOUT(T) inout T\n";
	for (const char *name : {"sdf_common", "sdf_primitives", "sdf_blend"}) {
		const Ref<ShaderInclude> include = ResourceLoader::get_singleton()->load(String("res://shaders/sdf/") + name + ".gdshaderinc");
		if (include.is_null()) {
			UtilityFunctions::push_error("GpuSampler: cannot load ", name, ".gdshaderinc; sampling bricks on the CPU");
			failed = true;
			return nullptr;
		}
		source += include->get_code() + "\n";
	}
	source += kInterpreter;
	std::shared_ptr<GpuSampler> s(new GpuSampler());
	if (!s->start(source)) {
		failed = true;
		return nullptr;
	}
	instance = s;
	return s;
}

bool GpuSampler::start(const String &source) {
	thread_ = std::thread([this, source]() { run(source); });
	std::unique_lock<std::mutex> lock(mutex_);
	done_.wait(lock, [this]() { return started_; });
	return ok_;
}

GpuSampler::~GpuSampler() {
	{
		std::lock_guard<std::mutex> lock(mutex_);
		quit_ = true;
	}
	wake_.notify_all();
	if (thread_.joinable()) {
		thread_.join();
	}
}

void GpuSampler::run(const String &source) {
	rd_ = RenderingServer::get_singleton()->create_local_rendering_device();
	const bool ok = rd_ && build(source);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ok_ = ok;
		started_ = true;
	}
	done_.notify_all();
	while (ok) {
		std::unique_lock<std::mutex> lock(mutex_);
		wake_.wait(lock, [this]() { return quit_ || pending_; });
		if (quit_) {
			break;
		}
		const std::function<void()> task = task_;
		lock.unlock();
		task();
		lock.lock();
		task_ = nullptr;
		pending_ = false;
		done_.notify_all();
	}
	release();
}

void GpuSampler::call(const std::function<void()> &task) {
	std::unique_lock<std::mutex> lock(mutex_);
	task_ = task;
	pending_ = true;
	wake_.notify_all();
	done_.wait(lock, [this]() { return !pending_; });
}

bool GpuSampler::build(const String &source) {
	Ref<RDShaderSource> src;
	src.instantiate();
	src->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	src->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, source);
	const Ref<RDShaderSPIRV> spirv = rd_->shader_compile_spirv_from_source(src, false);
	const String error = spirv.is_valid() ? spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE) : "no SPIR-V";
	if (!error.is_empty()) {
		UtilityFunctions::push_error("GpuSampler: the brick shader does not compile; sampling bricks on the CPU\n", error);
		return false;
	}
	shader_ = rd_->shader_create_from_spirv(spirv, "sdf_bricks");
	if (!shader_.is_valid()) {
		return false;
	}
	pipeline_ = rd_->compute_pipeline_create(shader_);
	return pipeline_.is_valid();
}

void GpuSampler::release() {
	if (!rd_) {
		return;
	}
	if (set_.is_valid() && rd_->uniform_set_is_valid(set_)) {
		rd_->free_rid(set_);
	}
	for (RID &b : buffers_) {
		if (b.is_valid()) {
			rd_->free_rid(b);
		}
	}
	if (pipeline_.is_valid()) {
		rd_->free_rid(pipeline_);
	}
	if (shader_.is_valid()) {
		rd_->free_rid(shader_);
	}
	memdelete(rd_);
	rd_ = nullptr;
}

void GpuSampler::ensure(int binding, std::size_t size) {
	size = std::max<std::size_t>(size, 16);
	if (capacity_[binding] >= size) {
		return;
	}
	if (set_.is_valid() && rd_->uniform_set_is_valid(set_)) {
		rd_->free_rid(set_);
	}
	set_ = RID();
	if (buffers_[binding].is_valid()) {
		rd_->free_rid(buffers_[binding]);
	}
	capacity_[binding] = std::max(size, 2 * capacity_[binding]);
	buffers_[binding] = rd_->storage_buffer_create(std::uint32_t(capacity_[binding]));
}

void GpuSampler::dispatch(const Batch &batch, std::vector<float> &corners, std::vector<float> &centres) {
	const std::size_t sizes[kBindings] = {batch.edits.size() * 4, batch.tape.size() * 4, batch.jobs.size() * 4,
			batch.count * kCorners * 16, batch.count * kCentres * 4};
	for (int b = 0; b < kBindings; ++b) {
		ensure(b, sizes[b]);
	}
	if (!set_.is_valid()) {
		TypedArray<RDUniform> uniforms;
		for (int b = 0; b < kBindings; ++b) {
			Ref<RDUniform> u;
			u.instantiate();
			u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_STORAGE_BUFFER);
			u->set_binding(b);
			u->add_id(buffers_[b]);
			uniforms.push_back(u);
		}
		set_ = rd_->uniform_set_create(uniforms, shader_, 0);
	}
	const void *inputs[3] = {batch.edits.data(), batch.tape.data(), batch.jobs.data()};
	for (int b = 0; b < 3; ++b) {
		if (sizes[b]) {
			rd_->buffer_update(buffers_[b], 0, std::uint32_t(sizes[b]), bytes(inputs[b], sizes[b]));
		}
	}
	const std::int64_t list = rd_->compute_list_begin();
	rd_->compute_list_bind_compute_pipeline(list, pipeline_);
	rd_->compute_list_bind_uniform_set(list, set_, 0);
	for (std::size_t first = 0; first < batch.count; first += kMaxGroups) {
		std::uint32_t params[24];
		std::memcpy(params, batch.params, sizeof(params));
		params[22] = std::uint32_t(first);
		rd_->compute_list_set_push_constant(list, bytes(params, sizeof(params)), sizeof(params));
		rd_->compute_list_dispatch(list, std::uint32_t(std::min<std::size_t>(kMaxGroups, batch.count - first)), 1, 1);
	}
	rd_->compute_list_end();
	rd_->submit();
	rd_->sync();
	const PackedByteArray c = rd_->buffer_get_data(buffers_[3], 0, std::uint32_t(sizes[3]));
	const PackedByteArray m = rd_->buffer_get_data(buffers_[4], 0, std::uint32_t(sizes[4]));
	corners.resize(sizes[3] / 4);
	centres.resize(sizes[4] / 4);
	std::memcpy(corners.data(), c.ptr(), std::min<std::size_t>(sizes[3], std::size_t(c.size())));
	std::memcpy(centres.data(), m.ptr(), std::min<std::size_t>(sizes[4], std::size_t(m.size())));
}

void GpuSampler::sample(const Body &body, const std::vector<AdfJob> &jobs, Sample *corners, float *centres) {
	std::lock_guard<std::mutex> one(call_mutex_);
	const auto start = std::chrono::steady_clock::now();
	std::vector<std::size_t> gpu, cpu;
	for (std::size_t j = 0; j < jobs.size(); ++j) {
		(holds_layer(body, *jobs[j].cell) ? cpu : gpu).push_back(j);
	}

	Batch batch;
	batch.count = gpu.size();
	if (!gpu.empty()) {
		batch.edits.reserve(body.edits().size() * 24);
		for (const Edit &e : body.edits()) {
			const int code = int(e.prim.type) | int(e.op) << 3 | int(e.blend) << 6 | int(e.shape) << 9;
			for (float f : {float(code), e.r, e.r2, float(e.material)}) {
				batch.edits.push_back(f);
			}
			for (const vec4 &p : e.prim.p) {
				for (float f : {p.x, p.y, p.z, p.w}) {
					batch.edits.push_back(f);
				}
			}
		}
		batch.jobs.reserve(gpu.size() * 8);
		for (std::size_t j : gpu) {
			const AdfJob &job = jobs[j];
			const float lo_voxel[4] = {job.lo.x, job.lo.y, job.lo.z, job.voxel};
			std::uint32_t words[8];
			std::memcpy(words, lo_voxel, sizeof(lo_voxel));
			words[4] = std::uint32_t(batch.tape.size());
			words[5] = std::uint32_t(job.cell->tape.size());
			words[6] = job.cell->base ? 1u : 0u;
			words[7] = 0;
			batch.jobs.insert(batch.jobs.end(), words, words + 8);
			batch.tape.insert(batch.tape.end(), job.cell->tape.begin(), job.cell->tape.end());
		}
		float base[20];
		for (int i = 0; i < 5; ++i) {
			const vec4 &p = body.base.p[i];
			base[4 * i] = p.x;
			base[4 * i + 1] = p.y;
			base[4 * i + 2] = p.z;
			base[4 * i + 3] = p.w;
		}
		std::memcpy(batch.params, base, sizeof(base));
		batch.params[20] = std::uint32_t(int(body.base.type));
		const float material = float(body.base_material);
		std::memcpy(&batch.params[21], &material, 4);
	}

	// The GPU's share on the device's thread; the CPU's here meanwhile.
	std::vector<float> gpu_corners, gpu_centres;
	double gpu_ms = 0;
	std::thread device;
	if (!gpu.empty()) {
		device = std::thread([&]() {
			const auto t0 = std::chrono::steady_clock::now();
			call([&]() { dispatch(batch, gpu_corners, gpu_centres); });
			gpu_ms = ms_since(t0);
		});
	}
	parallel_for(cpu.size(), [&](std::size_t k) {
		const std::size_t j = cpu[k];
		CpuSampler::sample_job(body, jobs[j], corners + j * kCorners, centres ? centres + j * kCentres : nullptr);
	});
	if (device.joinable()) {
		device.join();
	}
	for (std::size_t k = 0; k < gpu.size(); ++k) {
		const std::size_t j = gpu[k];
		const float *c = gpu_corners.data() + k * kCorners * 4;
		for (int i = 0; i < kCorners; ++i) {
			corners[j * kCorners + std::size_t(i)] = {c[4 * i], c[4 * i + 1], c[4 * i + 2], c[4 * i + 3]};
		}
		if (centres) {
			std::memcpy(centres + j * kCentres, gpu_centres.data() + k * kCentres, kCentres * sizeof(float));
		}
	}

	std::lock_guard<std::mutex> lock(stats_mutex_);
	stats_.ms += ms_since(start);
	stats_.gpu_ms += gpu_ms;
	stats_.gpu_jobs += gpu.size();
	stats_.cpu_jobs += cpu.size();
	stats_.dispatches += gpu.empty() ? 0 : (gpu.size() + kMaxGroups - 1) / kMaxGroups;
}

GpuSampler::Stats GpuSampler::take_stats() {
	std::lock_guard<std::mutex> lock(stats_mutex_);
	const Stats s = stats_;
	stats_ = {};
	return s;
}

} // namespace sdf::godot_bind
