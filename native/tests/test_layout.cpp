#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/layout.h"

#include <cmath>

using namespace sdf;

namespace {

float height_at(const Body &body, const Octree &octree, float x, float y) {
	const auto hit = raycast(body, octree, {x, y, 60.0f}, {0, 0, -1}, 200.0f, 1e-4f);
	return hit ? hit->point.z : std::nanf("");
}

} // namespace

TEST(a_scribed_line_is_a_narrow_v_as_deep_as_scribed) {
	// Drawn along the board's top 20 mm in from its edge (y = 30), from end to end.
	Body board = demo::board(mat::Ash);
	const float top = 12.5f, y = 30.0f;
	auto scribe = tools::chisel_stroke(tools::scribe_edge(), {-78.0f, y, top}, {0, 0, 1}, {1, 0, 0},
			tools::kScribeDepth);
	for (float x = -75.0f; x <= 78.0f; x += 3.0f) {
		scribe->move_to({x, y, top});
	}
	for (const Edit &e : scribe->edits()) {
		CHECK(board.add(e));
	}
	for (const Edit &e : scribe->finish()) {
		CHECK(board.add(e));
	}
	Octree octree;
	octree.build(board);
	// Its V: as deep as scribed on the line, the top untouched a little way either side (a
	// 40 degree V is 0.22 mm across at the top).
	const float half = tools::kScribeDepth * std::tan(20.0f * 3.14159265f / 180.0f);
	for (float x : {-60.0f, 0.0f, 60.0f}) {
		CHECK_NEAR(height_at(board, octree, x, y), top - tools::kScribeDepth, 2e-3);
		CHECK_NEAR(height_at(board, octree, x, y - half - 0.05f), top, 2e-3);
		CHECK_NEAR(height_at(board, octree, x, y + half + 0.05f), top, 2e-3);
		const float side = height_at(board, octree, x, y + 0.5f * half);
		CHECK(side < top - 0.1f && side > top - tools::kScribeDepth + 0.1f); // on its wall
	}
	CHECK_NEAR(height_at(board, octree, 0.0f, 20.0f), top, 2e-3);
}

TEST(the_gauge_and_knife_are_made_of_their_materials) {
	for (const float distance : {3.0f, 12.0f, 40.0f}) {
		tools::MarkingGauge gauge;
		gauge.distance = distance;
		const Body model = gauge.model();
		Octree octree;
		octree.build(model);
		// The fence's (brass) face at y = distance, beyond the pin, below the beam's top.
		const auto face = raycast(model, octree, {0.0f, distance - 30.0f, -10.0f}, {0, 1, 0}, 60.0f, 1e-4f);
		CHECK(face.has_value() && std::fabs(face->point.y - distance) < 0.05f);
		// The pin's point at the origin, just under the beam.
		const auto pin = raycast(model, octree, {0.0f, 0.0f, -20.0f}, {0, 0, 1}, 40.0f, 1e-4f);
		CHECK(pin.has_value() && pin->point.z > -0.5f && pin->point.z < 0.5f);
	}
	const Body knife = tools::MarkingKnife{}.model();
	Octree octree;
	octree.build(knife);
	const auto point = raycast(knife, octree, {5.0f, -0.3f, -20.0f}, {0, 0, 1}, 40.0f, 1e-4f);
	CHECK(point.has_value() && std::fabs(point->point.z) < 0.5f);
}
