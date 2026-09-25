#pragma once

#include "adf/adf.h"
#include "body/body.h"
#include "compile/octree.h"

#include <vector>

// Bodies that come apart: whether a cut left a body in pieces, and what each piece needs to
// become a body of its own (its half of the edit list, its bounds, its volume, the points
// its physics hull is built from).
//
// A piece is the body cut by a half-space: the body with an Op::Intersect of a plane
// appended. The plane lies in the air of the cut that separated it (the middle of a saw's
// kerf), so it adds no surface.
namespace sdf {

struct Plane {
	vec3 point, normal; // normal: unit
	float distance(vec3 p) const { return gl::dot(p - point, normal); }
	Plane flipped() const { return {point, -normal}; }
	// The edit keeping what lies behind the plane (distance < 0).
	Edit keep_behind() const;
};

// Where a stroke cut clean through the work: the middle of its cut, and the cut's width.
struct Separation {
	Plane plane;
	float gap = 0.0f; // the cut's width (a saw's kerf)
	// What each piece keeps: the half-space behind its own face of the cut, 5 um into the
	// gap. (A plane in the middle of the gap would leave the field just outside each face
	// still seeing the other face: a kink in the air that the ADF refines all along it.)
	Plane behind() const;
	Plane front() const;
	Separation flipped() const { return {plane.flipped(), gap}; }
};

// Both sides of a separation, measured from the ADF: what each piece needs to become a
// body of its own (the worker measures them as soon as it finds the parts apart).
struct PieceSides {
	Separation cut;
	double volume[2] = {0.0, 0.0}; // behind the cut, in front of it (mm^3)
	vec3 centre[2];                // centres of volume
	std::vector<vec3> hull[2];     // hull_points() of each
};
// With `smaller_in_front`, the cut is turned so that its front is the smaller side (the
// piece that comes away).
PieceSides measure_sides(const Adf &adf, const Separation &cut, bool smaller_in_front);

// Whether no material crosses `plane` within `box` (the body's bounds): then the body lies
// in two parts, one on each side, as any path from one side to the other crosses the plane.
// An adaptive quadtree over the plane: a square is clear where the field at its centre
// exceeds the Lipschitz bound times its half-diagonal; others split, down to `finest`
// (mm). Any sample in material, or a square still unresolved at `finest` (a bridge thinner
// than that), means the parts still hold together. `samples`, if given, counts evaluations.
bool plane_clear(const Body &body, const Octree &octree, const Plane &plane, const Aabb &box, float finest = 0.05f,
		std::size_t *samples = nullptr);

// The bounds of what of `box` lies behind `plane`.
Aabb clip_box(const Aabb &box, const Plane &plane);

// The volume (mm^3) of material behind `plane` (all of it without one), from the ADF: solid
// leaves, and the voxels of brick leaves whose centres (by their bricks) are inside. With
// `centroid`, also where its centre of volume lies.
double volume(const Adf &adf, const Plane *plane = nullptr, vec3 *centroid = nullptr);

// Points on the surface behind `plane` (all of it without one) for a convex physics hull:
// where the ADF's bricks cross zero along their sample edges, reduced to at most `count`
// extreme points (the furthest along evenly spread directions), then to the outermost of
// any lying within `merge` (mm) of each other. Extremes of a sampled surface come in tight
// clusters round its corners and along its edges; left in, they make sliver faces that
// physics engines rest badly on (a hull of them rocks and sinks where one of its corners
// does not).
std::vector<vec3> hull_points(const Adf &adf, const Plane *plane = nullptr, int count = 256, float merge = 1.0f);

} // namespace sdf
