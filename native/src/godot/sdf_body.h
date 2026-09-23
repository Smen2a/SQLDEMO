#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "compile/octree.h"
#include "eval/reference_renderer.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace sdf::godot_bind {

// A carvable part rendered live: the body's octree is flattened into data textures and a
// proxy box raymarches it with the shared SDF code (game/shaders/sdf/sdf_live*.gdshader).
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
	// Whether the Live body casts shadow-map shadows. That runs the raymarch again in every
	// shadow pass (each directional split), which can cost more than drawing the body, so
	// it is off by default; the baked mesh will cast them instead.
	void set_live_shadows(bool enabled);
	bool get_live_shadows() const { return live_shadows_; }
	// Draw in Godot's transparent pipeline, which runs the raymarch once per pixel instead
	// of twice (depth prepass + colour pass). Ignored while live_shadows is on: that needs
	// the opaque pipeline.
	void set_live_single_pass(bool enabled);
	bool get_live_single_pass() const { return live_single_pass_; }
	godot::Dictionary get_stats() const;
	godot::AABB get_body_bounds() const;

protected:
	static void _bind_methods();

private:
	void rebuild();          // octree + proxy mesh + textures
	void upload_textures();  // flatten octree, edits and materials into data textures
	void update_material();
	void update_pipeline(); // shader and shadow casting from live_shadows / live_single_pass

	sdf::Body body_;
	sdf::Octree octree_;
	sdf::MaterialTable materials_ = sdf::MaterialTable::standard();
	sdf::Camera demo_camera_;
	int debug_view_ = SHADED;
	bool live_shadows_ = false;
	bool live_single_pass_ = true;

	godot::Ref<godot::ShaderMaterial> material_;
	godot::Ref<godot::Shader> opaque_shader_, single_pass_shader_;
	godot::Ref<godot::ImageTexture> nodes_tex_, tape_tex_, edits_tex_, materials_tex_;
};

} // namespace sdf::godot_bind

VARIANT_ENUM_CAST(sdf::godot_bind::SdfBody::DebugView);
