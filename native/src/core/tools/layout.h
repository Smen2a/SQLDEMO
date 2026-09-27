#pragma once

#include "tools/tools.h"

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

// How deep a scribed line is cut, mm.
constexpr float kScribeDepth = 0.3f;

// The edge that scribes a line: a narrow V (40 degrees included), stood steeply on the work
// so it goes straight in: a gauge's cutter or a knife's point, drawn along the line.
Chisel scribe_edge();

} // namespace sdf::tools
