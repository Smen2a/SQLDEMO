#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/catalog.h"
#include "tools/cutting.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

constexpr float kTop = 12.5f;
constexpr float kDeg = 3.14159265f / 180.0f;
const vec3 kUp{0, 0, 1};

struct Board {
	Body body = demo::board(mat::Ash);
	Octree octree;
	MaterialTable materials = MaterialTable::standard();

	Board() { octree.build(body); }
	Work work() const { return {body, octree, materials}; }
	void add(const std::vector<Edit> &edits) {
		for (const Edit &e : edits) {
			CHECK(body.add(e));
		}
		octree.build(body);
	}
	float height(float x, float y) const {
		const auto hit = raycast(body, octree, {x, y, 60.0f}, {0, 0, -1}, 200.0f, 1e-4f);
		return hit ? hit->point.z : std::nanf("");
	}
};

Chisel bench(float angle, const char *id = "bench_12") {
	Chisel c = find_chisel(id)->chisel; // (a 27 degree bevel)
	c.approach_deg = angle;
	return c;
}

// The stroke's edge pushed along its segment to `s` mm, half a millimetre at a time.
void push(SteeredStroke &stroke, float s) {
	const CutPlan &p = stroke.plan();
	for (float at = stroke.reached() + 0.5f; at < s; at += 0.5f) {
		stroke.move_to(p.start + p.path * at);
	}
	stroke.move_to(p.start + p.path * s);
}

int sweeps(const std::vector<Edit> &edits) {
	int n = 0;
	for (const Edit &e : edits) {
		n += e.prim.type == Prim::SweepSegment || e.prim.type == Prim::SweepBezier;
	}
	return n;
}

} // namespace

// Steered on the same way at the same attitude part way along, a stroke cuts just what the
// one plan would, in as many sweeps.
TEST(a_stroke_steered_on_its_way_cuts_as_its_plan) {
	Board single, steered;
	const Chisel c = bench(30.0f);
	const vec3 start{-79.5f, 0.0f, kTop};
	const CutPlan plan = plan_cut(c, single.work(), start, kUp, {1, 0, 0}, 120.0f, 0.3f, 0.0f, 1);
	single.add(plan.edits(120.0f, false));

	SteeredStroke stroke(plan_cut(c, steered.work(), start, kUp, {1, 0, 0}, 120.0f, 0.3f, 0.0f, 1));
	push(stroke, 40.0f);
	const SteeredStroke::Going going = stroke.going_on(steered.work(), kUp);
	CHECK_NEAR(going.entry, 0.3, 1e-3);
	stroke.steer(plan_cut(c, steered.work(), going.start, kUp, {1, 0, 0}, 80.0f, 0.3f, 0.0f, 1, 1.0f, going.entry));
	push(stroke, 80.0f);
	const std::vector<Edit> edits = stroke.edits();
	std::printf("    %d sweeps steered, %d planned\n", sweeps(edits), sweeps(plan.edits(120.0f, false)));
	CHECK(sweeps(edits) <= sweeps(plan.edits(120.0f, false)));
	steered.add(edits);
	for (float x = -75.0f; x <= 38.0f; x += 7.0f) {
		for (float y : {-5.0f, 0.0f, 5.0f}) {
			CHECK_NEAR(steered.height(x, y), single.height(x, y), 0.01);
		}
	}
}

