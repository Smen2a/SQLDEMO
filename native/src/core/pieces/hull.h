#pragma once

#include "body/body.h"

#include <array>
#include <vector>

// A convex hull of points: the surface a small piece of debris (a crumb, a sliver) is drawn
// and collides as (pieces/pieces.h measure_crumbs() gives its points).
namespace sdf {

struct ConvexHull {
	std::vector<vec3> points;                  // the hull's corners (the input's, those on it)
	std::vector<std::array<int, 3>> triangles; // into points, wound counter-clockwise seen from outside

	bool empty() const { return triangles.empty(); }
};

// Incremental hull (quickhull's steps, point by point: a few dozen points). Points within
// `eps` (mm) of a face count as on it. Empty where the points span no volume (fewer than
// four, or all in a line or a plane, to within `eps`).
ConvexHull convex_hull(const std::vector<vec3> &points, float eps = 1e-4f);

} // namespace sdf
