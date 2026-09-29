#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "tools/boring.h"
#include "tools/catalog.h"
#include "tools/cutting.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

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

// Turns a brace round its bit at `centre` (on the face `side`), an eighth of a turn at a
// time, until it is through (or `most` turns); its hole cut into the head. Returns the turns.
float bore(Head &head, const Brace &brace, vec3 centre, float side, float most = 200.0f) {
	const vec3 n(0.0f, 0.0f, side);
	auto stroke = boring_stroke(brace, head.work(), centre, n, {1.0f, 0.0f, 0.0f});
	const Frame f = Frame::at(centre, n, {1.0f, 0.0f, 0.0f});
	float turns = 0.0f;
	for (int k = 0; k <= int(most * 8.0f) && stroke->state().limit.empty(); ++k) {
		const float a = -float(k) * 0.25f * 3.14159265f; // clockwise from above
		stroke->move_to(f.point({40.0f * std::cos(a), 40.0f * std::sin(a), 0.0f}));
		turns = float(k) / 8.0f;
	}
	head.add(stroke->edits());
	return turns;
}

// How deep below the face `side` a chisel's edge `width` wide first meets wood across its
// width, its middle at `at` (on the face) and its back to `path`: where it rests.
float resting(const Head &head, vec3 at, vec3 path, float side, float width) {
	const vec3 n(0.0f, 0.0f, side), b = gl::cross(n, path);
	float least = 2.0f * kHalfT;
	for (int j = -4; j <= 4; ++j) {
		const vec3 q = at + path * 0.3f + b * (0.45f * width * float(j) / 4.0f);
		for (float d = 0.0f; d < least; d += 0.1f) {
			if (head.octree.distance(head.body, q - n * d) < 0.0f) {
				least = d;
				break;
			}
		}
	}
	return least;
}

// Chops down from the face `side` along a marked line (the chisel's middle at `at`, on the
// face; its bevel towards `path`, the waste) until no wood stands within `band` mm of the
// line down to `depth` (or `most` blows): a firm blow each time with the edge on the
// line, or moved in onto what is left nearer the waste, where it rests. Returns the blows.
int chop_down(Head &head, const Chisel &c, vec3 at, vec3 path, float side, float depth, float band, int most,
		std::uint32_t &seed) {
	const vec3 n(0.0f, 0.0f, side);
	int blows = 0;
	while (blows < most) {
		bool struck = false;
		for (float in = 0.0f; in <= band && !struck; in += 0.6f) {
			const vec3 line = at + path * in;
			const float d = resting(head, line, path, side, c.width);
			if (d >= depth) {
				continue;
			}
			const CutPlan plan = plan_cut(c, head.work(), line - n * d, n, path, 0.5f * c.width, 0.0f, 0.0f, ++seed, 1.0f);
			head.add(plan.edits());
			++blows;
			struck = true;
		}
		if (!struck) {
			break;
		}
	}
	return blows;
}

} // namespace

// A brace with a 12 mm bit bores through the head's 55 mm of oak, 1.6 mm a turn: a
// round hole, its walls where the bit's rim went, clean through and a millimetre beyond;
// turned back, it goes no deeper (a ratchet brace). Its updates as it goes cut what its
// merged edits do. Set down off the mortise's middle and past its end, it is held to the
// lines: in between the sides, its rim on the end line.
TEST(a_brace_bores_through_oak) {
	Head head;
	const Brace brace{find_bit("bit_12")->bit};
	const vec3 centre(-14.0f, 0.0f, kHalfT), n(0.0f, 0.0f, 1.0f);
	auto stroke = boring_stroke(brace, head.work(), centre, n, {1.0f, 0.0f, 0.0f});
	const Frame f = Frame::at(centre, n, {1.0f, 0.0f, 0.0f});
	auto grip = [&](float turns) {
		const float a = -turns * 2.0f * 3.14159265f;
		return f.point({40.0f * std::cos(a), 40.0f * std::sin(a), 0.0f});
	};
	// The updates applied as they come (dropping what each drops).
	std::vector<Edit> applied;
	auto apply = [&](const StrokeUpdate &u) {
		applied.resize(applied.size() - u.drop);
		applied.insert(applied.end(), u.edits.begin(), u.edits.end());
	};
	// Half a turn on, then back: no deeper for coming back.
	for (int k = 0; k <= 4; ++k) {
		apply(stroke->move_to(grip(float(k) / 8.0f)));
	}
	const float half = stroke->state().depth;
	for (int k = 3; k >= 0; --k) {
		apply(stroke->move_to(grip(float(k) / 8.0f)));
	}
	CHECK(std::fabs(half - 0.8f) < 0.01f && std::fabs(stroke->state().depth - half) < 1e-5f);
	float turns = 0.0f;
	for (int k = 1; k <= 8 * 60 && stroke->state().limit.empty(); ++k) {
		apply(stroke->move_to(grip(float(k) / 8.0f)));
		turns = float(k) / 8.0f;
	}
	std::printf("    a 12 mm bit through 55 mm of oak: %.1f turns, %.1f mm deep, %s; %zu edits as it went, %zu merged\n",
			0.5f + turns, stroke->state().depth, stroke->state().limit.c_str(), applied.size(), stroke->edits().size());
	CHECK(stroke->state().limit == "through" && std::fabs(stroke->state().depth - 56.0f) < 0.3f);
	CHECK(std::fabs(1.6f * (0.5f + turns) - stroke->state().depth) < 0.2f); // (the half turn first)
	Body gone_as_it_went = head.body;
	for (const Edit &e : applied) {
		gone_as_it_went.add(e);
	}
	head.add(stroke->edits());
	// Its wall: air just inside the rim, wood just outside, top to bottom; clean through.
	int wrong = 0, differs = 0;
	for (float z = -kHalfT + 0.5f; z < kHalfT - 0.5f; z += 2.0f) {
		for (int k = 0; k < 12; ++k) {
			const float a = float(k) * 3.14159265f / 6.0f;
			const vec3 dir(std::cos(a), std::sin(a), 0.0f);
			const vec3 in = vec3(centre.x, 0.0f, z) + dir * 5.8f, out = vec3(centre.x, 0.0f, z) + dir * 6.2f;
			wrong += int(head.octree.distance(head.body, in) < 0.0f) + int(head.octree.distance(head.body, out) > 0.0f);
			differs += int((gone_as_it_went.distance(in) < 0.0f) != (head.body.distance(in) < 0.0f));
			differs += int((gone_as_it_went.distance(out) < 0.0f) != (head.body.distance(out) < 0.0f));
		}
	}
	CHECK(wrong == 0 && differs == 0);
	CHECK(head.floor(centre.x, 1.0f) >= 2.0f * kHalfT - 0.1f);
	Limits lines;
	lines.sides = {Stop::through({0.0f, -6.0f, 0.0f}, {0.0f, 1.0f, 0.0f}), Stop::through({0.0f, 6.0f, 0.0f}, {0.0f, -1.0f, 0.0f})};
	lines.ends = {Stop::through({kX0, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}), Stop::through({kX1, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f})};
	const vec3 held = hold_bit({kX0 - 2.0f, 3.0f, kHalfT}, n, 6.0f, lines);
	CHECK(gl::length(held - vec3(kX0 + 6.0f, 0.0f, kHalfT)) < 1e-4f);
}

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

