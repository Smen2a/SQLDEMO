#include "godot/sdf_body.h"

#include "demo/gallery.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
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

} // namespace

SdfBody::SdfBody() {
	opaque_shader_ = ResourceLoader::get_singleton()->load("res://shaders/sdf/sdf_live.gdshader");
	single_pass_shader_ = ResourceLoader::get_singleton()->load("res://shaders/sdf/sdf_live_single.gdshader");
	material_.instantiate();
	set_material_override(material_);
	update_pipeline();
}

void SdfBody::update_pipeline() {
	const bool opaque = live_shadows_ || !live_single_pass_;
	material_->set_shader(opaque ? opaque_shader_ : single_pass_shader_);
	set_cast_shadows_setting(live_shadows_ ? SHADOW_CASTING_SETTING_ON : SHADOW_CASTING_SETTING_OFF);
	if (octree_.nodes().empty()) {
		return;
	}
	update_material(); // a new shader starts from default parameters
}

void SdfBody::set_live_shadows(bool enabled) {
	live_shadows_ = enabled;
	update_pipeline();
}

void SdfBody::set_live_single_pass(bool enabled) {
	live_single_pass_ = enabled;
	update_pipeline();
}

bool SdfBody::load_demo(const String &name) {
	Body body;
	Camera camera;
	if (!demo::named_demo(name.utf8().get_data(), body, camera)) {
		UtilityFunctions::push_error("SdfBody: unknown demo ", name);
		return false;
	}
	body_ = std::move(body);
	demo_camera_ = camera;
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
	for (const Edit &e : demo::random_strokes(body_, count, std::uint32_t(seed))) {
		if (body_.add(e)) {
			octree_.add_edit(body_, std::uint32_t(body_.edits().size() - 1));
		}
	}
	upload_textures();
	update_material();
}

void SdfBody::rebuild() {
	octree_.build(body_);
	// The proxy only needs to cover the body; the octree's root cube is usually larger.
	set_mesh(proxy_box(body_.bounds().expanded(0.5f)));
	upload_textures();
	update_material();
}

// Indices travel as floats (Godot images have no integer formats), exact below 2^24: fine
// for carving sessions, but a 100k-chip stone job's tapes approach that, and a texture's
// 16384-row limit caps each table at 16.7M texels. Large bodies will need integer storage
// buffers through RenderingDevice (milestone E8).
void SdfBody::upload_textures() {
	const auto &nodes = octree_.nodes();
	const auto &leaves = octree_.leaves();

	std::vector<float> node_texels, tape_values;
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
	while (tape_values.size() % 4) {
		tape_values.push_back(0.0f);
	}

	std::vector<float> edit_texels;
	edit_texels.reserve(body_.edits().size() * kEditTexels * 4);
	for (const Edit &e : body_.edits()) {
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
	const Octree::Node &root = octree_.nodes()[0];
	const Aabb box = body_.bounds().expanded(0.5f);
	material_->set_shader_parameter("sdf_nodes", nodes_tex_);
	material_->set_shader_parameter("sdf_tape", tape_tex_);
	material_->set_shader_parameter("sdf_edits", edits_tex_);
	material_->set_shader_parameter("sdf_materials", materials_tex_);
	material_->set_shader_parameter("sdf_root_lo", to_godot(root.lo));
	material_->set_shader_parameter("sdf_root_size", root.size);
	material_->set_shader_parameter("sdf_box_lo", to_godot(box.lo));
	material_->set_shader_parameter("sdf_box_hi", to_godot(box.hi));
	material_->set_shader_parameter("sdf_base_type", int(body_.base.type));
	const char *names[] = {"sdf_base_p0", "sdf_base_p1", "sdf_base_p2", "sdf_base_p3", "sdf_base_p4"};
	for (int i = 0; i < 5; ++i) {
		material_->set_shader_parameter(names[i], to_godot(body_.base.p[i]));
	}
	material_->set_shader_parameter("sdf_base_material", float(body_.base_material));
	material_->set_shader_parameter("sdf_grain_origin", to_godot(body_.grain_origin));
	material_->set_shader_parameter("sdf_grain_axis", to_godot(body_.grain_axis));
	material_->set_shader_parameter("sdf_debug_view", debug_view_);
}

void SdfBody::set_debug_view(int view) {
	debug_view_ = view;
	material_->set_shader_parameter("sdf_debug_view", debug_view_);
}

Dictionary SdfBody::get_stats() const {
	const Octree::Stats s = octree_.stats();
	Dictionary d;
	d["edits"] = int64_t(body_.edits().size());
	d["nodes"] = int64_t(s.nodes);
	d["surface_leaves"] = int64_t(s.surface_leaves);
	d["mean_tape"] = s.mean_surface_tape;
	d["max_tape"] = int64_t(s.max_tape);
	return d;
}

AABB SdfBody::get_body_bounds() const {
	const Aabb b = body_.bounds();
	return AABB(to_godot(b.lo), to_godot(b.size()));
}

void SdfBody::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load_demo", "name"), &SdfBody::load_demo);
	ClassDB::bind_method(D_METHOD("get_demo_camera"), &SdfBody::get_demo_camera);
	ClassDB::bind_method(D_METHOD("add_random_strokes", "count", "seed"), &SdfBody::add_random_strokes);
	ClassDB::bind_method(D_METHOD("set_live_shadows", "enabled"), &SdfBody::set_live_shadows);
	ClassDB::bind_method(D_METHOD("get_live_shadows"), &SdfBody::get_live_shadows);
	ClassDB::bind_method(D_METHOD("set_live_single_pass", "enabled"), &SdfBody::set_live_single_pass);
	ClassDB::bind_method(D_METHOD("get_live_single_pass"), &SdfBody::get_live_single_pass);
	ClassDB::bind_method(D_METHOD("set_debug_view", "view"), &SdfBody::set_debug_view);
	ClassDB::bind_method(D_METHOD("get_debug_view"), &SdfBody::get_debug_view);
	ClassDB::bind_method(D_METHOD("get_stats"), &SdfBody::get_stats);
	ClassDB::bind_method(D_METHOD("get_body_bounds"), &SdfBody::get_body_bounds);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_view", PROPERTY_HINT_ENUM, "Shaded,Normals,Steps,Albedo"), "set_debug_view",
			"get_debug_view");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "live_shadows"), "set_live_shadows", "get_live_shadows");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "live_single_pass"), "set_live_single_pass", "get_live_single_pass");
	BIND_ENUM_CONSTANT(SHADED);
	BIND_ENUM_CONSTANT(NORMALS);
	BIND_ENUM_CONSTANT(STEPS);
	BIND_ENUM_CONSTANT(ALBEDO);
}

} // namespace sdf::godot_bind
