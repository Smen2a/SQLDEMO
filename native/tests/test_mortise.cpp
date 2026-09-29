#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "tools/catalog.h"
#include "tools/cutting.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

// The mallet's head blank (oak, 120 x 70 x 55, centred) and its mortise as the workshop lays
// it out: 30 mm along the grain (x -20 to 10), 12 across (y -6 to 6), through.
constexpr float kHalfT = 27.5f;
constexpr float kX0 = -20.0f, kX1 = 10.0f;

struct Head {
	Body body = demo::stock(mat::Oak, {120, 70, 55});
	Octree octree;
	MaterialTable materials = MaterialTable::standard();
	Head() { octree.build(body); }
	Work work() const { return {body, octree, materials}; }
	void add(const std::vector<Edit> &edits) {
		for (const Edit &e : edits) {
			body.add(e);
		}
		octree.build(body);
	}
	// How deep the mortise's floor is at x (its middle across), from the face `side` (+1
	// the top, -1 the bottom): the first wood going in.
	float floor(float x, float side) const {
		for (float d = 0.0f; d < 2.0f * kHalfT; d += 0.1f) {
			if (octree.distance(body, {x, 0.0f, side * (kHalfT - d)}) < 0.0f) {
				return d;
			}
		}
		return 2.0f * kHalfT;
	}
};

Chisel chisel(const char *id) {
	Chisel c = find_chisel(id)->chisel;
	c.approach_deg = 90.0f;
	return c;
}

// A joiner's chopping of the mortise from one face, pass after pass: along it from one end to
// the other in `step` mm steps, the bevel towards the cuts already made (each blow's chip
// pops off into them), each blow at the floor where the chisel stands; until the floor is
// `depth` deep all along (or `most` blows). Returns the blows.
int chop_from(Head &head, const Chisel &c, float side, float depth, float step, float blow, int most, std::uint32_t &seed) {
	int blows = 0;
	const vec3 normal(0.0f, 0.0f, side);
	// The first cut near the far end, then back towards the near end, the bevel (the path's
	// side) towards the far end.
	const vec3 path(1.0f, 0.0f, 0.0f);
	const float hw = 0.5f * c.width;
	for (int pass = 0; pass < 60 && blows < most; ++pass) {
		bool deeper = false;
		for (float x = kX1 - 1.0f; x >= kX0 + 1.0f && blows < most; x -= step) {
			const float d = head.floor(x, side);
			if (d >= depth) {
				continue;
			}
			deeper = true;
			const vec3 start(x, 0.0f, side * (kHalfT - d));
			const CutPlan plan = plan_cut(c, head.work(), start, normal, path, hw, 0.0f, 0.0f, ++seed, blow);
			head.add(plan.edits());
			++blows;
		}
		if (!deeper) {
			break;
		}
	}
	return blows;
}

} // namespace

// Chopping the mallet's mortise (30 x 12, 55 deep, through) in oak with a 12 mm bench chisel
// and firm blows, half from each face: how many blows, and how deep each pass goes. The
// workshop's blows come no faster than one each 0.35 s: the blows set the least time a player
// spends chopping it. (Measured: 110 blows from each face, 220 in all, well over the ~150
// that make it too slow for play: a brace and bit takes the waste out first.)
TEST(chopping_the_mallets_mortise) {
	Head head;
	const Chisel bench = chisel("bench_12");
	std::uint32_t seed = 7;
	const auto start = std::chrono::steady_clock::now();
	const int top = chop_from(head, bench, 1.0f, kHalfT + 1.0f, 3.0f, 1.0f, 900, seed);
	const float from_top = head.floor(-5.0f, 1.0f);
	const int bottom = chop_from(head, bench, -1.0f, kHalfT + 1.0f, 3.0f, 1.0f, 900, seed);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	const bool through = head.floor(-5.0f, 1.0f) >= 2.0f * kHalfT - 0.1f;
	std::printf("    a 12 mm bench chisel, firm blows: %d from the face side (%.1f mm deep), %d from the back; %s; "
			"%zu edits, %.1f s computed\n",
			top, double(from_top), bottom, through ? "through" : "not through", head.body.edits().size(), ms / 1000.0);
	std::printf("    at a blow each 0.35 s, %.0f s of chopping at the least\n", 0.35 * double(top + bottom));
	CHECK(top > 0 && bottom > 0 && through && top + bottom < 400);
}
