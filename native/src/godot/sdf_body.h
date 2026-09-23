#pragma once

#include "adf/adf.h"
#include "body/body.h"
#include "body/materials.h"
#include "compile/octree.h"
#include "eval/reference_renderer.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace sdf::godot_bind {

// A carvable part rendered live: the body's octree and its adaptive distance field (ADF)
// are flattened into data textures and a proxy box raymarches them with the shared SDF
// code (game/shaders/sdf/sdf_live.gdshaderinc).
// The node's local space is the body's space, in millimetres; scale the node (0.001 for a
// metre-scaled world) to place it.
class SdfBody : public godot::MeshInstance3D {
	GDCLASS(SdfBody, godot::MeshInstance3D)

public:
	enum DebugView { SHADED = 0, NORMALS = 1, STEPS = 2, ALBEDO = 3 };
	// What the Live raymarch reads: the ADF (sampled bricks, exact cells at creases; cost
	// independent of edit count) or the octree tapes alone (exact everywhere, cost grows
	// with tape length). EXACT is the reference for checking ADF.
	enum LiveSource { LIVE_ADF = 0, LIVE_EXACT = 1 };

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
	void set_live_source(int source);
	int get_live_source() const { return live_source_; }
	godot::Dictionary get_stats() const;
	godot::AABB get_body_bounds() const;

protected:
	static void _bind_methods();

private:
	void rebuild();          // octree + ADF + proxy mesh + textures
	void upload_textures();  // flatten octree, edits and materials into data textures
	// ADF nodes always; bricks and material bricks in full, or only the layers holding
	// the given slots.
	void upload_adf(bool full, const std::vector<std::uint32_t> &bricks, const std::vector<std::uint32_t> &materials);
	void update_shaders();
	void update_material();
	void apply_parameters(const godot::Ref<godot::ShaderMaterial> &material);

	sdf::Body body_;
	sdf::Octree octree_;
	sdf::Adf adf_;
	sdf::MaterialTable materials_ = sdf::MaterialTable::standard();
	sdf::Camera demo_camera_;
	int debug_view_ = SHADED;
	bool live_shadows_ = false;
	int live_source_ = LIVE_ADF;

	// The visible surface draws in one pass (sdf_live_single.gdshader), never through the
	// opaque pipeline: Forward+ redraws opaque materials after its depth prepass with an
	// exact-equality depth test, and a depth recomputed by a separately compiled variant
	// misses it on some pixels (speckle). Shadows come from an internal child that runs
	// the opaque shader in shadow passes only.
	godot::Ref<godot::ShaderMaterial> material_;
	godot::MeshInstance3D *shadow_caster_ = nullptr;
	godot::Ref<godot::ShaderMaterial> caster_material_;
	godot::Ref<godot::ImageTexture> nodes_tex_, tape_tex_, edits_tex_, materials_tex_, adf_nodes_tex_, adf_cells_tex_;
	godot::Ref<godot::Texture2DArray> bricks_tex_, brick_materials_tex_;
	double last_update_ms_ = 0;
};

} // namespace sdf::godot_bind

VARIANT_ENUM_CAST(sdf::godot_bind::SdfBody::DebugView);
VARIANT_ENUM_CAST(sdf::godot_bind::SdfBody::LiveSource);
