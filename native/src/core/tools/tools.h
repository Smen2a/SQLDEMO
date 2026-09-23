#pragma once

#include "body/body.h"

#include <memory>
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

// An orthonormal frame: z = normal, x = `along` made perpendicular to it, y = z x x.
struct Frame {
	vec3 origin, x, y, z;

	static Frame at(vec3 origin, vec3 normal, vec3 along);
	vec3 point(vec3 local) const { return origin + x * local.x + y * local.y + z * local.z; }
	vec3 direction(vec3 local) const { return x * local.x + y * local.y + z * local.z; }
	// Quaternion taking the unit axes to x, y, z (for Primitive rotations).
	vec4 rotation() const;
};

// A bench chisel pared along the surface: bevel down, the handle raised behind the edge.
struct Chisel {
	float width = 12.0f;
	float thickness = 3.5f;
	float bevel_deg = 25.0f;
	float approach_deg = 20.0f; // angle between the blade and the work surface

	Body model() const;
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

	Body model() const;
	// Depth removed per millimetre of travel (coarser grits cut faster).
	float removal_per_mm() const;
	// Takes `depth` off below the plane over the rectangle [lo, hi] (plane x / y
	// coordinates) that the block's face covered, feathering out beyond it.
	Edit pass(const Frame &plane, vec2 lo, vec2 hi, float depth) const;
};

// What a stroke asks of the edit session after the tool moves.
struct StrokeUpdate {
	bool replace = false;    // replace the stroke in progress with `edits`, else append them
	std::vector<Edit> edits; // (replace with none: clear it)
	bool empty() const { return !replace && edits.empty(); }
};

// A tool in use, from engaging it to lifting it off: it turns the tool's motion into edits.
// Cuts that only ever grow (a chisel pushing on, a saw going deeper) are appended piece by
// piece, so each update touches only the new part; a sanding pass changes throughout, so
// it replaces the stroke.
class Stroke {
public:
	virtual ~Stroke() = default;
	// The tool moved to `point` on the work plane (body space).
	virtual StrokeUpdate move_to(vec3 point) = 0;
	// Edits ending the stroke, appended before it is committed.
	virtual std::vector<Edit> finish() { return {}; }
	// Where the tool's model is now: its frame, as the models are built.
	virtual Frame pose() const = 0;
};

// A chisel set on the surface at `contact`, facing `facing` until the push shows its
// direction (after 2 mm), cutting `depth` deep. It cannot un-cut: pulling back does nothing.
std::unique_ptr<Stroke> chisel_stroke(const Chisel &chisel, vec3 contact, vec3 normal, vec3 facing, float depth);
// A saw set on the surface at `contact` along `along`: each millimetre of blade travel,
// either way, deepens the kerf by `feed`.
std::unique_ptr<Stroke> saw_stroke(const Saw &saw, vec3 contact, vec3 normal, vec3 along, float feed);
// A sanding block pressed flat at `contact` (its length along `along`), rubbed about the
// plane there: it takes removal_per_mm() off per millimetre travelled, over what it covered.
std::unique_ptr<Stroke> sanding_stroke(const SandingBlock &block, vec3 contact, vec3 normal, vec3 along);

} // namespace sdf::tools
