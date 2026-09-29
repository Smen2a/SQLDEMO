#pragma once

#include "body/body.h"
#include "compile/octree.h"

#include <optional>

namespace sdf {

// Where a ray first meets a body's surface: the exact field (octree tapes), for pointing at
// bodies and, later, tool contact.
struct Hit {
	float t = 0;    // distance along the (unit) direction
	vec3 point;     // on the surface, to within `epsilon` of raycast()
	vec3 normal;    // unit, out of the body
	Sample sample;  // material there
};

// Sphere-traces from `origin` along unit `dir` up to `t_max`, stopping within `epsilon`
// of the surface and settling onto it. `octree` must be built from `body`.
std::optional<Hit> raycast(const Body &body, const Octree &octree, vec3 origin, vec3 dir, float t_max,
		float epsilon = 1e-3f);

} // namespace sdf
