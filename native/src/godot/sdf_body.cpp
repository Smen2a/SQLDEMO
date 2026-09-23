#include "godot/sdf_body.h"

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
static_assert(int(Prim::SweepBezier) < 8 && int(Op::Paint) < 8 && int(Blend::Profile) < 8 &&
				int(EdgeProfile::Ogee) < 4,
		"edit kinds outgrew their bits in sdf_live.gdshaderinc");
constexpr int kMaterialTexels = 4;

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
	demo_camera_ = camera;
	const auto start = std::chrono::steady_clock::now();
	session_.reset(body, {}, live_source_ == LIVE_ADF);
	last_update_ms_ = ms_since(start);
	rebuild();
	return true;
}

bool SdfBody::load_tool(const String &name, const Dictionary &settings) {
	Body model;
	if (name == "chisel") {
		tools::Chisel chisel;
		chisel.width = float(double(settings.get("width", 12.0)));
		model = chisel.model();
	} else if (name == "saw") {
		model = tools::Saw{}.model();
	} else if (name == "sanding_block") {
		tools::SandingBlock block;
		block.grit = int(settings.get("grit", 120));
		model = block.model();
	} else {
		UtilityFunctions::push_error("SdfBody: unknown tool ", name);
		return false;
	}
	flush();
	stroke_.reset();
	// A tool has a handful of edits: its exact tapes are short, and it needs no ADF.
	live_source_ = LIVE_EXACT;
	update_shaders();
	const auto start = std::chrono::steady_clock::now();
	session_.reset(model, {}, false);
	last_update_ms_ = ms_since(start);
	set_live_shadows(true);
	rebuild();
	return true;
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
	const auto start = std::chrono::steady_clock::now();
	std::vector<Edit> strokes;
	for (const Edit &e : demo::random_strokes(session_.body(), count, std::uint32_t(seed))) {
		if (Body::accepts(e)) {
			strokes.push_back(e);
		}
	}
	session_.set_stroke(strokes);
	session_.commit();
	upload_textures();
	upload_adf(false, session_.adf().dirty_bricks(), session_.adf().dirty_materials());
	update_material();
	last_update_ms_ = ms_since(start);
	refresh_stats();
}

