#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/cutting.h"
#include "tools/debris.h"
#include "tools/rubbing.h"

#include <chrono>
#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

// The workshop board (160 x 100 x 25 mm, centred), or another body.
struct Work_ {
	Body body;
	Octree octree;
	MaterialTable materials = MaterialTable::standard();

	explicit Work_(const Body &b) : body(b) { octree.build(body); }
	Work work() const { return {body, octree, materials}; }
	Work_ with(const std::vector<Edit> &edits) const {
		Body b = body;
		for (const Edit &e : edits) {
			CHECK(b.add(e));
		}
		return Work_(b);
	}
	// The surface's height under (x, y), looking straight down; NaN off the work.
	float at(float x, float y) const {
		const auto hit = raycast(body, octree, {x, y, 60.0f}, {0, 0, -1}, 200.0f, 1e-4f);
		return hit ? hit->point.z : std::nanf("");
	}
};

constexpr float kTop = 12.5f;
const vec3 kUp{0, 0, 1};

// Back and forth between a and b, `strokes` times, a millimetre a move.
std::vector<vec3> reciprocate(vec3 a, vec3 b, int strokes) {
	std::vector<vec3> path;
	const int steps = int(std::ceil(gl::length(b - a)));
	for (int k = 0; k < strokes; ++k) {
		for (int i = 1; i <= steps; ++i) {
			const float t = float(i) / float(steps);
			path.push_back(k % 2 == 0 ? a + (b - a) * t : b + (a - b) * t);
		}
	}
	return path;
}

// The stroke moved along `path`: its edits as the edit session would hold them after the
// updates (`held`, from updates before), which must be what it merges to.
std::vector<Edit> run(Stroke &stroke, const std::vector<vec3> &path, std::vector<Edit> held = {}) {
	for (const vec3 &p : path) {
		const StrokeUpdate u = stroke.move_to(p);
		CHECK(u.drop <= held.size());
		held.resize(held.size() - std::min(u.drop, held.size()));
		held.insert(held.end(), u.edits.begin(), u.edits.end());
	}
	CHECK(held.size() == stroke.edits().size());
	return held;
}

Body board_with_bump() {
	Body b = demo::board(mat::Ash);
	Edit bump;
	bump.prim = Primitive::box({0, 0, kTop + 0.5f}, {5, 5, 0.5f});
	bump.op = Op::Union;
	bump.material = mat::Ash;
	CHECK(b.add(bump));
	return b;
}

} // namespace

// A block of 120 grit takes a hundredth of a millimetre off ash in a metre of rubbing (about
// a minute's steady sanding); 80 grit half as much again, 240 half as much; harder wood
// less. Only where it rubbed.
TEST(a_sanding_block_takes_hundredths_where_it_rubs) {
	const Work_ ash(demo::board(mat::Ash));
	const SandingBlock block;
	CHECK(std::fabs(block.removal_per_mm() - 9.78e-6f) < 0.05e-6f);
	SandingBlock coarse = block, fine = block;
	coarse.grit = 80;
	fine.grit = 240;
	CHECK(std::fabs(coarse.removal_per_mm() / block.removal_per_mm() - 1.5f) < 1e-3f);
	CHECK(std::fabs(fine.removal_per_mm() / block.removal_per_mm() - 0.5f) < 1e-3f);
	CHECK(block.removal_per_mm(7000.0f) < block.removal_per_mm());

	const auto t0 = std::chrono::steady_clock::now();
	auto stroke = sanding_stroke(block, ash.work(), {-20, -15, kTop}, kUp, {1, 0, 0});
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("    set on the board in %.1f ms\n", ms);
	// 1000 mm back and forth over 20 mm, towards one corner of the board.
	run(*stroke, reciprocate({-30, -15, kTop}, {-10, -15, kTop}, 50));
	const StrokeState s = stroke->state();
	std::printf("    120 grit: %.4f mm after a metre, on %.0f%% of its face\n", double(s.depth), double(100.0f * s.contact));
	CHECK(std::fabs(s.depth - block.removal_per_mm() * 1000.0f) < 0.02f * s.depth);
	CHECK(s.contact > 0.99f);

	// Measurable at a hundred times the pace: lowered under where it went, and nowhere else.
	auto fast = sanding_stroke(block, ash.work(), {-20, -15, kTop}, kUp, {1, 0, 0}, 100.0f);
	const Work_ sanded = ash.with(run(*fast, reciprocate({-30, -15, kTop}, {-10, -15, kTop}, 50)));
	const float depth = fast->state().depth;
	CHECK(depth > 0.9f && depth < 1.0f);
	CHECK(std::fabs(sanded.at(-20, -15) - (kTop - depth)) < 5e-3f);
	CHECK(std::fabs(sanded.at(60, 30) - kTop) < 1e-3f);  // the far corner
	CHECK(std::fabs(sanded.at(-20, 20) - kTop) < 1e-3f); // beyond its breadth
}

