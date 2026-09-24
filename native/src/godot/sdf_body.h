#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "edit/session.h"
#include "eval/reference_renderer.h"
#include "tools/tools.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_vector4_array.hpp>

#include <deque>
#include <future>
#include <memory>
#include <vector>

namespace sdf::godot_bind {

// A carvable part rendered live: the body's octree and its adaptive distance field (ADF)
// are flattened into data textures and a proxy box raymarches them with the shared SDF
// code (game/shaders/sdf/sdf_live.gdshaderinc).
// The node's local space is the body's space, in millimetres; scale the node (0.001 for a
// metre-scaled world) to place it.
//
// Live editing: a tool stroke (begin_stroke / move_stroke / end_stroke) turns the tool's
// motion into edits. With stroke_preview on (the default) the shader draws them on top of
// the body while the tool moves (sdf_live.gdshaderinc's overlay), which costs no CPU work,
// and the stroke is applied once, merged, when the tool lifts off. Otherwise each move's
// edits are applied as they come. Either way the body applies edits on a worker thread,
// one batch at a time, uploading when each batch is done (signal `edited`); commands that
// arrive while a batch runs are folded together, so a slow update never builds a backlog.
// A previewed stroke stays in the overlay until the upload that carries it.
class SdfBody : public godot::MeshInstance3D {
	GDCLASS(SdfBody, godot::MeshInstance3D)

public:
	enum DebugView { SHADED = 0, NORMALS = 1, STEPS = 2, ALBEDO = 3 };
	// What the Live raymarch reads: the ADF (sampled bricks, exact cells at creases; cost
	// independent of edit count) or the octree tapes alone (exact everywhere, cost grows
	// with tape length). EXACT is the reference for checking ADF, and what tools use.
	enum LiveSource { LIVE_ADF = 0, LIVE_EXACT = 1 };

	SdfBody();
	~SdfBody() override;

	// Builds a demo body by name (see sdf::demo::named_demo): "carved_panel", "blend_<i>",
	// "material_<i>", "sphere", "session", "board", "board_oak", "board_walnut", "sanded".
	bool load_demo(const godot::String &name);
	// Builds a tool's model: "chisel" (settings: width), "saw", "sanding_block" (grit) or
	// "sanding_sponge" (grit). Tools are drawn from their exact tapes (live_source EXACT) and
	// cast shadows.
	bool load_tool(const godot::String &name, const godot::Dictionary &settings);
	// The camera that frames the last demo, in body space: {eye, target, up, fov}.
	godot::Dictionary get_demo_camera() const;
	// Carves `count` random strokes into the body's top face as one undo step, at once.
	void add_random_strokes(int count, int seed);

	// Where a world-space ray first meets the surface: {position, normal, distance} in world
	// space, or an empty dictionary. While an edit is being applied it answers from the last
	// query instead ({..., "stale": true}).
	godot::Dictionary raycast(const godot::Vector3 &from, const godot::Vector3 &direction, double max_distance);

	// Engages a tool at `contact` (world space, on this body's surface) with the surface
	// `normal` there and the tool facing `along`. tool: "chisel" (settings: width, depth mm),
	// "saw" (feed: mm deeper per mm of stroke), "sanding_block" (grit) or "sanding_sponge"
	// (grit). The sponge's work (a smoothing layer, see core tools/smoothing.h) is done on
	// the worker thread too, and applied as it goes: it has no preview.
	bool begin_stroke(const godot::String &tool, const godot::Vector3 &contact, const godot::Vector3 &normal,
			const godot::Vector3 &along, const godot::Dictionary &settings);
	// The tool moved to `point` (world space, on the plane it was engaged on).
	void move_stroke(const godot::Vector3 &point);
	void end_stroke();    // finishes the cut and makes it one undo step
	void cancel_stroke(); // takes the stroke's cut back
	bool is_stroking() const { return stroke_ != nullptr; }
	// Where the engaged tool's model goes (its world transform), as the stroke moves it.
	godot::Transform3D get_tool_pose() const;
	// A tool model's world transform standing `lift` mm off the surface at `contact`.
	godot::Transform3D pose_at(const godot::Vector3 &contact, const godot::Vector3 &normal, const godot::Vector3 &along,
			double lift) const;
	void undo();
	void redo();
	bool can_undo() const { return stats_.get("can_undo", false); }
	bool can_redo() const { return stats_.get("can_redo", false); }
	// Whether edits are still being applied (a batch running or queued).
	bool is_busy() const { return job_.valid() || !queue_.empty(); }
	// Whether strokes are drawn by the shader while the tool moves and applied when it lifts
	// off (on), or applied as the tool moves (off). Takes effect from the next stroke.
	void set_stroke_preview(bool enabled) { stroke_preview_ = enabled; }
	bool get_stroke_preview() const { return stroke_preview_; }
	// Waits for every queued edit to be applied and uploaded (for tests).
	void flush();
	// Whether the creases edits leave in coarse ADF cells (so that they land in a few
	// milliseconds) are refined on the worker once the body is idle: no tool engaged and
	// nothing queued. Refining changes no surface, only makes it cheaper to draw.
	void set_refine_when_idle(bool enabled) { refine_when_idle_ = enabled; }
	bool get_refine_when_idle() const { return refine_when_idle_; }