void SdfBody::rebuild() {
	bounds_ = session_.body().bounds();
	// The proxy only needs to cover the body; the octree's root cube is usually larger.
	const Ref<ArrayMesh> proxy = proxy_box(bounds_.expanded(0.5f));
	set_mesh(proxy);
	shadow_caster_->set_mesh(proxy);
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
	if (job_.valid()) {
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

bool SdfBody::begin_stroke(const String &tool, const Vector3 &contact, const Vector3 &normal, const Vector3 &along,
		const Dictionary &settings) {
	if (stroke_) {
		end_stroke();
	}
	const vec3 p = to_body(contact), n = gl::normalize(to_body_direction(normal)), a = to_body_direction(along);
	if (tool == "chisel") {
		tools::Chisel chisel;
		chisel.width = float(double(settings.get("width", 12.0)));
		stroke_ = tools::chisel_stroke(chisel, p, n, a, float(double(settings.get("depth", 1.0))));
	} else if (tool == "saw") {
		stroke_ = tools::saw_stroke(tools::Saw{}, p, n, a, float(double(settings.get("feed", 0.02))));
	} else if (tool == "sanding_block") {
		tools::SandingBlock block;
		block.grit = int(settings.get("grit", 120));
		stroke_ = tools::sanding_stroke(block, p, n, a);
	} else {
		UtilityFunctions::push_error("SdfBody: unknown tool ", tool);
		return false;
	}
	return true;
}

void SdfBody::move_stroke(const Vector3 &point) {
	if (!stroke_) {
		return;
	}
	tools::StrokeUpdate u = stroke_->move_to(to_body(point));
	if (!u.empty()) {
		queue({u.replace ? Command::REPLACE : Command::EXTEND, std::move(u.edits)});
	}
}

void SdfBody::end_stroke() {
	if (!stroke_) {
		return;
	}
	std::vector<Edit> finish = stroke_->finish();
	if (!finish.empty()) {
		queue({Command::EXTEND, std::move(finish)});
	}
	queue({Command::COMMIT, {}});
	stroke_.reset();
}

void SdfBody::cancel_stroke() {
	if (stroke_) {
		queue({Command::CANCEL, {}});
		stroke_.reset();
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
	queue({Command::UNDO, {}});
}

void SdfBody::redo() {
	cancel_stroke();
	queue({Command::REDO, {}});
}

void SdfBody::queue(Command c) {
	queue_.push_back(std::move(c));
	if (!job_.valid()) {
		start_job();
	}
}

void SdfBody::start_job() {
	// Fold runs of stroke updates: a replace makes whatever the stroke did before it moot,
	// and appends pile up.
	std::vector<Command> batch;
	for (Command &c : queue_) {
		const bool stroke = c.kind == Command::REPLACE || c.kind == Command::EXTEND;
		const bool after_stroke =
				!batch.empty() && (batch.back().kind == Command::REPLACE || batch.back().kind == Command::EXTEND);
		if (stroke && after_stroke) {
			if (c.kind == Command::REPLACE) {
				batch.back() = std::move(c);
			} else {
				batch.back().edits.insert(batch.back().edits.end(), c.edits.begin(), c.edits.end());
			}
		} else {
			batch.push_back(std::move(c));
		}
	}
	queue_.clear();
	job_bricks_.clear();
	job_materials_.clear();
	job_ = std::async(std::launch::async, [this, batch = std::move(batch)]() {
		const auto start = std::chrono::steady_clock::now();
		for (const Command &c : batch) {
			switch (c.kind) {
				case Command::REPLACE:
					session_.set_stroke(c.edits);
					break;
				case Command::EXTEND:
					session_.extend_stroke(c.edits);
					break;
				case Command::COMMIT:
					session_.commit();
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
			}
			// Every update's rewritten slots, not just the last one's, need uploading.
			const Adf &adf = session_.adf();
			job_bricks_.insert(job_bricks_.end(), adf.dirty_bricks().begin(), adf.dirty_bricks().end());
			job_materials_.insert(job_materials_.end(), adf.dirty_materials().begin(), adf.dirty_materials().end());
		}
		job_ms_ = ms_since(start);
	});
}

void SdfBody::finish_job() {
	job_.get();
	const auto start = std::chrono::steady_clock::now();
	upload_textures();
	upload_adf(false, job_bricks_, job_materials_);
	update_material();
	upload_ms_ = ms_since(start);
	last_update_ms_ = job_ms_;
	refresh_stats();
	emit_signal("edited", stats_);
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
}

// --- uploads -----------------------------------------------------------------------------

namespace {

constexpr int kLayerSize = 1024;
constexpr int kBricksPerLayer = 2048; // 16 x 128 tiles of 64 x 8 texels

// One 1024^2 layer of brick tiles: each brick's 8 z-slices of 8x8 samples side by side
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
		const int ox = (tile % 16) * 64, oy = (tile / 16) * 8;
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

// Brings a layered texture up to date: all layers when its layer count changes (or on
// request), otherwise only those holding `dirty` slots.
void upload_layers(Ref<Texture2DArray> &tex, const std::uint8_t *data, std::size_t slots, int texel_bytes,
		godot::Image::Format format, bool full, const std::vector<std::uint32_t> &dirty) {
	const int layers = std::max<int>(1, int((slots + kBricksPerLayer - 1) / kBricksPerLayer));
	if (full || tex.is_null() || tex->get_layers() != layers) {
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
	d["upload_ms"] = upload_ms_;
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
	ADD_SIGNAL(MethodInfo("edited", PropertyInfo(Variant::DICTIONARY, "stats")));
	BIND_ENUM_CONSTANT(LIVE_ADF);
	BIND_ENUM_CONSTANT(LIVE_EXACT);
	BIND_ENUM_CONSTANT(SHADED);
	BIND_ENUM_CONSTANT(NORMALS);
	BIND_ENUM_CONSTANT(STEPS);
	BIND_ENUM_CONSTANT(ALBEDO);
}

} // namespace sdf::godot_bind
