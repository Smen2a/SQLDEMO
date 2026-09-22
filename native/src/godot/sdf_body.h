#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "compile/octree.h"
#include "eval/reference_renderer.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace sdf::godot_bind {

// A carvable part rendered live: the body's octree is flattened into data textures and a
// proxy box raymarches it with the shared SDF code (game/shaders/sdf/sdf_live.gdshader).
// The node's local space is the body's space, in millimetres; scale the node (0.001 for a
// metre-scaled world) to place it.
class SdfBody : public godot::MeshInstance3D {
	GDCLASS(SdfBody, godot::MeshInstance3D)

public:
	enum DebugView { SHADED = 0, NORMALS = 1, STEPS = 2, ALBEDO = 3 };

	SdfBody();

	// Builds a demo body by name (see sdf::demo::named_demo): "carved_panel", "blend_<i>",
	// "material_<i>", "sphere", "session".
	bool load_demo(const godot::String &name);
	// The camera that frames the last demo, in body space: {eye, target, up, fov}.
	godot::Dictionary get_demo_camera() const;
	// Carves `count` random strokes into the body's top face, updating the octree
	// incrementally and re-uploading the data textures once.
	void add_random_strokes(int count, int seed);

	void set_debug_view(int view);
	int get_debug_view() const { return debug_view_; }
	godot::Dictionary get_stats() const;
	godot::AABB get_body_bounds() const;

protected:
	static void _bind_methods();

private:
	void rebuild();          // octree + proxy mesh + textures
	void upload_textures();  // flatten octree, edits and materials into data textures
	void update_material();

	sdf::Body body_;
	sdf::Octree octree_;
	sdf::MaterialTable materials_ = sdf::MaterialTable::standard();
	sdf::Camera demo_camera_;
	int debug_view_ = SHADED;

	godot::Ref<godot::ShaderMaterial> material_;
	godot::Ref<godot::ImageTexture> nodes_tex_, tape_tex_, edits_tex_, materials_tex_;
};

} // namespace sdf::godot_bind

VARIANT_ENUM_CAST(sdf::godot_bind::SdfBody::DebugView);
