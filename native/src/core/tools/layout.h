#pragma once

#include "tools/tools.h"

#include <vector>

// Laying out: lines marked on the work before it is cut, for the tools to go to. A marking
// gauge scribes a line parallel to an edge, its fence riding the face beside it; a marking
// knife, drawn along a square, scribes one across. Either line is a real cut, a knife's
// V a few tenths of a millimetre deep: the chisel's edge drops into it, and it severs the
// fibres there. Millimetres, body space.
namespace sdf::tools {

// A marking gauge set `distance` mm: its pin at the origin (the line it scribes along x,
// its point just below z = 0), the beam across the work along +y to the fence, whose face
// at y = distance rides the reference face beside the edge.
struct MarkingGauge {
	float distance = 6.0f;

	Body model() const;
};

// A marking knife: its point at the origin, the blade standing up (z) along the line (x),
// its flat side at y = 0 against the square, the handle above.
struct MarkingKnife {
	Body model() const;
};

// A pencil: its point at the origin, standing up (z), a round cedar body sharpened to a
// graphite lead.
struct Pencil {
	Body model() const;
};

// A sheet of a plan (paper, A5): lying flat, its near corner at the origin, along x and y.
struct Sheet {
	Body model() const;
};

struct CutPlan; // tools/cutting.h

// A plane a tool stops at, from a marked line: at(p) > 0 on the waste side (what may be
// cut), < 0 on the side to keep.
struct Stop {
	vec3 normal{0.0f, 0.0f, 1.0f}; // unit, towards the waste
	float offset = 0.0f;

	float at(vec3 p) const { return gl::dot(normal, p) + offset; }
	// The plane through `point`, its waste towards `waste` (made unit).
	static Stop through(vec3 point, vec3 waste);
};

// What marked lines hold a stroke to (body space): floors it cuts no deeper than (a rebate's,
// gauged on the face beside it; a chamfer's plane, through lines gauged on both faces), sides
// it keeps within (a shoulder), ends it stops at (a knife line across its way).
struct Limits {
	std::vector<Stop> floors, sides, ends;

	bool empty() const { return floors.empty() && sides.empty() && ends.empty(); }
	// How deep below `p` (along -normal) the floors let a cut go, mm (+inf with none; less than
	// zero where `p` is already below one).
	float depth_below(vec3 p, vec3 normal) const;
	// How far from `p` along `dir` the first end is, mm (+inf with none ahead).
	float reach(vec3 p, vec3 dir) const;
};

// Holds a planned cut (a chisel's, gouge's or spokeshave's: tools/cutting.h) to marked lines:
// its floor no deeper than the floors under its edge (its middle and corners), and, where it
// reaches an end, ended there, square: the knife line has severed the fibres, so the chip
// breaks off at it and the edge is not lifted out beyond. Says whether any held it (then its
// stop says kAtLine).
bool limit_plan(CutPlan &plan, const Limits &limits);

// How deep a scribed line is cut, mm.
constexpr float kScribeDepth = 0.3f;

// The edge that scribes a line: a narrow V (40 degrees included), stood steeply on the work
// so it goes straight in: a gauge's cutter or a knife's point, drawn along the line.
Chisel scribe_edge();

} // namespace sdf::tools