// The same mortise bored first: three 12 mm holes through from the face side, 9 mm apart
// along its middle (the end ones touching its ends), then what the holes leave (the cusps
// along its sides, its corners) chopped down to the lines, half from each face: the bench
// chisel's edge along each side (its bevel to the holes), and across each end.
TEST(the_mallets_mortise_bored_first) {
	Head head;
	const Brace brace{find_bit("bit_12")->bit};
	const Chisel bench = chisel("bench_12");
	std::uint32_t seed = 11;
	const auto start = std::chrono::steady_clock::now();
	float turns = 0.0f;
	for (const float x : {kX0 + 6.0f, 0.5f * (kX0 + kX1), kX1 - 6.0f}) {
		turns += bore(head, brace, {x, 0.0f, kHalfT}, 1.0f);
	}
	const std::size_t bored_edits = head.body.edits().size();
	int blows = 0;
	for (const float side : {1.0f, -1.0f}) {
		const float depth = kHalfT + 1.0f, top = side * kHalfT;
		for (const float x : {kX0 + 6.0f, kX0 + 10.5f, -5.0f, kX1 - 10.5f, kX1 - 6.0f}) {
			blows += chop_down(head, bench, {x, 6.0f, top}, {0.0f, -1.0f, 0.0f}, side, depth, 2.5f, 60, seed);
			blows += chop_down(head, bench, {x, -6.0f, top}, {0.0f, 1.0f, 0.0f}, side, depth, 2.5f, 60, seed);
		}
		blows += chop_down(head, bench, {kX0, 0.0f, top}, {1.0f, 0.0f, 0.0f}, side, depth, 6.0f, 60, seed);
		blows += chop_down(head, bench, {kX1, 0.0f, top}, {-1.0f, 0.0f, 0.0f}, side, depth, 6.0f, 60, seed);
	}
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	// What is left inside the lines (every 0.5 mm across, 1 mm along, 2 mm down), and what was
	// taken beyond its sides (0.5 mm out).
	int left = 0, all = 0, beyond = 0;
	for (float z = -kHalfT + 1.0f; z < kHalfT - 1.0f; z += 2.0f) {
		for (float x = kX0 + 0.5f; x < kX1; x += 1.0f) {
			for (float y = -5.75f; y < 6.0f; y += 0.5f) {
				++all;
				left += int(head.octree.distance(head.body, {x, y, z}) < 0.0f);
			}
			beyond += int(head.octree.distance(head.body, {x, 6.5f, z}) > 0.0f) + int(head.octree.distance(head.body, {x, -6.5f, z}) > 0.0f);
		}
	}
	std::printf("    bored first: %.0f turns of the brace (three holes), then %d firm blows; %.1f%% of the mortise left "
			"standing, %d points beyond its sides taken; %zu edits (%zu bored), %.1f s computed\n",
			turns, blows, 100.0 * double(left) / double(all), beyond, head.body.edits().size(), bored_edits,
			ms / 1000.0);
	std::printf("    at 2 turns a second and a blow each 0.35 s, %.0f s of boring and %.0f s of chopping\n",
			double(turns) / 2.0, 0.35 * double(blows));
	CHECK(blows > 0 && blows < 150);
	CHECK(left * 100 < all && beyond == 0);
}
