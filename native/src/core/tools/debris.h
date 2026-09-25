#pragma once

#include "body/body.h"
#include "compile/octree.h"
#include "tools/tools.h"

#include <vector>

// What a stroke takes off the work, for the game to show: shavings, chips and dust. It is
// never part of a body again (the stroke's edits are what change the body); this only says
// what came away, where and how much of it. Everything in body space, millimetres.
namespace sdf::tools {

// A shaving, as a chisel, gouge or spokeshave takes it: sampled along the cut, oldest
// first, each sample standing for `step` mm of it.
struct ShavingSample {
	vec3 point{0.0f};       // at mid-thickness, above the cut's floor
	float s = 0.0f;         // mm along the cut
	float thickness = 0.0f; // mm: from the floor up to the surface the edge came in under
	float width = 0.0f;     // mm (a gouge's curved chip: its area over its thickness)
	bool starts = false;    // a new piece begins here (it broke off, or after a gap)
};

// A chip broken out of the work (tear-out, breakout, a chop's pop-off): the box round the
// material it took.
struct Chip {
	Frame frame;          // at its centre: x along the cut, z out of the face
	vec3 size{0.0f};      // extent along x, y, z
	float volume = 0.0f;  // mm^3
};

// Dust thrown off where the tool works.
struct Dust {
	vec3 point{0.0f};
	vec3 direction{0.0f}; // the way it is thrown (unit), or zero
	float volume = 0.0f;  // mm^3 of wood
	float grain = 0.5f;   // mm: how coarse it is
};

struct Debris {
	float step = 0.5f; // mm of cut each shaving sample stands for
	std::vector<ShavingSample> shaving;
	std::vector<Chip> chips;
	std::vector<Dust> dust;
	bool ended = false; // the stroke is over: the shaving so far comes away

	bool empty() const { return shaving.empty() && chips.empty() && dust.empty() && !ended; }
	float shaving_volume() const;
	float chip_volume() const;
	float dust_volume() const;
};

// How far below the work's surface `floor` lies, looking along `up` (unit, out of the
// work): 0 where it is in the air, at most `most`.
float depth_below_surface(const Body &body, const Octree &octree, vec3 floor, vec3 up, float most);

// The material `cut` takes out of `body` that none of `taken` (the stroke's other cuts)
// does: sampled on a grid over its bounds (about `samples` points), measured in the frame
// of `along` and `up`. False when it takes nothing.
bool measure_chip(const Body &body, const Octree &octree, const Edit &cut, const std::vector<Edit> &taken, vec3 along,
		vec3 up, Chip &out, int samples = 4000);

} // namespace sdf::tools
