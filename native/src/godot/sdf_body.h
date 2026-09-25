#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "edit/session.h"
#include "eval/reference_renderer.h"
#include "godot/gpu_sampler.h"
#include "pieces/pieces.h"
#include "tools/cutting.h"
#include "tools/debris.h"
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
#include <optional>
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
	// `normal` there and the tool facing `along`. tool:
	//   "chisel", "gouge"  settings: variant (core tools/catalog.h: "bench_12", "gouge_7_12"...),
	//                      depth (mm, at most), angle (degrees between blade and work; 60 or
	//                      more chops), skew (degrees the hand turns the edge), length (mm of
	//                      path), seed. The stroke makes the plan the cutting model works out
	//                      against this body (core tools/cutting.h): plan_stroke()'s, if it
	//                      was this one.
	//   "spokeshave"       depth (mm below its sole), length, seed: planned like a chisel
	//   "rasp"             variant ("rasp_cabinet"...), pressure, tilt (degrees about its line),
	//                      length: worked back and forth along its line
	//   "scraper"          pressure, length: the same
	//   "saw"              feed: mm deeper per mm of stroke
	//   "sanding_block", "sanding_sponge"  grit, pressure
	// The sponge's work (a smoothing layer, see core tools/smoothing.h) is done on the worker
	// thread too, and applied as it goes: it has no preview. Takes the place of any plan;
	// get_plan() then reports what a chisel's, gouge's or spokeshave's stroke comes to
	// (planned now if plan_stroke() did not plan it), until clear_plan().
	bool begin_stroke(const godot::String &tool, const godot::Vector3 &contact, const godot::Vector3 &normal,
			const godot::Vector3 &along, const godot::Dictionary &settings);
	// Plans a stroke without making it: the tool engaged as begin_stroke() would, then moved
	// `length` mm along `along` (the saw forth and back: one stroke of its blade; the chisel
	// pushed that far and lifted out; the block rubbed that far). The cut it would make is
	// drawn by the shader, hatched in the plan tint, until clear_plan() or begin_stroke().
	// Returns {"edits": how many, "depth": mm it reaches, "length": mm}; the sponge's work
	// is a layer, which is not drawn (edits 0). A chisel's or gouge's plan (the cutting
	// model's) adds {"force", "available" (N), "grain" (0 along the fibres .. 1 severing
	// them), "slope" (+1 with the grain, -1 against), "warnings" (names: "skates", "shallow",
	// "tears out", ...), "chop", "blow" (mm this blow goes)}. It reads the body, so while an
	// edit is being applied it answers with the last plan ({..., "stale": true}) and plans
	// again once the edit lands (get_plan()).
	godot::Dictionary plan_stroke(const godot::String &tool, const godot::Vector3 &contact, const godot::Vector3 &normal,
			const godot::Vector3 &along, double length, const godot::Dictionary &settings);
	godot::Dictionary get_plan() const { return plan_report_; } // the current plan's report
	void clear_plan();
	// The tools with variants (core tools/catalog.h): [{id, family ("chisel", "gouge" or
	// "rasp"), label, width, ...}].
	static godot::Array tool_catalog();
	void set_plan_tint(const godot::Color &tint); // colour, and alpha: how strongly
	// How much of the body is drawn, 0 to 1 (a tool fading in and out): a dither.
	void set_opacity(double opacity);
	double get_opacity() const { return opacity_; }
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
	// What the stroke took off the work since the last call (core tools/debris.h), in world
	// space, for the game to show; empty when nothing came off. Gathered as a previewed
	// stroke moves (from the body as it was before the stroke), and when it ends:
	//   "shaving": {"points" (mid-thickness), "thickness", "width" (world lengths), "volume"
	//     (mm^3 each), "starts" (1 where a new piece begins: it broke, or after a gap),
	//     "colours"}, sampled every "step" (world length) along the cut, oldest first
	//   "chips": [{"transform" (its centre; x along the cut, z out of the face; unscaled),
	//     "size" (world), "volume" (mm^3), "colour"}]
	//   "ended": the stroke is over (the shaving so far comes away); "cancelled": it was
	//     taken back (and what came off with it)
	//   "rise": degrees the blade stands at (the shaving rides up it), "density" (g/cm^3)
	godot::Dictionary take_debris();
	// The body's colour at `point` (world space): its material there, grain and all. Also
	// inside material a stroke is cutting away (until the stroke lands). Transparent while an
	// edit is being applied to the body.
	godot::Color albedo_at(const godot::Vector3 &point) const;
	// Waits for every queued edit to be applied and uploaded (for tests).
	void flush();
	// Whether the creases edits leave in coarse ADF cells (so that they land in a few
	// milliseconds) are refined on the worker once the body is idle: no tool engaged and
	// nothing queued. Refining changes no surface, only makes it cheaper to draw.
	void set_refine_when_idle(bool enabled) { refine_when_idle_ = enabled; }
	bool get_refine_when_idle() const { return refine_when_idle_; }
	// Whether ADF bricks are sampled on the GPU (a compute shader, see gpu_sampler.h) where
	// the renderer has a RenderingDevice (Forward+, Mobile); the CPU samples them otherwise.
	void set_gpu_bricks(bool enabled);
	bool get_gpu_bricks() const { return gpu_bricks_; }
	// Pieces. When a stroke cuts clean through the body (a saw through a board), the body
	// checks on its worker whether it now lies in two parts and, if so, measures both (their
	// volumes, centres of mass and hull points) and emits separated(point, normal) with the
	// cut's middle plane (world space), turned so that the smaller part lies in front.
	// split() then makes the part in front of the plane (+normal) a body of its own and
	// keeps the part behind: at once, as both draw the same textures with the other side cut
	// away by the shader, while each body takes its half-space into its edit list on its
	// worker (an undo step, previewed until it lands). Both stay editable. Given the plane
	// separated() reported (either way round), split() takes the worker's measures and
	// reads nothing else from the body; given any other, it measures there and then.
	// Any other cut may leave an island no plane separates (a corner cut off by two saw
	// cuts meeting, a chip chiselled free): once the body is idle after a commit, the worker
	// looks round the cut (pieces/parts.h), cuts the island out with a region proved apart
	// and measures both sides, and separated(point, zero) reports it, with the island's
	// centre. split(point, zero) then makes the island a body of its own: each side takes
	// its region into its edit list (an undo step), and the island's body stays hidden until
	// its own lands (the shader cannot draw a region before), while this one shows the island
	// until then.
	SdfBody *split(const godot::Vector3 &point, const godot::Vector3 &normal);
	// Undoes a split's half-space here (the other piece is the caller's to free), leaving
	// nothing to redo.
	void rejoin();
	// Points on this body's surface (body space, mm) for a convex physics hull: at most
	// 256 extremes, on this piece's side of its last split.
	godot::PackedVector3Array get_hull_points();
	// Its material as convex pieces for physics, where one hull would fill a hollow (a board
	// that pieces came out of): its bounds cut into boxes by the faces of `holes` (AABBs, body
	// space, mm: where the pieces were, left out), each box's piece the points of the surface
	// in it and of its faces in the material (hull points, body space). After any queued
	// edits land. No piece reaches into a hole, whatever its hull, as each stays in its box.
	godot::Array get_collision_hulls(const godot::Array &holes);
	// The body's mass (kg), from its volume and its base material's density, and its centre
	// of mass (body space, mm).
	double get_mass();
	godot::Vector3 get_centre_of_mass();
	double get_volume(); // mm^3

	// For tests: the ADF against one built afresh on the CPU for the same body, at up to
	// `points` random points within 0.2 mm of the surface where both hold bricks: {sampler,
	// compared, max_difference, max_difference_exact (from the exact field), ...}.
	godot::Dictionary compare_bricks_with_cpu(int points);

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
		enum Kind { STROKE, WORK, COMMIT, CANCEL, UNDO, REDO, REFINE, DROP, CHECK } kind;
		std::size_t drop = 0;    // STROKE: drop the stroke's last `drop` edits,
		std::vector<Edit> edits; // then append these
		bool previewed = false;  // COMMIT: of a previewed stroke (the oldest in committing_)
		std::shared_ptr<tools::Stroke> stroke = nullptr; // WORK: a deferred stroke's recorded motion to work
		std::optional<Separation> separation = std::nullopt; // COMMIT: where the stroke cut clean through
		bool clip = false; // a split's half-space (which leaves the piece's measures standing)
		sdf::Aabb region = {}; // CHECK: where to look for an island
	};

	void rebuild();                                // proxy mesh, textures and stats for a new body
	// A tool's stroke engaged at p (body space), or null (with an error) for an unknown tool.
	std::unique_ptr<tools::Stroke> make_stroke(const godot::String &tool, vec3 p, vec3 n, vec3 a,
			const godot::Dictionary &settings) const;
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
	std::vector<Edit> planned_;             // a planned stroke's cut (drawn hatched), if any
	// The stroke being planned (plan_stroke's arguments), its chisel or gouge plan, and the
	// report; stale when it was asked for while an edit was being applied.
	struct PlanRequest {
		godot::String tool;
		godot::Vector3 contact, normal, along;
		double length = 0.0;
		godot::Dictionary settings;
	};
	std::optional<PlanRequest> plan_request_;
	std::optional<tools::CutPlan> cut_plan_;
	godot::Dictionary plan_report_;
	bool plan_stale_ = false;
	godot::Dictionary compute_plan();
	// A chisel's, gouge's or spokeshave's plan against `work`, from its settings.
	static tools::CutPlan plan_for(const godot::String &tool, const tools::Work &work, vec3 p, vec3 n, vec3 a,
			float length, const godot::Dictionary &settings);
	// What came off since take_debris(), with a colour per shaving sample and chip.
	tools::Debris debris_;
	std::vector<vec3> shaving_colours_, chip_colours_;
	bool debris_cancelled_ = false;
	float stroke_rise_ = 0.0f; // the stroke's blade angle (a chisel's approach, a spokeshave's bed)
	void gather_debris(bool ended);
	vec3 albedo_body(vec3 p) const; // body space; the body must stand
	double opacity_ = 1.0;
	bool stroke_preview_ = true;
	bool previewing_ = false;               // whether stroke_ is previewed
	bool refine_when_idle_ = true;
	bool gpu_bricks_ = true;
	std::shared_ptr<GpuSampler> gpu_; // while the session samples on it
	std::optional<PieceSides> job_separated_; // the batch found the body in two parts, measured
	double separation_ms_ = 0, split_ms_ = 0;
	// Where committed cuts were since the body was last looked at for islands (CHECK, once it
	// is idle), and where the running batch's were.
	sdf::Aabb check_region_, job_check_;
	double island_ms_ = 0;              // the last CHECK's
	godot::String island_failed_;       // why its island could not be cut out, if it could not
	const char *job_island_failed_ = nullptr;
	bool reveal_ = false;               // hidden until the batch taking in its region lands
	// The parts separated() last reported, for split(), until the body changes. Refining
	// waits a frame after the signal, so that split() (deferred to the frame's end) finds
	// the worker idle and the session free to copy.
	std::optional<PieceSides> sides_;
	bool hold_refine_ = false;
	// This piece's side of each split it came out of (undone by rejoin()): a half-space, or a
	// side of a region; and its bounds before each.
	struct Clip {
		Plane plane;
		std::shared_ptr<const Region> region;
		std::uint8_t side = 0;
	};
	std::vector<Clip> clips_;
	std::vector<sdf::Aabb> unclipped_bounds_;
	// get_hull_points() and get_mass(), once computed (dropped when an edit other than a
	// split's half-space lands): split() sets them, so that nothing reads the session while
	// its batches run.
	std::vector<vec3> hull_;
	double mass_ = -1.0; // kg
	double volume_ = 0.0; // mm^3, with mass_
	vec3 centre_{0.0f};  // of mass, with mass_
	void clip(const Plane &plane);
	const Plane *last_plane() const; // the half-space of the last split, if it was one
	void report_separation();        // emits separated() for what the batch found
	// Whether nothing queued or running changes the body (as refining and looking for islands
	// do not), so that it can be read on the main thread; settle_body() waits until it is.
	bool body_settled() const;
	void settle_body();
	void clip(std::shared_ptr<const Region> region, std::uint8_t side, const sdf::Aabb &bounds);
	void set_proxy();
	// Held by every piece drawing this body's textures (they share them after a split, until
	// one uploads): the first of them to upload makes new ones, leaving the rest in place.
	std::shared_ptr<char> textures_ = std::make_shared<char>();
	void unshare_textures();
	double job_gpu_ms_ = 0;
	std::size_t job_gpu_jobs_ = 0, gpu_jobs_total_ = 0;
	void collect_gpu_stats(); // the GPU sampler's work since the last call (shared by bodies)
	bool job_refines_ = false;              // the running batch only refines (the body is unchanged)
	bool job_clips_ = false;                // the running batch only takes in split half-spaces (or regions)
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
