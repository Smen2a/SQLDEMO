#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/cutting.h"
#include "tools/layout.h"
#include "tools/shaping.h"

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

namespace {

struct Worked {
	Body body = demo::board(mat::Ash);
	Octree octree;
	MaterialTable materials = MaterialTable::standard();

	Worked() { octree.build(body); }
	tools::Work work() const { return {body, octree, materials}; }
	void apply(const std::vector<Edit> &edits) {
		for (const Edit &e : edits) {
			CHECK(body.add(e));
		}
		octree.build(body);
	}
	float at(float x, float y) const { return height_at(body, octree, x, y); }
};

const vec3 kUp{0, 0, 1};
constexpr float kTop = 12.5f;

} // namespace

// A rebate's floor gauged 0.3 mm down: a chisel asked for more goes no deeper, and a second
// pass over it takes nothing.
TEST(a_plan_held_to_a_floor_goes_no_deeper) {
	Worked w;
	tools::Limits limits;
	limits.floors.push_back(tools::Stop::through({0, 0, kTop - 0.3f}, kUp));
	tools::Chisel bench = tools::Chisel{};
	bench.approach_deg = 20.0f;
	for (int pass = 0; pass < 2; ++pass) {
		const float top = w.at(-79.0f, 0.0f);
		tools::CutPlan plan = tools::plan_cut(bench, w.work(), {-79.5f, 0, top}, kUp, {1, 0, 0}, 50.0f, 1.0f, 0.0f, 1);
		const bool held = tools::limit_plan(plan, limits);
		CHECK(held == (pass == 0) || plan.depth < 1e-3f);
		CHECK((plan.stop & tools::kAtLine) != 0 || pass == 1);
		w.apply(plan.edits());
	}
	for (float x : {-70.0f, -50.0f, -40.0f}) {
		CHECK_NEAR(w.at(x, 0.0f), kTop - 0.3f, 0.01);
	}
}

// A knife line across the way: the cut ends there, square, and the wood beyond it is left.
TEST(a_plan_stops_square_at_a_knife_line) {
	Worked w;
	tools::Limits limits;
	limits.ends.push_back(tools::Stop::through({-20.0f, 0, 0}, {-1, 0, 0})); // the waste before x = -20
	tools::Chisel bench;
	bench.approach_deg = 20.0f;
	tools::CutPlan plan = tools::plan_cut(bench, w.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 100.0f, 0.3f, 0.0f, 1);
	CHECK(tools::limit_plan(plan, limits));
	CHECK_NEAR(plan.length, 59.5, 0.01);
	CHECK(plan.square_end && (plan.stop & tools::kAtLine));
	w.apply(plan.edits());
	CHECK(w.at(-21.0f, 0.0f) < kTop - 0.2f);  // cut up to the line
	CHECK_NEAR(w.at(-19.5f, 0.0f), kTop, 2e-3); // and not a hair past it: no lift-out
}

// A rasp worked far past a gauged floor, beside a shoulder line: down to the floor and no
// further, and nothing on the shoulder's side to keep.
TEST(a_rubbed_face_keeps_to_its_floor_and_shoulder) {
	Worked w;
	tools::Limits limits;
	limits.floors.push_back(tools::Stop::through({0, 0, kTop - 0.2f}, kUp));
	limits.sides.push_back(tools::Stop::through({0, 15.0f, 0}, {0, 1, 0})); // the waste beyond y = 15
	auto stroke = tools::rasp_stroke(tools::Rasp{}, w.work(), {-40.0f, 20.0f, kTop}, kUp, {1, 0, 0}, 60.0f, 400.0f,
			limits);
	for (int k = 0; k < 12; ++k) {
		for (int i = 1; i <= 60; ++i) {
			const float x = k % 2 == 0 ? -40.0f + float(i) : 20.0f - float(i);
			stroke->move_to({x, 20.0f, kTop});
		}
	}
	CHECK(stroke->state().limit == "at the line");
	w.apply(stroke->edits());
	for (float x : {-20.0f, 0.0f}) {
		CHECK_NEAR(w.at(x, 22.0f), kTop - 0.2f, 0.02); // the rebate: down to the floor
		CHECK_NEAR(w.at(x, 12.0f), kTop, 2e-3);        // the shoulder's side: untouched
	}
}

// A chamfer 3 mm each way, gauged on the top and the side: a chisel laid across the corner on
// the plane between the lines, pass after pass, takes the corner down to that plane and then
// takes nothing; the faces beyond the lines are untouched.
TEST(a_chamfer_pared_to_its_lines_stops_taking_wood) {
	Worked w;
	const vec3 n = gl::normalize(vec3{0, 1, 1});
	tools::Limits limits;
	limits.floors.push_back(tools::Stop::through({0, 47.0f, kTop}, n));
	tools::Chisel bench;
	bench.approach_deg = 20.0f;
	float last = 1.0f;
	for (int pass = 0; pass < 12 && last > 1e-3f; ++pass) {
		// Set on the corner as it is now.
		const vec3 corner{-79.5f, 50.0f, kTop};
		const auto on = raycast(w.body, w.octree, corner + n * 5.0f, -n, 10.0f, 1e-4f);
		CHECK(on.has_value());
		tools::CutPlan plan = tools::plan_cut(bench, w.work(), on->point, n, {1, 0, 0}, 150.0f, 0.5f, 0.0f, 1);
		tools::limit_plan(plan, limits);
		last = plan.depth;
		w.apply(plan.edits());
	}
	CHECK(last < 1e-3f); // the last pass took nothing
	for (float x : {-40.0f, 0.0f, 40.0f}) {
		CHECK_NEAR(w.at(x, 48.5f), kTop - 1.5f, 0.05); // halfway: on the plane
		CHECK_NEAR(w.at(x, 46.5f), kTop, 2e-3);        // past the line: untouched
	}
}
