#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "tools/edges.h"

#include <cmath>
#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

// The workshop board (160 x 100 x 25 mm, centred, arrises eased by a millimetre).
struct Board {
	Body body;
	Octree octree;

	Board() : body(demo::board(mat::Ash)) { octree.build(body); }
	void cut(const Primitive &prim) {
		Edit e;
		e.prim = prim;
		e.op = Op::Subtract;
		CHECK(body.add(e));
		octree.build(body);
	}
};

constexpr float kTop = 12.5f;
const vec3 kUp{0, 0, 1};

void describe(const char *what, const EdgeLock &e) {
	std::printf("    %s: %s, %.2f mm away, along (%.2f, %.2f, %.2f), step %.2f%s%s\n", what, e.found ? "found" : "none",
			double(e.distance), double(e.direction.x), double(e.direction.y), double(e.direction.z), double(e.step),
			e.floor ? ", a floor beyond" : "", e.fold ? ", a fold" : "");
}

} // namespace

// Near the board's end, the edge is its arris: along it, a drop beyond (off the work).
TEST(an_edge_lock_finds_the_boards_arris) {
	const Board b;
	const EdgeLock e = find_edge(b.body, b.octree, {76, 5, kTop}, kUp, 6.0f, 25.0f);
	describe("3-4 mm from the end", e);
	CHECK(e.found && std::fabs(e.direction.y) > 0.999f && e.step < -100.0f && !e.floor);
	CHECK(e.distance > 2.5f && e.distance < 4.5f && e.across.x < -0.99f);
	std::printf("    the face beyond: (%.2f, %.2f, %.2f)%s\n", double(e.far_normal.x), double(e.far_normal.y),
			double(e.far_normal.z), e.convex ? ", falling away" : "");
	CHECK(e.convex && e.far_normal.x > 0.99f); // the end face
	const EdgeLock none = find_edge(b.body, b.octree, {0, 0, kTop}, kUp, 6.0f, 25.0f);
	describe("the middle of the top", none);
	CHECK(!none.found);
}

// A rebate 1.2 mm deep over the right half: from its floor, its wall is a step up; from the
// top beside it, a step down to a floor, level at 1.2 mm.
TEST(an_edge_lock_finds_both_sides_of_a_rebate) {
	Board b;
	b.cut(Primitive::box({40, 0, kTop}, {40, 60, 1.2f}));
	const EdgeLock from_floor = find_edge(b.body, b.octree, {2, 3, kTop - 1.2f}, kUp, 6.0f, 25.0f);
	const EdgeLock from_top = find_edge(b.body, b.octree, {-2, 3, kTop}, kUp, 6.0f, 25.0f);
	describe("from the floor, 2 mm from the wall", from_floor);
	describe("from the top, 2 mm from the edge", from_top);
	CHECK(from_floor.found && std::fabs(from_floor.direction.y) > 0.999f && std::fabs(from_floor.step - 1.2f) < 0.1f);
	CHECK(std::fabs(from_floor.point.x) < 0.05f && std::fabs(from_floor.distance - 2.0f) < 0.05f && from_floor.across.x > 0.99f);
	CHECK(from_top.found && std::fabs(from_top.direction.y) > 0.999f && std::fabs(from_top.step + 1.2f) < 0.1f && from_top.floor);
	CHECK(std::fabs(from_top.point.x) < 0.05f && from_top.across.x < -0.99f);
}

// A chamfer along the end (a 45 degree face): its edge is a fold, found when folds of 25
// degrees count and not when only 60 do.
TEST(an_edge_lock_finds_a_fold_as_sharp_as_it_is_set_to) {
	Board b;
	// A box turned 45 degrees about y, its edge along the end's top arris: a 4.2 mm chamfer.
	const float s = std::sin(0.5f * 0.7853982f);
	b.cut(Primitive::box({-80, 0, kTop}, {3, 60, 3}, 0.0f, {0, s, 0, std::cos(0.5f * 0.7853982f)}));
	const EdgeLock sharp = find_edge(b.body, b.octree, {-73, 0, kTop}, kUp, 6.0f, 25.0f);
	const EdgeLock blunt = find_edge(b.body, b.octree, {-73, 0, kTop}, kUp, 6.0f, 60.0f);
	describe("the chamfer's edge, folds of 25 degrees", sharp);
	describe("folds of 60 degrees", blunt);
	CHECK(sharp.found && sharp.fold && std::fabs(sharp.direction.y) > 0.999f);
	CHECK(sharp.convex && std::fabs(sharp.far_normal.x + 0.7071f) < 0.02f && std::fabs(sharp.far_normal.z - 0.7071f) < 0.02f);
	CHECK(std::fabs(sharp.point.x + 80.0f - 3.0f * std::sqrt(2.0f)) < 0.1f);
	CHECK(!blunt.found || blunt.point.x < -77.0f); // (only the chamfer's own foot, off the work, if anything)
}