// Worked back and forth along a diagonal, the block sands a band along it, not the
// rectangle round it; along two sides of a square, two bands, not the square.
TEST(a_block_sands_only_along_its_way) {
	const Work_ ash(demo::board(mat::Ash));
	auto diagonal = sanding_stroke(SandingBlock{}, ash.work(), {-50, -25, kTop}, kUp, {1, 0, 0}, 300.0f);
	std::vector<Edit> edits = run(*diagonal, reciprocate({-50, -25, kTop}, {50, 25, kTop}, 6));
	std::printf("    back and forth along a diagonal: %zu patch(es)\n", edits.size());
	const Work_ band = ash.with(edits);
	CHECK(band.at(0, 0) < kTop - 0.1f);
	CHECK(std::fabs(band.at(-50, 25) - kTop) < 1e-3f);
	CHECK(std::fabs(band.at(50, -25) - kTop) < 1e-3f);

	auto corner = sanding_stroke(SandingBlock{}, ash.work(), {-50, -25, kTop}, kUp, {1, 0, 0}, 300.0f);
	std::vector<vec3> path = reciprocate({-50, -25, kTop}, {50, -25, kTop}, 3);
	for (const vec3 &p : reciprocate({50, -25, kTop}, {50, 25, kTop}, 3)) {
		path.push_back(p);
	}
	edits = run(*corner, path);
	std::printf("    along two sides of a square: %zu patches\n", edits.size());
	CHECK(edits.size() >= 2 && edits.size() <= 3); // (a third where its edge reached untouched ground)
	const Work_ sides = ash.with(edits);
	CHECK(sides.at(0, -25) < kTop - 0.05f && sides.at(50, 0) < kTop - 0.05f);
	CHECK(std::fabs(sides.at(-40, 20) - kTop) < 1e-3f);
}

// A hard block rests on what stands highest: a bump goes first, fast (all the pressure is
// on it), while the flat round it is untouched until the bump is down to it.
TEST(a_sanding_block_takes_a_bump_down_first) {
	const Work_ bumped(board_with_bump());
	auto stroke = sanding_stroke(SandingBlock{}, bumped.work(), {0, 0, kTop + 1.0f}, kUp, {1, 0, 0}, 10.0f);
	const std::vector<Edit> first = run(*stroke, reciprocate({-10, 0, kTop + 1}, {10, 0, kTop + 1}, 30));
	const Work_ partly = bumped.with(first);
	const float depth = stroke->state().depth;
	std::printf("    on %.0f%% of its face: %.2f mm off the bump in 600 mm\n", double(100.0f * stroke->state().contact),
			double(depth));
	CHECK(depth > 0.4f && depth < 0.8f);
	CHECK(std::fabs(partly.at(0, 0) - (kTop + 1.0f - depth)) < 5e-3f);
	CHECK(std::fabs(partly.at(22, 0) - kTop) < 1e-3f);
	// On until the bump is gone: then the whole face bears, and the flat comes down slowly.
	run(*stroke, reciprocate({-10, 0, kTop + 1}, {10, 0, kTop + 1}, 60), first);
	const StrokeState s = stroke->state();
	CHECK(s.depth > 1.0f && s.contact > 0.9f);
	const Work_ flat = bumped.with(stroke->edits());
	std::printf("    then %.2f mm in all, on %.0f%%: the flat %.3f mm down\n", double(s.depth), double(100.0f * s.contact),
			double(kTop - flat.at(22, 0)));
	CHECK(flat.at(22, 0) < kTop - 1e-3f && flat.at(22, 0) > kTop - 0.1f);
	CHECK(std::fabs(flat.at(0, 0) - flat.at(22, 0)) < 5e-3f);
}

// On a narrow edge the hand's pressure bears on less of the face: it sands faster.
TEST(a_narrow_edge_sands_faster_than_a_wide_face) {
	Body strip;
	strip.base = Primitive::box({0, 0, 0}, {80, 3, 12.5f});
	strip.base_material = mat::Ash;
	const Work_ narrow(strip), wide(demo::board(mat::Ash));
	auto on_edge = sanding_stroke(SandingBlock{}, narrow.work(), {0, 0, kTop}, kUp, {1, 0, 0});
	auto on_face = sanding_stroke(SandingBlock{}, wide.work(), {0, 0, kTop}, kUp, {1, 0, 0});
	const auto path = reciprocate({-10, 0, kTop}, {10, 0, kTop}, 10);
	run(*on_edge, path);
	run(*on_face, path);
	const float ratio = on_edge->state().depth / on_face->state().depth;
	std::printf("    on %.0f%% of its face: %.1f times as fast\n", double(100.0f * on_edge->state().contact), double(ratio));
	CHECK(ratio > 4.0f && ratio < 10.0f);
}

// Its dust is what it took, patch by patch.
TEST(sanding_dust_is_what_the_patches_take) {
	const Work_ ash(demo::board(mat::Ash));
	auto stroke = sanding_stroke(SandingBlock{}, ash.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 300.0f);
	Debris d;
	for (const vec3 &p : reciprocate({-40, 0, kTop}, {40, 20, kTop}, 4)) {
		stroke->move_to(p);
		stroke->debris(ash.body, ash.octree, d);
	}
	stroke->debris(ash.body, ash.octree, d, true);
	const Work_ sanded = ash.with(stroke->edits());
	double lost = 0.0;
	for (float y = -44.75f; y < 45.0f; y += 0.5f) {
		for (float x = -77.75f; x < 78.0f; x += 0.5f) {
			lost += double(ash.at(x, y) - sanded.at(x, y)) * 0.25;
		}
	}
	std::printf("    %zu patches: %.1f mm^3 of dust; the board lost %.1f mm^3\n", stroke->edits().size(),
			double(d.dust_volume()), lost);
	CHECK(lost > 50.0 && std::fabs(double(d.dust_volume()) - lost) < 0.15 * lost);
}