// Going on from a cut 0.3 mm deep, the bevel steers the edge (a 6 mm bench chisel's, 27
// degrees): tipped 6 degrees past its bevel it dives at tan 6, to the depth asked; held on
// its bevel it runs level; lowered 6 degrees under it, it rises at tan 6 and lifts out
// 2.9 mm on.
TEST(the_bevel_steers_a_cut_that_goes_on) {
	Board b;
	const vec3 start{-40.0f, 0.0f, kTop};
	const CutPlan dives = plan_cut(bench(33.0f, "bench_6"), b.work(), start, kUp, {1, 0, 0}, 40.0f, 0.6f, 0.0f, 1, 1.0f, 0.3f);
	const CutPlan level = plan_cut(bench(27.0f, "bench_6"), b.work(), start, kUp, {1, 0, 0}, 40.0f, 0.6f, 0.0f, 1, 1.0f, 0.3f);
	const CutPlan lifts = plan_cut(bench(21.0f, "bench_6"), b.work(), start, kUp, {1, 0, 0}, 40.0f, 0.6f, 0.0f, 1, 1.0f, 0.3f);
	const float rate = std::tan(6.0f * kDeg);
	std::printf("    dives: %.3f mm at 2 mm, %.3f at 30; level: %.3f at 20; lifts out at %.2f mm\n",
			double(dives.depth_at(2.0f)), double(dives.depth_at(30.0f)), double(level.depth_at(20.0f)),
			double(lifts.length));
	CHECK(dives.open && std::fabs(dives.depth_at(0.0f) - 0.3f) < 1e-4f);
	CHECK_NEAR(dives.depth_at(2.0f), 0.3 + 2.0 * rate, 0.01);
	CHECK_NEAR(dives.depth_at(30.0f), 0.6, 1e-3);
	CHECK_NEAR(level.depth_at(20.0f), 0.3, 1e-3);
	CHECK(!(level.warnings & kSkates));
	CHECK((lifts.warnings & kLifts) && (lifts.stop & kLifts));
	CHECK_NEAR(lifts.length, 0.3 / rate, 0.05);
	CHECK_NEAR(lifts.depth_at(1.0f), 0.3 - rate, 0.01);
	Edit out;
	CHECK(!lifts.lift_out(lifts.length, out)); // (it has left the wood: nothing to lift out)
}

// Steered round a 90 degree arc of 40 mm radius (a 6 mm bench chisel, 0.3 mm deep: as a
// hand pushes it along the grain and across it), towards a point 3 degrees further on each
// time the edge reaches the last: the cut follows the arc (its middle within 0.5 mm), at its
// depth, in a few sweeps.
TEST(a_stroke_steered_round_a_curve_follows_it) {
	Board b;
	const vec3 centre{-40.0f, -40.0f, kTop};
	const float radius = 40.0f;
	auto on_arc = [&](float deg) { return centre + vec3(std::cos(deg * kDeg), std::sin(deg * kDeg), 0.0f) * radius; };
	const Chisel c = bench(33.0f, "bench_6");
	vec3 aim = on_arc(87.0f);
	SteeredStroke stroke(plan_cut(c, b.work(), on_arc(90.0f), kUp, gl::normalize(aim - on_arc(90.0f)), 300.0f, 0.3f,
			0.0f, 1));
	double planning = 0.0;
	for (float deg = 84.0f; deg >= -0.1f; deg -= 3.0f) {
		push(stroke, gl::dot(aim - stroke.plan().start, stroke.plan().path));
		const auto t0 = std::chrono::steady_clock::now();
		const SteeredStroke::Going going = stroke.going_on(b.work(), kUp);
		const vec3 next = on_arc(deg);
		vec3 way = next - going.start;
		way.z = 0.0f;
		stroke.steer(plan_cut(c, b.work(), going.start, kUp, gl::normalize(way), 300.0f, 0.3f, 0.0f, 1, 1.0f,
				going.entry));
		planning += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		aim = next;
	}
	push(stroke, gl::dot(aim - stroke.plan().start, stroke.plan().path));
	const auto t0 = std::chrono::steady_clock::now();
	const std::vector<Edit> edits = stroke.edits();
	const double merging = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("    %zu segments in %d sweeps (%zu edits with chips); a steer planned in %.1f ms, the edits merged in "
				"%.2f ms\n",
			stroke.segments(), sweeps(edits), edits.size(), planning / double(stroke.segments() - 1), merging);
	CHECK(sweeps(edits) <= 6);
	b.add(edits);
	float worst = 0.0f;
	for (const float deg : {80.0f, 60.0f, 45.0f, 30.0f, 10.0f}) {
		const vec3 out{std::cos(deg * kDeg), std::sin(deg * kDeg), 0.0f};
		float first = -1.0f, last = -1.0f;
		for (float r = radius - 6.0f; r <= radius + 6.0f; r += 0.05f) {
			const vec3 q = centre + out * r;
			if (b.height(q.x, q.y) < kTop - 0.15f) {
				first = first < 0.0f ? r : first;
				last = r;
			}
		}
		const vec3 on = centre + out * radius;
		std::printf("    at %2.0f deg: cut from %.2f to %.2f mm out (its middle %.2f), %.3f deep on the arc\n", double(deg),
				double(first), double(last), double(0.5f * (first + last)), double(kTop - b.height(on.x, on.y)));
		CHECK(first > 0.0f);
		worst = std::max(worst, std::fabs(0.5f * (first + last) - radius));
		CHECK_NEAR(kTop - b.height(on.x, on.y), 0.3, 0.03);
	}
	CHECK(worst < 0.5f);
}
