#pragma once

#include "body/body.h"
#include "util/pose.h"

#include <functional>
#include <vector>

// How a part goes into its mate along a straight line (a tenon into a mortise, a wedge
// into a kerf): at each step of the way, how far the part's surface runs into the mate
// across the line (interference: it binds) or how near it comes (clearance: it rattles),
// and whether it has met the mate end on (a shoulder on the mortise's face: seated). From
// that, where it stops and how it fits: loose, snug (home by hand), drives (home with the
// mallet) or won't go.
namespace sdf {

using Distance = std::function<float(vec3)>;

// A point on a surface and its outward normal there.
struct SurfacePoint {
	vec3 p{0.0f};
	vec3 n{0.0f};
};

// Points on a body's surface within `region`, about `spacing` mm apart: the grid points
// within half a spacing of it, moved onto it along the field's gradient.
std::vector<SurfacePoint> surface_points(const Distance &distance, const Aabb &region, float spacing = 1.0f);

struct FitLimits {
	float snug = 0.05f;    // mm of interference a hand pushes home
	float drive = 0.5f;    // mm the mallet drives home (more: it won't go)
	float loose = 0.1f;    // mm of clearance, each side, that rattles
	float band = 1.0f;     // mm: surfaces nearer than this are measured
	float end = 0.7f;      // |normal . axis| above this: the part meets the mate end on
	float seated = 0.05f;  // mm: end on this near, it is home
	float mouth = 1.0f;    // mm: within this of the mouth, its leading end on the mate's rim is no seat
};

struct FitStep {
	float t = 0.0f;
	float interference = 0.0f; // the part's side surface deepest into the mate (mm)
	float clearance = 0.0f;    // its side surface nearest the mate, outside it (at most band)
	bool seated = false;       // end on against the mate
};

struct Fit {
	enum class Kind : std::uint8_t { Loose, Snug, Drives, WontGo };
	Kind kind = Kind::Snug;
	float stops_at = 0.0f; // how far it goes (mm along the line)
	bool home = false;     // it gets all the way (to `travel`)
	bool seated = false;   // stopped end on against the mate
	float most = 0.0f;     // the most interference on the way
	float clearance = 0.0f; // at where it stops
	std::vector<FitStep> steps;
	int points = 0;
	double ms = 0.0;
};

// The part's surface (its points and normals, in its own space) carried from `start` (its
// space into the mate's, at the mouth) along `axis` (the mate's space) for `travel` mm in
// `step`s, against the mate's distance (in the mate's space). It stops where its side runs
// more than limits.drive into the mate, or it meets the mate end on.
Fit fit_along(const std::vector<SurfacePoint> &part, const Distance &mate, const Pose &start, vec3 axis, float travel,
		float step = 0.5f, const FitLimits &limits = {});

const char *fit_name(Fit::Kind kind);

} // namespace sdf
