#pragma once

#include "plans/part.h"

#include <functional>
#include <vector>

// Checking a part against its drawing: where the wood is proud of the part as drawn
// (part_solid: wood still there that the drawing takes away) and where it is short of it
// (wood gone that the drawing keeps), each place measured on the surfaces and named by the
// feature (or the blank's face) it is on. A guide only: no score.
namespace sdf::plans {

struct Spot {
	enum class Kind : std::uint8_t { Proud, Short };
	Kind kind = Kind::Proud;
	float most = 0.0f;   // mm: the surface furthest off the drawing
	float area = 0.0f;   // mm^2 of surface off by more than the tolerance
	float volume = 0.0f; // mm^3 of wood there that should not be (proud), or gone (short)
	vec3 at{0.0f};       // the worst place, on the drawing's surface (part space)
	vec3 normal{0.0f};   // the drawing's outward normal there
	int feature = -1;    // the feature whose face it is (its index); -1: the blank's
	// Points on the wood's surface (proud) or the drawing's (short), and how far each is off
	// (at most kDots of them, spread over the spot).
	std::vector<vec3> dots;
	std::vector<float> by;
	static constexpr int kDots = 150;
};

struct Check {
	std::vector<Spot> spots; // the worst first
	float proud = 0.0f;      // mm^3 in all
	float short_ = 0.0f;
	int samples = 0; // grid points looked at
	double ms = 0.0;
};

// Checks the wood (its signed distance at a point of part space, and its bounds there)
// against `part` as drawn, at grid `spacing` mm: surfaces more than `tolerance` mm off,
// joined into spots. Along the blank's arrises, which the drawing eases by a millimetre as
// stock is, a square arris is allowed another 0.75 mm.
Check check_part(const Part &part, const std::function<float(vec3)> &wood, const Aabb &wood_bounds,
		float tolerance = 0.5f, float spacing = 1.0f);

} // namespace sdf::plans