	void set_debug_view(int view);
	int get_debug_view() const { return debug_view_; }
	// Whether the Live body casts shadow-map shadows. That runs the raymarch again in every
	// shadow pass (each directional split), which can cost more than drawing the body, so
	// it is off by default for workpieces; the baked mesh will cast them instead.
	void set_live_shadows(bool enabled);
	bool get_live_shadows() const { return live_shadows_; }
	void set_live_source(int source);
	int get_live_source() const { return live_source_; }
	// Off: the ADF's exact cells (creases) march and shade on their bricks instead of their
	// tapes, which rounds creases and uses the base material there. For measuring what
	// exact cells cost; always on otherwise.
	void set_exact_cells(bool enabled);
	bool get_exact_cells() const { return exact_cells_; }
	godot::Dictionary get_stats() const { return stats_; }
	godot::AABB get_body_bounds() const;

	void _process(double delta) override;

protected:
	static void _bind_methods();

private:
	// An edit-session command, queued on the main thread and applied on a worker.
	struct Command {
		enum Kind { STROKE, WORK, COMMIT, CANCEL, UNDO, REDO, REFINE } kind;
		std::size_t drop = 0;    // STROKE: drop the stroke's last `drop` edits,
		std::vector<Edit> edits; // then append these
		bool previewed = false;  // COMMIT: of a previewed stroke (the oldest in committing_)
		std::shared_ptr<tools::Stroke> stroke = nullptr; // WORK: a deferred stroke's recorded motion to work
	};

	void rebuild();                                // proxy mesh, textures and stats for a new body
	void queue(Command c);
	void queue_all(std::vector<Command> commands); // in one batch
	// The overlay: previewed strokes not yet uploaded, then the stroke in progress (if
	// previewed), packed for the shader.
	void update_overlay();
	void apply_overlay(const godot::Ref<godot::ShaderMaterial> &material) const;
	void start_job();                              // applies the queue on a worker
	void finish_job();                             // waits for the worker, then uploads
	void upload_all();
	void upload_textures();                        // edits, tapes, materials (and the octree, for EXACT)
	// ADF nodes and grid always; bricks and material bricks in full, or only the layers
	// holding the given slots.
	void upload_adf(bool full, const std::vector<std::uint32_t> &bricks, const std::vector<std::uint32_t> &materials);
	void update_shaders();
	void update_material();
	void apply_parameters(const godot::Ref<godot::ShaderMaterial> &material);
	void refresh_stats();
	// Body space (mm) to world and back, through the node's transform.
	vec3 to_body(const godot::Vector3 &world) const;
	vec3 to_body_direction(const godot::Vector3 &world) const;
	godot::Transform3D frame_to_world(const tools::Frame &f) const;

	EditSession session_;
	sdf::MaterialTable materials_ = sdf::MaterialTable::standard();
	sdf::Camera demo_camera_;
	int debug_view_ = SHADED;
	bool live_shadows_ = false;
	int live_source_ = LIVE_ADF;
	bool exact_cells_ = true;

	std::shared_ptr<tools::Stroke> stroke_; // the tool in use, if any (queued work may share it)
	bool stroke_preview_ = true;
	bool previewing_ = false;               // whether stroke_ is previewed
	bool refine_when_idle_ = true;
	bool job_refines_ = false;              // the running batch only refines (the body is unchanged)
	std::vector<Command> queue_;            // not yet applied
	std::future<void> job_;                 // the batch being applied, if any
	std::vector<std::uint32_t> job_bricks_, job_materials_; // slots the batch rewrote
	std::deque<std::vector<Edit>> committing_; // previewed strokes queued or being applied, oldest first
	std::size_t job_previews_ = 0;             // how many of them the batch applies
	godot::PackedVector4Array overlay_;        // what the shader's overlay uniforms hold
	int overlay_count_ = 0;
	sdf::Aabb overlay_box_;
	float overlay_lipschitz_ = 1.0f;
	double job_ms_ = 0, upload_ms_ = 0, refine_ms_ = 0;
	godot::Dictionary stats_;
	godot::Dictionary last_hit_;
	sdf::Aabb bounds_; // the body's when loaded (edits only cut)

	// The visible surface draws in one pass (sdf_live_single.gdshader), never through the
	// opaque pipeline: Forward+ redraws opaque materials after its depth prepass with an
	// exact-equality depth test, and a depth recomputed by a separately compiled variant
	// misses it on some pixels (speckle). Shadows come from an internal child that runs
	// the opaque shader in shadow passes only.
	godot::Ref<godot::ShaderMaterial> material_;
	godot::MeshInstance3D *shadow_caster_ = nullptr;
	godot::Ref<godot::ShaderMaterial> caster_material_;
	godot::Ref<godot::ImageTexture> nodes_tex_, tape_tex_, edits_tex_, materials_tex_, adf_nodes_tex_, adf_cells_tex_,
			adf_grid_tex_;
	godot::Ref<godot::Texture2DArray> bricks_tex_, brick_materials_tex_;
	double last_update_ms_ = 0;
};

} // namespace sdf::godot_bind

VARIANT_ENUM_CAST(sdf::godot_bind::SdfBody::DebugView);
VARIANT_ENUM_CAST(sdf::godot_bind::SdfBody::LiveSource);
