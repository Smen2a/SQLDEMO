#pragma once

#include "body/body.h"
#include "compile/octree.h"
#include "tools/tools.h"

// Edges on the work near a point, for a tool to lock onto: where the surface steps up or
// down (a wall, an earlier cut's side, the board's edge) or folds (an arris, a chamfer's
// edge). Body space, millimetres.
namespace sdf::tools {

struct EdgeLock {
	bool found = false;
	vec3 point{0.0f};      // on the edge line, nearest the given point
	vec3 direction{0.0f};  // unit, along the edge, in the point's tangent plane
	vec3 across{0.0f};     // unit, in that plane, from the edge towards the point's side
	float distance = 0.0f; // mm from the point to the edge line
	float step = 0.0f;     // mm the far side stands above (+, a wall) or below (-) the near side
	bool floor = false;    // the far side is a floor within kFloorReach below (an earlier cut's)
	bool fold = false;     // it is a fold, not a step: the surface turns there, all of a piece
};

// How far below the near side a floor may lie (mm) and still be one.
constexpr float kFloorReach = 10.0f;

// The edge nearest `point` (on the surface, `normal` out of it) within `reach` mm: where the
// surface, seen along -normal, steps by 0.1 mm or more beyond its own slope, or folds by
// `fold_deg` or more. Samples a height map every millimetre round the point (about 170
// rays), then finds the line where it crosses by bisection.
EdgeLock find_edge(const Body &body, const Octree &octree, vec3 point, vec3 normal, float reach, float fold_deg);

} // namespace sdf::tools
