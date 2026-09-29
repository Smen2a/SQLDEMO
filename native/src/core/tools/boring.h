#pragma once

#include "tools/cutting.h"
#include "tools/layout.h"
#include "tools/tools.h"

#include <memory>

// Boring: a brace and bit. The bit is an auger bit: a lead screw at its point, two spurs that
// score the hole's rim, two cutters that lift the waste, and a twist that carries it up and
// out. Turned clockwise, the screw draws the bit in a set distance each turn, whatever the
// wood (a harder one only turns harder); turned back, it stays (the brace's ratchet), so a
// crank worked back and forth goes on in. The hole is a
// round cylinder from the surface to the depth the cutters have reached, the screw's point
// a little ahead of them. Millimetres, body space.
namespace sdf::tools {

// An auger bit.
struct Bit {
	float diameter = 12.0f;
	float pitch = 1.6f;    // mm the lead screw draws it in each turn (a medium screw: 16 turns to the inch)
	float screw = 4.0f;    // mm the screw's point runs ahead of the cutters
	float length = 120.0f; // mm from its cutters to the chuck

	float radius() const { return 0.5f * diameter; }
	// The hole from `from` to `to` below `contact` along -normal (from < 0: from above the
	// work), the cutters' flat floor at `to`.
	Edit hole(vec3 contact, vec3 normal, float from, float to) const;
	// The screw's hole ahead of the cutters at `depth`.
	Edit screw_hole(vec3 contact, vec3 normal, float depth) const;
};

// A brace: a steel crank whose grip goes round a circle `sweep` mm across, a chuck holding
// the bit, and a round head for the other hand to bear on. The model stands on the bit's
// cutters (the origin), up along +z, its grip out along +x.
struct Brace {
	Bit bit;
	float sweep = 200.0f;

	Body model() const;
};

// Where a bit's centre goes to keep its hole within marked lines (tools/layout.h): moved in
// from any wall (a side or an end) nearer to it than the bit's radius; between two facing
// walls closer together than the bit is wide, halfway between them. Only walls standing
// across the face (their normals across `normal`) hold it.
vec3 hold_bit(vec3 centre, vec3 normal, float radius, const Limits &limits);

// How deep below `contact` (along -normal) the work goes under a bit `radius` wide there:
// its deepest wood, probed every half millimetre (0 with none).
float depth_through(const Work &work, vec3 contact, vec3 normal, float radius);

// A brace set on the work at `contact`, boring in along -normal, its bit held to the marked
// lines of `limits` (hold_bit()). move_to() points go round the bit's axis: the grip's angle
// about it (at most half a turn from one to the next; a point on the axis is ignored). Each
// turn clockwise, seen from above (from +normal), draws the bit in by its pitch times the
// pace, through the work and a millimetre beyond (depth_through(), read when it is set) or
// down to the floors of `limits` (a gauge line's depth).
std::unique_ptr<Stroke> boring_stroke(const Brace &brace, const Work &work, vec3 contact, vec3 normal, vec3 along,
		float pace = 1.0f, const Limits &limits = {});

} // namespace sdf::tools
