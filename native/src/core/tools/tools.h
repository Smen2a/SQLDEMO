#pragma once

#include "body/body.h"
#include "compile/octree.h"
#include "pieces/pieces.h"

#include <memory>
#include <optional>
#include <vector>

// Hand tools: each one's model (a Body, drawn like any part) and the cuts it makes, from
// the same dimensions. Everything is in millimetres.
//
// A tool model is built in its own frame: the origin is where it works (the chisel's edge
// centre, the saw's tooth line centre, the sanding block's face centre), +z points away
// from the work (along the surface normal) and +x is the working direction (the chisel's
// push, the saw's stroke, the block's long side).
//
// Cuts are built in body space from a contact on the workpiece: a surface point, its
// normal (out of the wood) and a direction along the surface.
namespace sdf::tools {

struct Debris; // tools/debris.h

// An orthonormal frame: z = normal, x = `along` made perpendicular to it, y = z x x.
struct Frame {
	vec3 origin, x, y, z;

	static Frame at(vec3 origin, vec3 normal, vec3 along);
	vec3 point(vec3 local) const { return origin + x * local.x + y * local.y + z * local.z; }
	vec3 direction(vec3 local) const { return x * local.x + y * local.y + z * local.z; }
	// Quaternion taking the unit axes to x, y, z (for Primitive rotations).
	vec4 rotation() const;
};

// A chisel or carving gouge, held bevel down with the handle raised behind the edge. Its
// edge's cross-section is flat (chisels), an arc (gouges, by sweep) or a V (V-tools). The
// catalog (tools/catalog.h) has the workshop's variants; tools/cutting.h what they can do.
struct Chisel {
	float width = 12.0f;
	float thickness = 3.5f;
	float bevel_deg = 25.0f;
	float approach_deg = 20.0f;  // angle between the blade and the work surface
	float blade_length = 85.0f;
	float skew_deg = 0.0f;       // a skew chisel's edge, angled across the blade
	float hand_force = 200.0f;   // N it can be pushed with (a long, flexible blade takes less)
	float mallet = 1.0f;         // its blows: 0 never struck, 1 a bench chisel's, 1.5 a mortise chisel's
	int kind = gl::SDF_TOOL_FLAT; // or SDF_TOOL_GOUGE, SDF_TOOL_V
	float sweep_radius = 0.0f;   // gouges: the arc's radius
	float v_angle_deg = 60.0f;   // V-tools: the included angle

	Body model() const;
	// The cross-section of what the edge removes, `height` above its deepest point.
	ToolProfile profile(float height) const;
	// How deep the edge goes before its corners enter the work: 0 for a flat edge (its
	// corners are in from the start), a gouge's sweep, a V-tool's wings.
	float corner_depth() const;
	// The area (mm^2) of the chip the edge takes `depth` deep (up to its corners; the
	// width beyond).
	float chip_area(float depth) const;
	// The cut of a push from `start` (a surface point) towards `end` (projected onto the
	// surface plane there) at `depth` below it: a ramp in at the approach angle, then a flat
	// run. A push shorter than the ramp is ramp only.
	std::vector<Edit> paring(vec3 start, vec3 end, vec3 normal, float depth) const;
	// Lifting the edge out forwards at the end of a push, so the cut does not stop at a wall.
	Edit lift_out(vec3 end, vec3 direction, vec3 normal, float depth) const;
	// Where the edge is after paring(start, end, ...): the flat run's end at depth.
	vec3 edge(vec3 start, vec3 end, vec3 normal, float depth) const;
};

// A back saw. Its kerf is a straight slot along the blade, as deep as the saw has gone.
struct Saw {
	float kerf = 0.8f;
	float plate = 0.6f;
	float blade_length = 250.0f;
	float blade_height = 60.0f;
	float tooth_pitch = 3.2f;

	Body model() const;
	Edit kerf_cut(vec3 centre, vec3 along, vec3 normal, float depth) const;
	// The part of a kerf between two depths (from 0: the whole slot up to the surface), so
	// sawing deeper only adds what is new.
	Edit kerf_slice(vec3 centre, vec3 along, vec3 normal, float from_depth, float to_depth) const;
};

// A cork sanding block with abrasive paper. A hard block flattens: it takes down what
// stands above its plane within its footprint first.
struct SandingBlock {
	float length = 70.0f; // along the working direction
	float breadth = 40.0f;
	int grit = 120;
	float feather = 2.0f; // blend radius: how far a pass's rim spreads, and edges round over
	float pressure = 1.0f; // how hard it is pressed (1: an ordinary hand's worth)

