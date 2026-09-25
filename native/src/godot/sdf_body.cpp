#include "godot/sdf_body.h"

#include "tools/catalog.h"

#include "demo/gallery.h"
#include "eval/query.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace sdf::godot_bind {

using namespace godot;

namespace {

constexpr int kTextureWidth = 1024;
constexpr int kNodeTexels = 2;
constexpr int kEditTexels = 6;
// An edit's kinds share one float in the shader: prim | op << 3 | blend << 6 | profile << 9.
static_assert(int(Prim::SweepBezier) < 8 && int(Op::Layer) < 8 && int(Blend::Profile) < 8 &&
				int(EdgeProfile::Ogee) < 4,
		"edit kinds outgrew their bits in sdf_live.gdshaderinc");
constexpr int kMaterialTexels = 4;
// The shader's overlay (sdf_live.gdshaderinc): SDF_OVERLAY_MAX edits of SDF_OVERLAY_TEXELS.
constexpr int kOverlayMax = 16;
constexpr int kOverlayTexels = 8;
constexpr int kOverlayPlanned = 2048; // SDF_OVERLAY_PLANNED: bit 11 of an overlay edit's code
// How far past its bounds an overlay edit is still applied: it can change the field's
// value (never its sign) inside the body there, which settling and normal taps within a
// millimetre of the surface read.
constexpr float kOverlayMargin = 1.0f;

Vector3 to_godot(vec3 v) {
	return Vector3(v.x, v.y, v.z);
}

Vector4 to_godot(vec4 v) {
	return Vector4(v.x, v.y, v.z, v.w);
}

// Packs RGBA float texels into an ImageTexture of fixed width, reusing the texture when
// its size is unchanged (ImageTexture::update is cheaper than a new texture).
void upload(Ref<ImageTexture> &tex, const std::vector<float> &texels) {
	const int count = std::max<int>(1, int(texels.size() / 4));
	const int height = (count + kTextureWidth - 1) / kTextureWidth;
	PackedByteArray bytes;
	bytes.resize(int64_t(kTextureWidth) * height * 4 * sizeof(float));
	std::memset(bytes.ptrw(), 0, size_t(bytes.size()));
	if (!texels.empty()) {
		std::memcpy(bytes.ptrw(), texels.data(), texels.size() * sizeof(float));
	}
	Ref<godot::Image> image =
			godot::Image::create_from_data(kTextureWidth, height, false, godot::Image::FORMAT_RGBAF, bytes);
	if (tex.is_valid() && tex->get_height() == height) {
		tex->update(image);
	} else {
		tex = ImageTexture::create_from_image(image);
	}
}

void push(std::vector<float> &out, vec4 v) {
	out.insert(out.end(), {v.x, v.y, v.z, v.w});
}

// The raymarch starts on the proxy's back faces, so any closed box around the body works;
// building it in body coordinates keeps node space and body space the same.
Ref<ArrayMesh> proxy_box(const Aabb &b) {
	PackedVector3Array vertices;
	for (int i = 0; i < 8; ++i) {
		vertices.push_back(Vector3(i & 1 ? b.hi.x : b.lo.x, i & 2 ? b.hi.y : b.lo.y, i & 4 ? b.hi.z : b.lo.z));
	}
	// Each face's corners run counter-clockwise seen from outside; Godot's front faces are
	// clockwise, so the triangles take them in reverse.
	const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
	PackedInt32Array indices;
	for (const auto &f : faces) {
		for (int k : {f[0], f[2], f[1], f[0], f[3], f[2]}) {
			indices.push_back(k);
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

double ms_since(std::chrono::steady_clock::time_point t) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

vec3 to_vec(const Vector3 &v) {
	return vec3(float(v.x), float(v.y), float(v.z));
}

} // namespace

SdfBody::SdfBody() {
	material_.instantiate();
	set_material_override(material_);
	set_cast_shadows_setting(SHADOW_CASTING_SETTING_OFF);

	caster_material_.instantiate();
	update_shaders();
	shadow_caster_ = memnew(MeshInstance3D);
	shadow_caster_->set_material_override(caster_material_);
	shadow_caster_->set_cast_shadows_setting(SHADOW_CASTING_SETTING_SHADOWS_ONLY);
	shadow_caster_->set_visible(false);
	add_child(shadow_caster_, false, INTERNAL_MODE_FRONT);
	set_process(true);
}

SdfBody::~SdfBody() {
	if (job_.valid()) {
		job_.wait();
	}
}

void SdfBody::set_live_shadows(bool enabled) {
	live_shadows_ = enabled;
	shadow_caster_->set_visible(enabled);
}

void SdfBody::set_live_source(int source) {
	flush();
	live_source_ = source;
	update_shaders();
	if (!session_.octree().nodes().empty()) {
		session_.set_adf(live_source_ == LIVE_ADF);
		upload_all();
		update_material(); // a new shader starts from default parameters
		refresh_stats();
	}
}

void SdfBody::set_gpu_bricks(bool enabled) {
	flush();
	gpu_bricks_ = enabled;
	gpu_ = gpu_bricks_ && live_source_ == LIVE_ADF ? GpuSampler::shared() : nullptr;
	session_.set_adf_sampler(gpu_);
	refresh_stats();
}

void SdfBody::collect_gpu_stats() {
	if (!gpu_) {
		return;
	}
	const GpuSampler::Stats g = gpu_->take_stats();
	job_gpu_ms_ = g.gpu_ms;
	job_gpu_jobs_ = g.gpu_jobs;
	gpu_jobs_total_ += g.gpu_jobs;
}

Dictionary SdfBody::compare_bricks_with_cpu(int points) {
	flush();
	collect_gpu_stats();
	Dictionary out;
	out["sampler"] = gpu_ ? "gpu" : "cpu";
	out["gpu_bricks_sampled"] = int64_t(gpu_jobs_total_); // by the GPU for this body so far
	if (!session_.has_adf()) {
		return out;
	}
	const Adf &adf = session_.adf();
	Adf cpu;
	cpu.build(session_.body(), session_.octree(), adf.params());
	std::vector<int> leaves;
	for (std::size_t i = 0; i < adf.nodes().size(); ++i) {
		if (adf.nodes()[i].brick >= 0) {
			leaves.push_back(int(i));
		}
	}
	std::uint32_t state = 12345u;
	auto next = [&]() {
		state = state * 1664525u + 1013904223u;
		return float(state >> 8) / float(1u << 24);
	};
	// Points near the surface (what rays see): random points in brick leaves, stepped onto
	// the surface along the field's gradient, then moved off it by up to 0.2 mm.
	const Body &body = session_.body();
	const Octree &octree = session_.octree();
	int compared = 0;
	float worst = 0.0f, worst_exact = 0.0f;
	for (int k = 0; k < points && !leaves.empty(); ++k) {
		const Adf::Node &n = adf.nodes()[std::size_t(leaves[std::size_t(next() * float(leaves.size())) % leaves.size()])];
		vec3 p = n.lo + vec3(next(), next(), next()) * n.size;
		vec3 g(0.0f);
		for (int step = 0; step < 6; ++step) {
			const float d = octree.distance(body, p), h = 1e-3f;
			g = vec3(octree.distance(body, p + vec3(h, 0, 0)) - octree.distance(body, p - vec3(h, 0, 0)),
					octree.distance(body, p + vec3(0, h, 0)) - octree.distance(body, p - vec3(0, h, 0)),
					octree.distance(body, p + vec3(0, 0, h)) - octree.distance(body, p - vec3(0, 0, h)));
			const float length = gl::length(g);
			if (!(length > 1e-9f)) {
				break;
			}
			g = g / length;
			p = p - g * d;
		}
		p = p + g * ((next() * 2.0f - 1.0f) * 0.2f);
		const int a = adf.leaf(p), b = cpu.leaf(p);
		if (a < 0 || b < 0 || adf.nodes()[std::size_t(a)].brick < 0 || cpu.nodes()[std::size_t(b)].brick < 0) {
			continue;
		}
		const float d = adf.distance(body, octree, p);
		worst = std::max(worst, std::fabs(d - cpu.distance(body, octree, p)));
		worst_exact = std::max(worst_exact, std::fabs(d - octree.distance(body, p)));
		++compared;
	}
	out["compared"] = compared;
	out["max_difference"] = worst;             // from the CPU-built ADF
	out["max_difference_exact"] = worst_exact; // from the exact field
	out["bricks"] = int64_t(adf.stats().bricks);
	out["cpu_bricks"] = int64_t(cpu.stats().bricks);
	return out;
}

void SdfBody::set_exact_cells(bool enabled) {
	exact_cells_ = enabled;
	for (const Ref<ShaderMaterial> &m : {material_, caster_material_}) {
		m->set_shader_parameter("sdf_exact_cells", exact_cells_);
	}
}

void SdfBody::update_shaders() {
	const bool adf = live_source_ == LIVE_ADF;
	ResourceLoader *loader = ResourceLoader::get_singleton();
	const String dir = "res://shaders/sdf/";
	material_->set_shader(loader->load(dir + String(adf ? "sdf_adf_single.gdshader" : "sdf_live_single.gdshader")));
	caster_material_->set_shader(loader->load(dir + String(adf ? "sdf_adf.gdshader" : "sdf_live.gdshader")));
}

bool SdfBody::load_demo(const String &name) {
	Body body;
	Camera camera;
	if (!demo::named_demo(name.utf8().get_data(), body, camera)) {
		UtilityFunctions::push_error("SdfBody: unknown demo ", name);
		return false;
	}
	flush();
	stroke_.reset();
	update_overlay();
	demo_camera_ = camera;
	gpu_ = gpu_bricks_ ? GpuSampler::shared() : nullptr;
	session_.set_adf_sampler(gpu_);
	const auto start = std::chrono::steady_clock::now();
	session_.reset(body, {}, live_source_ == LIVE_ADF);
	last_update_ms_ = ms_since(start);
	gpu_jobs_total_ = 0;
	collect_gpu_stats();
	rebuild();
	return true;
}

namespace {

bool edge_tool(const String &tool) {
	return tool == "chisel" || tool == "gouge";
}

// A chisel's or gouge's variant (core tools/catalog.h), held at its settings' angle.
tools::Chisel chisel_from(const String &tool, const Dictionary &settings) {
	const String fallback = tool == "gouge" ? "gouge_7_12" : "bench_12";
	const tools::ChiselVariant *v = tools::find_chisel(String(settings.get("variant", fallback)).utf8().get_data());
	if (v == nullptr) {
		v = tools::find_chisel(fallback.utf8().get_data());
	}
	tools::Chisel c = v->chisel;
	c.approach_deg = float(double(settings.get("angle", 30.0)));
	return c;
}

} // namespace

bool SdfBody::load_tool(const String &name, const Dictionary &settings) {
	Body model;
	if (edge_tool(name)) {
		model = chisel_from(name, settings).model();
	} else if (name == "saw") {
		model = tools::Saw{}.model();
	} else if (name == "sanding_block") {
		tools::SandingBlock block;
		block.grit = int(settings.get("grit", 120));
		model = block.model();
	} else if (name == "sanding_sponge") {
		model = tools::SandingSponge{}.model();
	} else {
		UtilityFunctions::push_error("SdfBody: unknown tool ", name);
		return false;
	}
	flush();
	stroke_.reset();
	update_overlay();
	// A tool has a handful of edits: its exact tapes are short, and it needs no ADF.
	live_source_ = LIVE_EXACT;
	gpu_ = nullptr;
	session_.set_adf_sampler(nullptr);
	update_shaders();
	const auto start = std::chrono::steady_clock::now();
	session_.reset(model, {}, false);
	last_update_ms_ = ms_since(start);
	set_live_shadows(true);
	rebuild();
	return true;
}

Array SdfBody::tool_catalog() {
	Array out;
	for (const tools::ChiselVariant &v : tools::chisel_catalog()) {
		Dictionary d;
		d["id"] = String(v.id.c_str());
		d["family"] = String(v.family.c_str());
		d["label"] = String::utf8(v.label.c_str());
		d["width"] = double(v.chisel.width);
		d["bevel"] = double(v.chisel.bevel_deg);
		d["mallet"] = double(v.chisel.mallet);
		out.push_back(d);
	}
	return out;
}

Dictionary SdfBody::get_demo_camera() const {
	Dictionary d;
	d["eye"] = to_godot(demo_camera_.eye);
	d["target"] = to_godot(demo_camera_.target);
	d["up"] = to_godot(demo_camera_.up);
	d["fov"] = demo_camera_.fov_deg;
	return d;
}

void SdfBody::add_random_strokes(int count, int seed) {
	flush();
	stroke_.reset();
	update_overlay();
	const auto start = std::chrono::steady_clock::now();
	std::vector<Edit> strokes;
	for (const Edit &e : demo::random_strokes(session_.body(), count, std::uint32_t(seed))) {
		if (Body::accepts(e)) {
			strokes.push_back(e);
		}
	}
	session_.set_stroke(strokes);
	session_.commit();
	collect_gpu_stats();
	upload_textures();
	upload_adf(false, session_.adf().dirty_bricks(), session_.adf().dirty_materials());
	update_material();
	last_update_ms_ = ms_since(start);
	refresh_stats();
}

void SdfBody::rebuild() {
	// A new body: no pieces of the last one, and no plan on it.
	clear_plan();
	clips_.clear();
	unclipped_bounds_.clear();
	sides_.reset();
	hull_.clear();
	mass_ = -1.0;
	bounds_ = session_.body().bounds();
	set_proxy();
	upload_all();
	update_material();
	refresh_stats();
}

// --- live editing ------------------------------------------------------------------------

vec3 SdfBody::to_body(const Vector3 &world) const {
	return to_vec(get_global_transform().affine_inverse().xform(world));
}

vec3 SdfBody::to_body_direction(const Vector3 &world) const {
	return to_vec(get_global_transform().affine_inverse().basis.xform(world));
}

Transform3D SdfBody::frame_to_world(const tools::Frame &f) const {
	const Transform3D local(Basis(to_godot(f.x), to_godot(f.y), to_godot(f.z)), to_godot(f.origin));
	return get_global_transform() * local;
}

Dictionary SdfBody::raycast(const Vector3 &from, const Vector3 &direction, double max_distance) {
	if (job_.valid() && !job_refines_) {
		// The body is being edited on the worker: answer from the last query.
		Dictionary stale = last_hit_.duplicate();
		if (!stale.is_empty()) {
			stale["stale"] = true;
		}
		return stale;
	}
	last_hit_ = Dictionary();
	const vec3 origin = to_body(from), dir = to_body_direction(direction);
	const float body_per_world = gl::length(dir) / float(direction.length());
	if (!(body_per_world > 0.0f) || session_.octree().nodes().empty()) {
		return last_hit_;
	}
	const auto hit = sdf::raycast(session_.body(), session_.octree(), origin, gl::normalize(dir),
			float(max_distance) * body_per_world, 1e-3f);
	if (!hit) {
		return last_hit_;
	}
	const Transform3D xf = get_global_transform();
	last_hit_["position"] = xf.xform(to_godot(hit->point));
	last_hit_["normal"] = xf.basis.xform(to_godot(hit->normal)).normalized();
	last_hit_["distance"] = double(hit->t / body_per_world);
	last_hit_["material"] = int(hit->sample.t < 0.5f ? hit->sample.m0 : hit->sample.m1);
	return last_hit_;
}

std::unique_ptr<tools::Stroke> SdfBody::make_stroke(const String &tool, vec3 p, vec3 n, vec3 a,
		const Dictionary &settings) const {
	if (edge_tool(tool)) {
		// Worked out against the body: only while no edit is being applied to it.
		const tools::Work work{session_.body(), session_.octree(), materials_};
		return tools::planned_stroke(tools::plan_cut(chisel_from(tool, settings), work, p, n, a,
				float(double(settings.get("length", 40.0))), float(double(settings.get("depth", 1.0))),
				float(double(settings.get("skew", 0.0))), std::uint32_t(int64_t(settings.get("seed", 1)))));
	}
	if (tool == "saw") {
		// Deep enough to go right through the body, no deeper.
		float through = 0.0f;
		for (int c = 0; c < 8; ++c) {
			const vec3 corner(c & 1 ? bounds_.hi.x : bounds_.lo.x, c & 2 ? bounds_.hi.y : bounds_.lo.y,
					c & 4 ? bounds_.hi.z : bounds_.lo.z);
			through = std::max(through, gl::dot(p - corner, n));
		}
		return tools::saw_stroke(tools::Saw{}, p, n, a, float(double(settings.get("feed", 0.02))), through + 1.0f);
	}
	if (tool == "sanding_block") {
		tools::SandingBlock block;
		block.grit = int(settings.get("grit", 120));
		block.pressure = float(double(settings.get("pressure", 1.0)));
		return tools::sanding_stroke(block, p, n, a);
	}
	if (tool == "sanding_sponge") {
		tools::SandingSponge sponge;
		sponge.grit = int(settings.get("grit", 120));
		sponge.pressure = float(double(settings.get("pressure", 1.0)));
		return tools::hand_sanding_stroke(sponge, p, n, a);
	}
	UtilityFunctions::push_error("SdfBody: unknown tool ", tool);
	return nullptr;
}

bool SdfBody::begin_stroke(const String &tool, const Vector3 &contact, const Vector3 &normal, const Vector3 &along,
		const Dictionary &settings) {
	if (stroke_) {
		end_stroke();
	}
	const vec3 p = to_body(contact), n = gl::normalize(to_body_direction(normal)), a = to_body_direction(along);
	if (edge_tool(tool) && cut_plan_ && plan_request_ && plan_request_->tool == tool &&
			plan_request_->contact.distance_to(contact) < 1e-6) {
		if (plan_stale_) {
			flush(); // lands the edit and plans again against it
		}
		stroke_ = tools::planned_stroke(*cut_plan_); // the plan it was shown
	} else {
		if (edge_tool(tool)) {
			flush(); // the cutting model reads the body
		}
		stroke_ = make_stroke(tool, p, n, a, settings);
	}
	if (!stroke_) {
		return false;
	}
	planned_.clear(); // the stroke takes the plan's place
	plan_request_.reset();
	cut_plan_.reset();
	plan_report_ = Dictionary();
	// A deferred stroke's work is a layer, which the shader cannot draw: it is applied as it goes.
	previewing_ = stroke_preview_ && !stroke_->deferred();
	std::size_t pending = 0;
	for (const std::vector<Edit> &edits : committing_) {
		pending += edits.size();
	}
	// A stroke merges to at most 3 edits; a chisel's or gouge's with its chips, 14.
	const std::size_t needs = edge_tool(tool) ? 14 : 4;
	if (previewing_ && pending + needs > std::size_t(kOverlayMax)) {
		// No room in the overlay for another stroke until the strokes before it are applied.
		// Only a burst of strokes during slow updates gets here.
		flush();
	}
	return true;
}

Dictionary SdfBody::plan_stroke(const String &tool, const Vector3 &contact, const Vector3 &normal,
		const Vector3 &along, double length, const Dictionary &settings) {
	plan_request_ = PlanRequest{tool, contact, normal, along, length, settings.duplicate()};
	return compute_plan();
}

Dictionary SdfBody::compute_plan() {
	Dictionary report;
	if (!plan_request_) {
		return report;
	}
	const PlanRequest &r = *plan_request_;
	const vec3 p = to_body(r.contact), n = gl::normalize(to_body_direction(r.normal));
	const vec3 a = tools::Frame::at(p, n, to_body_direction(r.along)).x;
	if (edge_tool(r.tool)) {
		if (job_.valid()) {
			// The body is being changed: the last plan stands until the edit lands.
			plan_stale_ = true;
			report = plan_report_.duplicate();
			report["stale"] = true;
			return report;
		}
		const tools::Work work{session_.body(), session_.octree(), materials_};
		const Dictionary &s = r.settings;
		cut_plan_ = tools::plan_cut(chisel_from(r.tool, s), work, p, n, a, float(r.length),
				float(double(s.get("depth", 1.0))), float(double(s.get("skew", 0.0))),
				std::uint32_t(int64_t(s.get("seed", 1))));
		const tools::CutPlan &c = *cut_plan_;
		planned_ = c.edits();
		report["edits"] = int64_t(planned_.size());
		report["depth"] = double(c.depth);
		report["length"] = double(c.length);
		report["force"] = double(c.force);
		report["available"] = double(c.available);
		report["grain"] = double(c.grain);
		report["slope"] = c.slope;
		report["chop"] = c.chop;
		report["blow"] = double(c.blow);
		report["bevel"] = double(c.chisel.bevel_deg);
		PackedStringArray warnings;
		for (const std::string &w : tools::warning_names(c.warnings)) {
			warnings.push_back(String(w.c_str()));
		}
		report["warnings"] = warnings;
		plan_stale_ = false;
		plan_report_ = report;
		update_overlay();
		return report;
	}
	std::unique_ptr<tools::Stroke> stroke = make_stroke(r.tool, p, n, a, r.settings);
	if (!stroke) {
		return report;
	}
	const String &tool = r.tool;
	const double length = r.length;
	// Moved as the hand would, a millimetre at a time (a chisel's ramp shows its direction
	// first, then its run grows), and the saw back again: one stroke of its blade.
	const float mm = std::max(float(length), 0.0f);
	const int steps = std::max(1, int(std::ceil(mm)));
	for (int i = 1; i <= steps; ++i) {
		stroke->move_to(p + a * (mm * float(i) / float(steps)));
	}
	if (tool == "saw") {
		for (int i = steps - 1; i >= 0; --i) {
			stroke->move_to(p + a * (mm * float(i) / float(steps)));
		}
	}
	planned_ = stroke->edits();
	for (const Edit &e : stroke->finish()) {
		planned_.push_back(e);
	}
	// How deep it goes: the deepest point of its tool's pose (where the edge or teeth are).
	const tools::Frame f = stroke->pose();
	report["edits"] = int64_t(planned_.size());
	report["depth"] = double(std::max(0.0f, -gl::dot(f.origin - p, n)));
	report["length"] = double(mm);
	plan_stale_ = false;
	plan_report_ = report;
	update_overlay();
	return report;
}

void SdfBody::clear_plan() {
	plan_request_.reset();
	cut_plan_.reset();
	plan_report_ = Dictionary();
	plan_stale_ = false;
	if (planned_.empty()) {
		return;
	}
	planned_.clear();
	update_overlay();
}

void SdfBody::set_plan_tint(const Color &tint) {
	for (const Ref<ShaderMaterial> &m : {material_, caster_material_}) {
		m->set_shader_parameter("sdf_plan_tint", Vector4(tint.r, tint.g, tint.b, tint.a));
	}
}

void SdfBody::set_opacity(double opacity) {
	opacity_ = std::clamp(opacity, 0.0, 1.0);
	for (const Ref<ShaderMaterial> &m : {material_, caster_material_}) {
		m->set_shader_parameter("sdf_opacity", float(opacity_));
	}
}

void SdfBody::set_section(const Vector3 &point, const Vector3 &normal) {
	if (normal.length_squared() < 1e-12) {
		material_->set_shader_parameter("sdf_section", Vector4());
		return;
	}
	const vec3 n = gl::normalize(to_body_direction(normal));
	material_->set_shader_parameter("sdf_section", Vector4(n.x, n.y, n.z, gl::dot(n, to_body(point))));
}

void SdfBody::move_stroke(const Vector3 &point) {
	if (!stroke_) {
		return;
	}
	tools::StrokeUpdate u = stroke_->move_to(to_body(point));
	if (stroke_->deferred()) {
		queue({Command::WORK, 0, {}, false, stroke_});
		return;
	}
	if (u.empty()) {
		return;
	}
	if (previewing_) {
		update_overlay();
	} else {
		queue({Command::STROKE, u.drop, std::move(u.edits)});
	}
}

void SdfBody::end_stroke() {
	if (!stroke_) {
		return;
	}
	const std::optional<sdf::Separation> through = stroke_->separation();
	if (previewing_) {
		// The whole stroke, merged, in one batch; it stays in the overlay until that lands.
		std::vector<Edit> edits = stroke_->edits();
		for (const Edit &e : stroke_->finish()) {
			edits.push_back(e);
		}
		stroke_.reset();
		if (!edits.empty()) {
			committing_.push_back(edits);
			std::vector<Command> commands;
			commands.push_back({Command::STROKE, 0, std::move(edits)});
			commands.push_back({Command::COMMIT, 0, {}, true});
			commands.back().separation = through;
			queue_all(std::move(commands));
		}
		update_overlay();
		return;
	}
	std::vector<Command> commands;
	if (stroke_->deferred()) {
		commands.push_back({Command::WORK, 0, {}, false, stroke_}); // whatever motion is left
	}
	std::vector<Edit> finish = stroke_->finish();
	if (!finish.empty()) {
		commands.push_back({Command::STROKE, 0, std::move(finish)});
	}
	commands.push_back({Command::COMMIT, 0, {}});
	commands.back().separation = through;
	queue_all(std::move(commands));
	stroke_.reset();
}

void SdfBody::cancel_stroke() {
	if (!stroke_) {
		return;
	}
	stroke_.reset();
	if (previewing_) {
		update_overlay(); // the body never saw it
	} else {
		queue({Command::CANCEL, 0, {}});
	}
}

Transform3D SdfBody::get_tool_pose() const {
	return stroke_ ? frame_to_world(stroke_->pose()) : get_global_transform();
}

Transform3D SdfBody::pose_at(const Vector3 &contact, const Vector3 &normal, const Vector3 &along, double lift) const {
	const vec3 n = gl::normalize(to_body_direction(normal));
	return frame_to_world(tools::Frame::at(to_body(contact) + n * float(lift), n, to_body_direction(along)));
}

void SdfBody::undo() {
	cancel_stroke();
	queue({Command::UNDO, 0, {}});
}

void SdfBody::redo() {
	cancel_stroke();
	queue({Command::REDO, 0, {}});
}

void SdfBody::queue(Command c) {
	if (c.kind != Command::REFINE) {
		sides_.reset(); // the body is about to change (refining changes no surface)
	}
	queue_.push_back(std::move(c));
	if (!job_.valid()) {
		start_job();
	}
}

void SdfBody::queue_all(std::vector<Command> commands) {
	sides_.reset();
	for (Command &c : commands) {
		queue_.push_back(std::move(c));
	}
	if (!job_.valid()) {
		start_job();
	}
}

void SdfBody::start_job() {
	// Fold runs of stroke updates into one: what a later update drops comes off the earlier
	// one's appends first.
	std::vector<Command> batch;
	for (Command &c : queue_) {
		if (c.kind == Command::WORK && !batch.empty() && batch.back().kind == Command::WORK &&
				batch.back().stroke == c.stroke) {
			continue; // one call works all the motion recorded so far
		}
		if (c.kind == Command::STROKE && !batch.empty() && batch.back().kind == Command::STROKE) {
			Command &a = batch.back();
			a.clip = a.clip && c.clip;
			if (c.drop <= a.edits.size()) {
				a.edits.resize(a.edits.size() - c.drop);
			} else {
				a.drop += c.drop - a.edits.size();
				a.edits.clear();
			}
			a.edits.insert(a.edits.end(), c.edits.begin(), c.edits.end());
		} else {
			batch.push_back(std::move(c));
		}
	}
	queue_.clear();
	job_bricks_.clear();
	job_materials_.clear();
	job_previews_ = 0;
	job_refines_ = true;
	job_clips_ = true;
	job_separated_.reset();
	for (const Command &c : batch) {
		job_previews_ += c.previewed;
		job_refines_ = job_refines_ && c.kind == Command::REFINE;
		job_clips_ = job_clips_ && c.clip;
	}
	job_ = std::async(std::launch::async, [this, batch = std::move(batch)]() {
		const auto start = std::chrono::steady_clock::now();
		for (const Command &c : batch) {
			switch (c.kind) {
				case Command::STROKE:
					session_.revise_stroke(c.drop, c.edits);
					break;
				case Command::WORK: {
					const tools::StrokeUpdate u = c.stroke->work(session_.body(), session_.octree());
					if (!u.empty()) {
						session_.revise_stroke(u.drop, u.edits, u.changed);
					}
					break;
				}
				case Command::COMMIT:
					session_.commit();
					if (c.separation) {
						// Cut clean through: are the parts on either side still joined anywhere?
						// If not, measure both here, so that split() has nothing to read.
						const auto t0 = std::chrono::steady_clock::now();
						const Body &body = session_.body();
						if (plane_clear(body, session_.octree(), c.separation->plane, body.bounds())) {
							job_separated_ = measure_sides(session_.adf(), *c.separation, true);
						}
						separation_ms_ = ms_since(t0);
					}
					break;
				case Command::DROP:
					session_.drop_last_step();
					break;
				case Command::CANCEL:
					session_.cancel();
					break;
				case Command::UNDO:
					session_.undo();
					break;
				case Command::REDO:
					session_.redo();
					break;
				case Command::REFINE:
					session_.refine();
					break;
			}
			// Every update's rewritten slots, not just the last one's, need uploading.
			const Adf &adf = session_.adf();
			job_bricks_.insert(job_bricks_.end(), adf.dirty_bricks().begin(), adf.dirty_bricks().end());
			job_materials_.insert(job_materials_.end(), adf.dirty_materials().begin(), adf.dirty_materials().end());
		}
		job_ms_ = ms_since(start);
		collect_gpu_stats();
	});
}

void SdfBody::finish_job() {
	job_.get();
	const auto start = std::chrono::steady_clock::now();
	upload_textures();
	upload_adf(false, job_bricks_, job_materials_);
	update_material();
	// The previewed strokes this batch applied are in the textures now: out of the overlay,
	// in the same frame.
	for (; job_previews_ > 0 && !committing_.empty(); --job_previews_) {
		committing_.pop_front();
	}
	update_overlay();
	upload_ms_ = ms_since(start);
	if (job_refines_) {
		refine_ms_ = job_ms_;
		refresh_stats();
		if (plan_stale_) {
			compute_plan();
		}
		return;
	}
	last_update_ms_ = job_ms_;
	if (!job_clips_) { // the body changed (a split's half-space leaves what split() measured)
		hull_.clear();
		mass_ = -1.0;
	}
	refresh_stats();
	emit_signal("edited", stats_);
	if (plan_stale_) {
		compute_plan(); // against the body as it is now
	}
	if (job_separated_) {
		sides_ = std::move(job_separated_);
		job_separated_.reset();
		hold_refine_ = true;
		const sdf::Plane plane = sides_->cut.plane;
		const Transform3D xf = get_global_transform();
		emit_signal("separated", xf.xform(to_godot(plane.point)), xf.basis.xform(to_godot(plane.normal)).normalized());
	}
}

// --- pieces ------------------------------------------------------------------------------

void SdfBody::set_proxy() {
	// The proxy only needs to cover the body; the octree's root cube is usually larger.
	const Ref<ArrayMesh> proxy = proxy_box(bounds_.expanded(0.5f));
	set_mesh(proxy);
	shadow_caster_->set_mesh(proxy);
}

void SdfBody::unshare_textures() {
	if (textures_.use_count() == 1) {
		return;
	}
	// Another piece still draws these: this body's next upload goes into new ones.
	for (Ref<ImageTexture> *tex :
			{&nodes_tex_, &tape_tex_, &edits_tex_, &materials_tex_, &adf_nodes_tex_, &adf_cells_tex_, &adf_grid_tex_}) {
		tex->unref();
	}
	bricks_tex_.unref();
	brick_materials_tex_.unref();
	textures_ = std::make_shared<char>();
}

void SdfBody::clip(const sdf::Plane &plane) {
	clips_.push_back(plane);
	unclipped_bounds_.push_back(bounds_);
	bounds_ = clip_box(bounds_, plane);
	set_proxy();
	// The half-space edit, drawn by the overlay until the batch taking it in lands.
	const Edit keep = plane.keep_behind();
	committing_.push_back({keep});
	std::vector<Command> commands;
	commands.push_back({Command::STROKE, 0, {keep}});
	commands.push_back({Command::COMMIT, 0, {}, true});
	for (Command &c : commands) {
		c.clip = true;
	}
	queue_all(std::move(commands));
	update_overlay();
}

SdfBody *SdfBody::split(const Vector3 &point, const Vector3 &normal) {
	const auto start = std::chrono::steady_clock::now();
	if (stroke_ || !session_.has_adf()) {
		UtilityFunctions::push_error("SdfBody.split: needs an ADF body with no tool engaged");
		return nullptr;
	}
	const sdf::Plane plane{to_body(point), gl::normalize(to_body_direction(normal))};
	// What the worker measured, if this is the plane separated() reported (either way round).
	std::optional<PieceSides> sides = std::move(sides_);
	sides_.reset();
	if (sides) {
		const float turn = gl::dot(sides->cut.plane.normal, plane.normal);
		if (std::fabs(sides->cut.plane.distance(plane.point)) > 1e-3f || std::fabs(std::fabs(turn) - 1.0f) > 1e-4f) {
			sides.reset();
		} else if (turn < 0.0f) {
			sides->cut = sides->cut.flipped();
			std::swap(sides->volume[0], sides->volume[1]);
			std::swap(sides->centre[0], sides->centre[1]);
			std::swap(sides->hull[0], sides->hull[1]);
		}
	}
	// The new piece copies the session, so nothing may be changing it (at most a refine
	// runs here, if split() comes a frame or more after the signal).
	flush();
	if (!sides) {
		sides = measure_sides(session_.adf(), Separation{plane, 0.0f}, false);
	}
	const Separation cut = sides->cut;
	SdfBody *piece = memnew(SdfBody);
	piece->session_ = session_;
	piece->materials_ = materials_;
	piece->demo_camera_ = demo_camera_;
	piece->debug_view_ = debug_view_;
	piece->live_source_ = live_source_;
	piece->exact_cells_ = exact_cells_;
	piece->stroke_preview_ = stroke_preview_;
	piece->refine_when_idle_ = refine_when_idle_;
	piece->gpu_bricks_ = gpu_bricks_;
	piece->gpu_ = gpu_;
	piece->bounds_ = bounds_;
	piece->clips_ = clips_;
	piece->unclipped_bounds_ = unclipped_bounds_;
	piece->nodes_tex_ = nodes_tex_;
	piece->tape_tex_ = tape_tex_;
	piece->edits_tex_ = edits_tex_;
	piece->materials_tex_ = materials_tex_;
	piece->adf_nodes_tex_ = adf_nodes_tex_;
	piece->adf_cells_tex_ = adf_cells_tex_;
	piece->adf_grid_tex_ = adf_grid_tex_;
	piece->bricks_tex_ = bricks_tex_;
	piece->brick_materials_tex_ = brick_materials_tex_;
	piece->textures_ = textures_;
	piece->update_shaders();
	piece->set_live_shadows(live_shadows_);
	piece->set_transform(get_transform());
	// Each side's physics hull and mass, and everything else that reads the session, before
	// the batches taking in the half-spaces start.
	const double density = double(materials_[session_.body().base_material].density) * 1e-6; // kg per mm^3
	hull_ = std::move(sides->hull[0]);
	volume_ = sides->volume[0];
	centre_ = sides->centre[0];
	mass_ = volume_ * density;
	piece->hull_ = std::move(sides->hull[1]);
	piece->volume_ = sides->volume[1];
	piece->centre_ = sides->centre[1];
	piece->mass_ = piece->volume_ * density;
	update_material();
	piece->update_material();
	refresh_stats();
	piece->refresh_stats();
	clip(cut.behind());
	piece->clip(cut.front());
	split_ms_ = ms_since(start);
	stats_["split_ms"] = split_ms_;
	return piece;
}

void SdfBody::rejoin() {
	flush();
	if (clips_.empty()) {
		return;
	}
	clips_.pop_back();
	bounds_ = unclipped_bounds_.back();
	unclipped_bounds_.pop_back();
	set_proxy();
	hull_.clear();
	mass_ = -1.0;
	queue({Command::DROP, 0, {}});
}

PackedVector3Array SdfBody::get_hull_points() {
	if (hull_.empty() && session_.has_adf()) {
		flush();
		hull_ = hull_points(session_.adf(), clips_.empty() ? nullptr : &clips_.back());
	}
	PackedVector3Array out;
	for (const vec3 &p : hull_) {
		out.push_back(to_godot(p));
	}
	return out;
}

double SdfBody::get_mass() {
	if (mass_ < 0.0 && session_.has_adf()) {
		flush();
		// mm^3 x g/cm^3 = 1e-3 g = 1e-6 kg.
		volume_ = volume(session_.adf(), clips_.empty() ? nullptr : &clips_.back(), &centre_);
		mass_ = volume_ * double(materials_[session_.body().base_material].density) * 1e-6;
	}
	return std::max(mass_, 0.0);
}

double SdfBody::get_volume() {
	get_mass();
	return volume_;
}

Vector3 SdfBody::get_centre_of_mass() {
	get_mass();
	return to_godot(centre_);
}

void SdfBody::update_overlay() {
	std::vector<Edit> edits;
	for (const std::vector<Edit> &stroke : committing_) {
		edits.insert(edits.end(), stroke.begin(), stroke.end());
	}
	if (stroke_ && previewing_) {
		for (const Edit &e : stroke_->edits()) {
			edits.push_back(e);
		}
	}
	const std::size_t planned_from = edits.size(); // the plan's edits come last, flagged
	edits.insert(edits.end(), planned_.begin(), planned_.end());
	overlay_.resize(kOverlayMax * kOverlayTexels);
	overlay_.fill(Vector4());
	overlay_count_ = 0;
	overlay_box_ = Aabb();
	overlay_lipschitz_ = 1.0f;
	for (std::size_t k = 0; k < edits.size(); ++k) {
		const Edit &e = edits[k];
		// The body would refuse what it does not accept, and the shader only cuts (a piece's
		// half-space too: an intersection).
		if (overlay_count_ == kOverlayMax || !Body::accepts(e) || (e.op != Op::Subtract && e.op != Op::Intersect)) {
			continue;
		}
		const Aabb box = e.bounds().expanded(kOverlayMargin);
		const int o = overlay_count_ * kOverlayTexels;
		const int code = int(e.prim.type) | int(e.op) << 3 | int(e.blend) << 6 | int(e.shape) << 9 |
				(k >= planned_from ? kOverlayPlanned : 0);
		overlay_.set(o, Vector4(float(code), e.r, e.r2, float(e.material)));
		for (int i = 0; i < 5; ++i) {
			overlay_.set(o + 1 + i, to_godot(e.prim.p[i]));
		}
		overlay_.set(o + 6, Vector4(box.lo.x, box.lo.y, box.lo.z, 0.0f));
		overlay_.set(o + 7, Vector4(box.hi.x, box.hi.y, box.hi.z, 0.0f));
		overlay_box_.include(box);
		overlay_lipschitz_ = std::max(overlay_lipschitz_, e.lipschitz());
		++overlay_count_;
	}
	for (const Ref<ShaderMaterial> &m : {material_, caster_material_}) {
		apply_overlay(m);
	}
	stats_["overlay_edits"] = overlay_count_;
	stats_["planned_edits"] = int64_t(planned_.size());
}

void SdfBody::apply_overlay(const Ref<ShaderMaterial> &material) const {
	material->set_shader_parameter("sdf_overlay_count", overlay_count_);
	if (overlay_count_ == 0) {
		return;
	}
	material->set_shader_parameter("sdf_overlay", overlay_);
	material->set_shader_parameter("sdf_overlay_lo", to_godot(overlay_box_.lo));
	material->set_shader_parameter("sdf_overlay_hi", to_godot(overlay_box_.hi));
	material->set_shader_parameter("sdf_overlay_lipschitz", overlay_lipschitz_);
}

void SdfBody::flush() {
	while (job_.valid() || !queue_.empty()) {
		if (job_.valid()) {
			job_.wait();
			finish_job();
		}
		if (!queue_.empty()) {
			start_job();
		}
	}
}

void SdfBody::_process(double) {
	if (job_.valid() && job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		finish_job();
	}
	if (!job_.valid() && !queue_.empty()) {
		start_job();
	}
	if (!job_.valid() && queue_.empty() && !stroke_ && refine_when_idle_ && !hold_refine_ && session_.needs_refine()) {
		queue({Command::REFINE, 0, {}});
	}
	hold_refine_ = false;
}

// --- uploads -----------------------------------------------------------------------------

namespace {

// Small layers, so that an edit re-uploads little: a layer is re-sent whole when any of its
// bricks changes. (Up to 131k bricks still fit in the 256 layers every renderer allows.)
constexpr int kLayerSize = 512;
constexpr int kBricksPerLayer = 512; // 8 x 64 tiles of 64 x 8 texels (SDF_BRICKS_PER_LAYER)

// One 512^2 layer of brick tiles: each brick's 8 z-slices of 8x8 samples side by side
// (the layout sdf_live.gdshaderinc reads). `texel_bytes` per sample, copied from `data`.
Ref<godot::Image> brick_layer(const std::uint8_t *data, std::size_t slots, int layer, int texel_bytes,
		godot::Image::Format format) {
	PackedByteArray bytes;
	bytes.resize(int64_t(kLayerSize) * kLayerSize * texel_bytes);
	std::memset(bytes.ptrw(), 0, size_t(bytes.size()));
	std::uint8_t *out = bytes.ptrw();
	for (int tile = 0; tile < kBricksPerLayer; ++tile) {
		const std::size_t slot = std::size_t(layer) * kBricksPerLayer + std::size_t(tile);
		if (slot >= slots) {
			break;
		}
		const int ox = (tile % 8) * 64, oy = (tile / 8) * 8;
		for (int z = 0; z < 8; ++z) {
			for (int y = 0; y < 8; ++y) {
				const std::uint8_t *row = data + (slot * Adf::kBrickSamples + std::size_t(z * 64 + y * 8)) * texel_bytes;
				std::memcpy(out + (std::size_t(oy + y) * kLayerSize + std::size_t(ox + z * 8)) * texel_bytes, row,
						std::size_t(8 * texel_bytes));
			}
		}
	}
	return godot::Image::create_from_data(kLayerSize, kLayerSize, false, format, bytes);
}

// Brings a layered texture up to date: all layers on request or when the slots outgrow it
// (made a quarter larger than needed then, so that a growing brick pool seldom sends it
// whole again), otherwise only those holding `dirty` slots.
void upload_layers(Ref<Texture2DArray> &tex, const std::uint8_t *data, std::size_t slots, int texel_bytes,
		godot::Image::Format format, bool full, const std::vector<std::uint32_t> &dirty) {
	const int needed = std::max<int>(1, int((slots + kBricksPerLayer - 1) / kBricksPerLayer));
	if (full || tex.is_null() || tex->get_layers() < needed) {
		const int layers = needed + needed / 4 + 1;
		TypedArray<Ref<godot::Image>> images;
		for (int l = 0; l < layers; ++l) {
			images.push_back(brick_layer(data, slots, l, texel_bytes, format));
		}
		if (tex.is_null()) {
			tex.instantiate();
		}
		tex->create_from_images(images);
		return;
	}
	std::vector<int> touched;
	for (std::uint32_t slot : dirty) {
		touched.push_back(int(slot / kBricksPerLayer));
	}
	std::sort(touched.begin(), touched.end());
	touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
	for (int l : touched) {
		tex->update_layer(brick_layer(data, slots, l, texel_bytes, format), l);
	}
}

} // namespace

void SdfBody::upload_all() {
	upload_textures();
	upload_adf(true, {}, {});
}

void SdfBody::upload_adf(bool full, const std::vector<std::uint32_t> &bricks,
		const std::vector<std::uint32_t> &materials) {
	unshare_textures();
	if (!session_.has_adf()) {
		return;
	}
	const Adf &adf = session_.adf();
	std::vector<float> node_texels;
	node_texels.reserve(adf.nodes().size() * 4);
	for (const Adf::Node &n : adf.nodes()) {
		push(node_texels, vec4(float(n.child), float(n.brick), float(n.material), n.value));
	}
	upload(adf_nodes_tex_, node_texels);
	static_assert(Adf::kGridLevels == 6, "SDF_GRID_LEVELS in sdf_live.gdshaderinc");
	std::vector<float> grid_texels;
	grid_texels.reserve(adf.grid().size() * 2);
	for (const Adf::GridCell &cell : adf.grid()) {
		grid_texels.push_back(float(cell.node));
		grid_texels.push_back(float(cell.depth));
	}
	upload(adf_grid_tex_, grid_texels);
	upload_layers(bricks_tex_, reinterpret_cast<const std::uint8_t *>(adf.brick_values().data()), adf.brick_slots(), 2,
			godot::Image::FORMAT_RH, full, bricks);
	upload_layers(brick_materials_tex_, adf.material_values().data(), adf.material_slots(), 4,
			godot::Image::FORMAT_RGBA8, full, materials);
}

// Indices travel as floats (Godot images have no integer formats), exact below 2^24: fine
// for carving sessions, but a 100k-chip stone job's tapes approach that, and a texture's
// 16384-row limit caps each table at 16.7M texels. Large bodies will need integer storage
// buffers through RenderingDevice (milestone E8).
void SdfBody::upload_textures() {
	unshare_textures();
	const Body &body = session_.body();
	const Octree &octree = session_.octree();
	// The ADF source reads only its exact cells' tapes; the octree's are for EXACT.
	const bool octree_tapes = live_source_ == LIVE_EXACT;

	std::vector<float> node_texels, tape_values;
	if (octree_tapes) {
		const auto &nodes = octree.nodes();
		const auto &leaves = octree.leaves();
		node_texels.reserve(nodes.size() * kNodeTexels * 4);
		for (const Octree::Node &n : nodes) {
			if (n.child >= 0) {
				push(node_texels, vec4(float(n.child), 0, 0, 0));
				push(node_texels, vec4(0.0f));
				continue;
			}
			const Octree::Leaf &leaf = leaves[std::size_t(n.leaf)];
			const float flags = float(int(leaf.state) + 1 + (leaf.base ? 3 : 0));
			push(node_texels, vec4(-1.0f, float(tape_values.size()), float(leaf.tape.size()), flags));
			push(node_texels, vec4(leaf.lipschitz, 0, 0, 0));
			for (std::uint32_t entry : leaf.tape) {
				const float code = float((entry & ~Octree::kResetBit) + 1);
				tape_values.push_back(entry & Octree::kResetBit ? -code : code);
			}
		}
	}
	// The ADF's exact leaves have their own (shorter) tapes: append them, and describe each
	// leaf with a texel in the octree nodes' format so the shader evaluates it the same way
	// (x, unused by the tape interpreter, carries the brick's error band).
	std::vector<float> cell_texels;
	if (session_.has_adf()) {
		const Adf &adf = session_.adf();
		const std::size_t adf_tape_base = tape_values.size();
		for (std::uint32_t entry : adf.exact_tape()) {
			const float code = float((entry & ~Octree::kResetBit) + 1);
			tape_values.push_back(entry & Octree::kResetBit ? -code : code);
		}
		for (const Adf::ExactCell &cell : adf.exact_cells()) {
			push(cell_texels, vec4(cell.error, float(adf_tape_base + cell.offset), float(cell.count), cell.base ? 4.0f : 1.0f));
		}
	}
	upload(adf_cells_tex_, cell_texels);
	while (tape_values.size() % 4) {
		tape_values.push_back(0.0f);
	}

	std::vector<float> edit_texels;
	edit_texels.reserve(body.edits().size() * kEditTexels * 4);
	for (const Edit &e : body.edits()) {
		const int code = int(e.prim.type) | int(e.op) << 3 | int(e.blend) << 6 | int(e.shape) << 9;
		push(edit_texels, vec4(float(code), e.r, e.r2, float(e.material)));
		for (const vec4 &p : e.prim.p) {
			push(edit_texels, p);
		}
	}

	std::vector<float> material_texels;
	material_texels.reserve(materials_.size() * kMaterialTexels * 4);
	for (std::size_t i = 0; i < materials_.size(); ++i) {
		const Material &m = materials_[std::uint16_t(i)];
		push(material_texels, m.k[0]);
		push(material_texels, m.k[1]);
		push(material_texels, m.k[2]);
		push(material_texels, vec4(m.specular, m.shininess, 0, 0));
	}

	upload(nodes_tex_, node_texels);
	upload(tape_tex_, tape_values);
	upload(edits_tex_, edit_texels);
	upload(materials_tex_, material_texels);
}

void SdfBody::update_material() {
	for (const Ref<ShaderMaterial> &m : {material_, caster_material_}) {
		apply_parameters(m);
	}
}

void SdfBody::apply_parameters(const Ref<ShaderMaterial> &material) {
	const Body &body = session_.body();
	const Octree::Node &root = session_.octree().nodes()[0];
	const Aabb box = bounds_.expanded(0.5f);
	material->set_shader_parameter("sdf_nodes", nodes_tex_);
	material->set_shader_parameter("sdf_tape", tape_tex_);
	material->set_shader_parameter("sdf_edits", edits_tex_);
	material->set_shader_parameter("sdf_materials", materials_tex_);
	material->set_shader_parameter("sdf_adf_nodes", adf_nodes_tex_);
	material->set_shader_parameter("sdf_adf_cells", adf_cells_tex_);
	material->set_shader_parameter("sdf_adf_grid", adf_grid_tex_);
	if (session_.has_adf()) {
		material->set_shader_parameter("sdf_grid_scale", session_.adf().grid_scale());
	}
	material->set_shader_parameter("sdf_bricks", bricks_tex_);
	material->set_shader_parameter("sdf_brick_materials", brick_materials_tex_);
	material->set_shader_parameter("sdf_root_lo", to_godot(root.lo));
	material->set_shader_parameter("sdf_root_size", root.size);
	material->set_shader_parameter("sdf_box_lo", to_godot(box.lo));
	material->set_shader_parameter("sdf_box_hi", to_godot(box.hi));
	material->set_shader_parameter("sdf_base_type", int(body.base.type));
	const char *names[] = {"sdf_base_p0", "sdf_base_p1", "sdf_base_p2", "sdf_base_p3", "sdf_base_p4"};
	for (int i = 0; i < 5; ++i) {
		material->set_shader_parameter(names[i], to_godot(body.base.p[i]));
	}
	material->set_shader_parameter("sdf_base_material", float(body.base_material));
	material->set_shader_parameter("sdf_grain_origin", to_godot(body.grain_origin));
	material->set_shader_parameter("sdf_grain_axis", to_godot(body.grain_axis));
	material->set_shader_parameter("sdf_debug_view", debug_view_);
	material->set_shader_parameter("sdf_exact_cells", exact_cells_);
	material->set_shader_parameter("sdf_opacity", float(opacity_));
	apply_overlay(material);
}

void SdfBody::set_debug_view(int view) {
	debug_view_ = view;
	material_->set_shader_parameter("sdf_debug_view", debug_view_);
}

void SdfBody::refresh_stats() {
	const Octree::Stats s = session_.octree().stats();
	Dictionary d;
	d["edits"] = int64_t(session_.body().edits().size());
	d["steps"] = int64_t(session_.steps());
	d["can_undo"] = session_.can_undo();
	d["can_redo"] = session_.can_redo();
	d["nodes"] = int64_t(s.nodes);
	d["surface_leaves"] = int64_t(s.surface_leaves);
	d["mean_tape"] = s.mean_surface_tape;
	d["max_tape"] = int64_t(s.max_tape);
	const Adf::Stats a = session_.adf().stats();
	d["adf_bricks"] = int64_t(a.bricks);
	d["adf_exact_cells"] = int64_t(a.exact_leaves);
	d["adf_mb"] = double(a.bytes) / 1048576.0;
	d["adf_finest_voxel"] = a.finest_voxel;
	d["update_ms"] = last_update_ms_;
	d["refine_ms"] = refine_ms_; // the last refinement of coarse cells, in the background
	d["refine_pending"] = session_.needs_refine();
	d["separation_ms"] = separation_ms_; // the worker's last check whether a cut left two parts (and measuring them)
	d["split_ms"] = split_ms_;           // the last split(), on the main thread
	d["pieces_split"] = int64_t(clips_.size());
	d["sampler"] = gpu_ ? "gpu" : "cpu"; // where ADF bricks are sampled
	d["gpu_ms"] = job_gpu_ms_;           // the last batch's time on the GPU sampler (with transfers)
	d["gpu_bricks"] = int64_t(job_gpu_jobs_);
	d["upload_ms"] = upload_ms_;
	d["overlay_edits"] = overlay_count_; // edits the shader draws on top (previewed strokes)
	stats_ = d;
}

AABB SdfBody::get_body_bounds() const {
	return AABB(to_godot(bounds_.lo), to_godot(bounds_.size()));
}

void SdfBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_demo", "name"), &SdfBody::load_demo);
	ClassDB::bind_method(D_METHOD("load_tool", "name", "settings"), &SdfBody::load_tool, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_demo_camera"), &SdfBody::get_demo_camera);
	ClassDB::bind_method(D_METHOD("add_random_strokes", "count", "seed"), &SdfBody::add_random_strokes);
	ClassDB::bind_method(D_METHOD("raycast", "from", "direction", "max_distance"), &SdfBody::raycast);
	ClassDB::bind_method(D_METHOD("begin_stroke", "tool", "contact", "normal", "along", "settings"), &SdfBody::begin_stroke,
			DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("plan_stroke", "tool", "contact", "normal", "along", "length", "settings"),
			&SdfBody::plan_stroke, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("clear_plan"), &SdfBody::clear_plan);
	ClassDB::bind_method(D_METHOD("get_plan"), &SdfBody::get_plan);
	ClassDB::bind_static_method("SdfBody", D_METHOD("tool_catalog"), &SdfBody::tool_catalog);
	ClassDB::bind_method(D_METHOD("set_plan_tint", "tint"), &SdfBody::set_plan_tint);
	ClassDB::bind_method(D_METHOD("set_opacity", "opacity"), &SdfBody::set_opacity);
	ClassDB::bind_method(D_METHOD("get_opacity"), &SdfBody::get_opacity);
	ClassDB::bind_method(D_METHOD("set_section", "point", "normal"), &SdfBody::set_section);
	ClassDB::bind_method(D_METHOD("move_stroke", "point"), &SdfBody::move_stroke);
	ClassDB::bind_method(D_METHOD("end_stroke"), &SdfBody::end_stroke);
	ClassDB::bind_method(D_METHOD("cancel_stroke"), &SdfBody::cancel_stroke);
	ClassDB::bind_method(D_METHOD("is_stroking"), &SdfBody::is_stroking);
	ClassDB::bind_method(D_METHOD("get_tool_pose"), &SdfBody::get_tool_pose);
	ClassDB::bind_method(D_METHOD("pose_at", "contact", "normal", "along", "lift"), &SdfBody::pose_at);
	ClassDB::bind_method(D_METHOD("undo"), &SdfBody::undo);
	ClassDB::bind_method(D_METHOD("redo"), &SdfBody::redo);
	ClassDB::bind_method(D_METHOD("can_undo"), &SdfBody::can_undo);
	ClassDB::bind_method(D_METHOD("can_redo"), &SdfBody::can_redo);
	ClassDB::bind_method(D_METHOD("is_busy"), &SdfBody::is_busy);
	ClassDB::bind_method(D_METHOD("flush"), &SdfBody::flush);
	ClassDB::bind_method(D_METHOD("set_stroke_preview", "enabled"), &SdfBody::set_stroke_preview);
	ClassDB::bind_method(D_METHOD("get_stroke_preview"), &SdfBody::get_stroke_preview);
	ClassDB::bind_method(D_METHOD("split", "point", "normal"), &SdfBody::split);
	ClassDB::bind_method(D_METHOD("rejoin"), &SdfBody::rejoin);
	ClassDB::bind_method(D_METHOD("get_hull_points"), &SdfBody::get_hull_points);
	ClassDB::bind_method(D_METHOD("get_mass"), &SdfBody::get_mass);
	ClassDB::bind_method(D_METHOD("get_centre_of_mass"), &SdfBody::get_centre_of_mass);
	ClassDB::bind_method(D_METHOD("get_volume"), &SdfBody::get_volume);
	ClassDB::bind_method(D_METHOD("set_gpu_bricks", "enabled"), &SdfBody::set_gpu_bricks);
	ClassDB::bind_method(D_METHOD("get_gpu_bricks"), &SdfBody::get_gpu_bricks);
	ClassDB::bind_method(D_METHOD("compare_bricks_with_cpu", "points"), &SdfBody::compare_bricks_with_cpu);
	ClassDB::bind_method(D_METHOD("set_refine_when_idle", "enabled"), &SdfBody::set_refine_when_idle);
	ClassDB::bind_method(D_METHOD("get_refine_when_idle"), &SdfBody::get_refine_when_idle);
	ClassDB::bind_method(D_METHOD("set_live_shadows", "enabled"), &SdfBody::set_live_shadows);
	ClassDB::bind_method(D_METHOD("get_live_shadows"), &SdfBody::get_live_shadows);
	ClassDB::bind_method(D_METHOD("set_live_source", "source"), &SdfBody::set_live_source);
	ClassDB::bind_method(D_METHOD("get_live_source"), &SdfBody::get_live_source);
	ClassDB::bind_method(D_METHOD("set_exact_cells", "enabled"), &SdfBody::set_exact_cells);
	ClassDB::bind_method(D_METHOD("get_exact_cells"), &SdfBody::get_exact_cells);
	ClassDB::bind_method(D_METHOD("set_debug_view", "view"), &SdfBody::set_debug_view);
	ClassDB::bind_method(D_METHOD("get_debug_view"), &SdfBody::get_debug_view);
	ClassDB::bind_method(D_METHOD("get_stats"), &SdfBody::get_stats);
	ClassDB::bind_method(D_METHOD("get_body_bounds"), &SdfBody::get_body_bounds);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_view", PROPERTY_HINT_ENUM, "Shaded,Normals,Steps,Albedo"), "set_debug_view",
			"get_debug_view");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "live_shadows"), "set_live_shadows", "get_live_shadows");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "live_source", PROPERTY_HINT_ENUM, "ADF,Exact"), "set_live_source",
			"get_live_source");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exact_cells"), "set_exact_cells", "get_exact_cells");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stroke_preview"), "set_stroke_preview", "get_stroke_preview");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_opacity", "get_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "refine_when_idle"), "set_refine_when_idle", "get_refine_when_idle");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "gpu_bricks"), "set_gpu_bricks", "get_gpu_bricks");
	ADD_SIGNAL(MethodInfo("edited", PropertyInfo(Variant::DICTIONARY, "stats")));
	ADD_SIGNAL(MethodInfo("separated", PropertyInfo(Variant::VECTOR3, "point"), PropertyInfo(Variant::VECTOR3, "normal")));
	BIND_ENUM_CONSTANT(LIVE_ADF);
	BIND_ENUM_CONSTANT(LIVE_EXACT);
	BIND_ENUM_CONSTANT(SHADED);
	BIND_ENUM_CONSTANT(NORMALS);
	BIND_ENUM_CONSTANT(STEPS);
	BIND_ENUM_CONSTANT(ALBEDO);
}

} // namespace sdf::godot_bind