	Body model() const;
	// Depth removed per millimetre of travel (coarser grits and more pressure cut faster).
	float removal_per_mm() const;
	// Takes `depth` off below the plane over the rectangle [lo, hi] (plane x / y
	// coordinates) that the block's face covered, feathering out beyond it.
	Edit pass(const Frame &plane, vec2 lo, vec2 hi, float depth) const;
};

// A sanding sponge: a block of foam coated in abrasive, rubbed by hand. Unlike the sanding
// block it is soft: it wraps over edges and into shallow hollows, so rather than flattening
// it smooths, rounding arrises over and softening ridges left by other tools. Its work is a
// smoothing layer (tools/smoothing.h, body/layer.h) built by curvature flow, not cuts.
struct SandingSponge {
	float length = 100.0f;
	float breadth = 68.0f;
	float thickness = 25.0f;
	int grit = 120;
	float reach = 10.0f; // how far round the point it is pressed at it bears on the work
	float pressure = 1.0f; // how hard it is pressed (1: an ordinary hand's worth)

	Body model() const;
	// Curvature flow (mm^2) per millimetre of travel: coarser grits and more pressure cut faster.
	float rate() const;
};

// What a stroke asks of the edit session after the tool moves: drop its last `drop` edits,
// then append `edits`. With `changed` set (and as many edits as it drops), the new edits
// replace the old ones and differ from them only there.
struct StrokeUpdate {
	std::size_t drop = 0;
	std::vector<Edit> edits;
	Aabb changed;
	bool empty() const { return drop == 0 && edits.empty(); }
};

// A tool in use, from engaging it to lifting it off: it turns the tool's motion into edits,
// in two forms.
// - Updates (move_to), for applying the stroke to a body as it goes: cuts that only ever
//   grow (a chisel pushing on, a saw going deeper) keep their newest piece open, re-cutting
//   just that as it grows and freezing it at a set size, so an update touches only the
//   part that moved. A sanding pass changes throughout, so it replaces itself.
// - The whole cut so far in as few edits as it takes (edits()): the chisel's ramp and one
//   flat run, the saw's one kerf, the block's one pass. This is what a preview draws while
//   the tool moves (the Live shader's overlay) and what is committed when it lifts off:
//   fewer, longer edits, applied once.
// Either way every edit is a cut (Op::Subtract): a stroke only takes material away.
//
// A *deferred* stroke's updates take real work (the sanding sponge runs a flow over a grid):
// move_to() only records the motion, and work() turns it into an update, possibly on
// another thread than move_to() and pose() run on. Its work is a layer, not cuts, and it
// has no merged form to preview.
class Stroke {
public:
	virtual ~Stroke() = default;
	// The tool moved to `point` on the work plane (body space).
	virtual StrokeUpdate move_to(vec3 point) = 0;
	// The cut so far, merged (see above).
	virtual std::vector<Edit> edits() const = 0;
	// Edits ending the stroke, appended before it is committed.
	virtual std::vector<Edit> finish() { return {}; }
	// Where the tool's model is now: its frame, as the models are built.
	virtual Frame pose() const = 0;

	// Where the stroke has cut clean through the work, if it has (the middle of a saw's
	// kerf, and its width, once the saw is through): the body may now lie in two pieces,
	// one each side (see pieces/pieces.h: plane_clear).
	virtual std::optional<Separation> separation() const { return std::nullopt; }

	// What came off the work since the last call (tools/debris.h), appended to `out`: read
	// from `body` as it was before this stroke (a previewed stroke is only applied when it
	// is committed). Called after move_to(), and with `ended` once it is lifted off (after
	// finish()). A deferred stroke reports what its own work measured and reads nothing (its
	// work may be running on another thread). Nothing by default.
	virtual void debris(const Body &body, const Octree &octree, Debris &out, bool ended = false) {
		(void)body;
		(void)octree;
		(void)out;
		(void)ended;
	}

	virtual bool deferred() const { return false; }
	// The motion recorded since the last call, as an update for the edit session holding
	// `body` (with this stroke's earlier updates applied; the first call sees the body as it
	// was before the stroke).
	virtual StrokeUpdate work(const Body &body, const Octree &octree) {
		(void)body;
		(void)octree;
		return {};
	}
};

// A chisel set on the surface at `contact`, facing `facing` until the push shows its
// direction (after 2 mm), cutting `depth` deep. It cannot un-cut: pulling back does nothing.
std::unique_ptr<Stroke> chisel_stroke(const Chisel &chisel, vec3 contact, vec3 normal, vec3 facing, float depth);
// A saw set on the surface at `contact` along `along`: each millimetre of blade travel,
// either way, deepens the kerf by `feed`, down to `max_depth` (through the work).
std::unique_ptr<Stroke> saw_stroke(const Saw &saw, vec3 contact, vec3 normal, vec3 along, float feed,
		float max_depth = 1e9f);
// A sanding block pressed flat at `contact` (its length along `along`), rubbed about the
// plane there: it takes removal_per_mm() off per millimetre travelled, over what it covered.
std::unique_ptr<Stroke> sanding_stroke(const SandingBlock &block, vec3 contact, vec3 normal, vec3 along);
// A sanding sponge pressed at `contact` and rubbed about the plane there (deferred): the
// work smooths whatever lies within its reach of the path, in proportion to the travel.
std::unique_ptr<Stroke> hand_sanding_stroke(const SandingSponge &sponge, vec3 contact, vec3 normal, vec3 along,
		float spacing = 0.25f);

} // namespace sdf::tools
