#include "test.h"

#include "body/materials.h"
#include "mallet_parts.h"
#include "pieces/fit.h"
#include "plans/joint.h"
#include "plans/part.h"

#include <cmath>
#include <cstdio>

using namespace sdf;
using namespace sdf::plans;
using mallet_parts::handle;
using mallet_parts::head;
using mallet_parts::wedge;

namespace {

// A part as drawn, its cuts run well past it (so distances from inside a cut are exact).
Body solid(const Part &p) {
	return part_solid(p, mat::Oak, nullptr, 400.0f);
}

// A handle with its tenon's section `grow` mm bigger across its width and `grow_z` across
// its thickness (each split between the two sides; negative: thinner).
Part handle_with(float grow, float grow_z) {
	Part p = handle();
	p.features[0].y = {2.5f - 0.5f * grow, 32.5f + 0.5f * grow};
	p.features[0].z = {8.0f - 0.5f * grow_z, 20.0f + 0.5f * grow_z};
	return p;
}

// The handle (as `tenon`) offered to the mortise of `mortised` (the head's shape), and
// pushed in.
Fit fit_of(const Part &tenon, const Body &mortised) {
	const JointPose j = joint_pose(head(), head().features[0], tenon, tenon.features[0]);
	const Body part = solid(tenon);
	const std::vector<SurfacePoint> points = surface_points([&](vec3 p) { return part.distance(p); }, j.region, 1.0f);
	const Fit f = fit_along(points, [&](vec3 p) { return mortised.distance(p); }, j.mouth(), j.axis, j.travel, 0.5f);
	std::printf("    %s: goes %.1f of %.0f mm%s%s, most %.3f mm in, clearance %.3f, %d points, %zu steps, %.0f ms\n",
			fit_name(f.kind), f.stops_at, j.travel, f.home ? " (home)" : "", f.seated ? " seated" : "", f.most, f.clearance,
			f.points, f.steps.size(), f.ms);
	return f;
}

} // namespace

// The handle's tenon into the head's mortise: along the tenon, into the face side, its 30 mm
// along the mortise's 30; home with the shoulder on the face side, the end 3 mm proud of
// the back. The wedge into the kerf: thin end first, 38 mm to the kerf's bottom.
TEST(a_tenon_lines_up_with_its_mortise) {
	const Part h = head(), hd = handle();
	const JointPose j = joint_pose(h, h.features[0], hd, hd.features[0]);
	CHECK(j.kind == JointPose::Kind::MortiseTenon && !j.a_moves);
	CHECK(std::fabs(j.travel - 58.0f) < 1e-4f && j.axis.z > 0.999f);
	// The tenon's corners (its end, and at the shoulder) where the mortise's are.
	const vec3 end_corner = j.home.apply({0.0f, 2.5f, 8.0f}), end_far = j.home.apply({0.0f, 32.5f, 20.0f});
	const vec3 shoulder = j.home.apply({58.0f, 17.5f, 14.0f});
	std::printf("    the tenon's end from (%.1f %.1f %.1f) to (%.1f %.1f %.1f); its shoulder's middle (%.1f %.1f %.1f)\n",
			end_corner.x, end_corner.y, end_corner.z, end_far.x, end_far.y, end_far.z, shoulder.x, shoulder.y, shoulder.z);
	CHECK(std::fabs(std::fmin(end_corner.x, end_far.x) - 40.0f) < 1e-3f && std::fabs(std::fmax(end_corner.x, end_far.x) - 70.0f) < 1e-3f);
	CHECK(std::fabs(std::fmin(end_corner.y, end_far.y) - 29.0f) < 1e-3f && std::fabs(std::fmax(end_corner.y, end_far.y) - 41.0f) < 1e-3f);
	CHECK(std::fabs(end_corner.z - 58.0f) < 1e-3f && std::fabs(shoulder.z) < 1e-3f); // 3 proud of the back (55)
	CHECK(std::fabs(j.mouth().apply({0.0f, 17.5f, 14.0f}).z) < 1e-3f); // at the mouth, its end on the face
	// The same joint named the other way round: the handle still goes in.
	const JointPose k = joint_pose(hd, hd.features[0], h, h.features[0]);
	CHECK(k.kind == JointPose::Kind::MortiseTenon && k.a_moves && std::fabs(k.travel - 58.0f) < 1e-4f);

	const Part w = wedge();
	const JointPose wj = joint_pose(hd, hd.features[1], w, w.features[0]);
	const vec3 tip = wj.home.apply({45.0f, 6.0f, 0.5f});
	std::printf("    the wedge's thin end home at (%.1f %.1f %.1f)\n", tip.x, tip.y, tip.z);
	CHECK(wj.kind == JointPose::Kind::Wedge && std::fabs(wj.travel - 38.0f) < 1e-4f && wj.axis.x > 0.999f);
	CHECK(std::fabs(tip.x - 38.0f) < 1e-3f && std::fabs(tip.y - 17.5f) < 1e-3f && std::fabs(tip.z - 14.0f) < 1e-3f);
}

// As drawn, the tenon is the mortise's size: it goes home by hand and seats on its shoulder.
TEST(a_tenon_as_drawn_is_snug) {
	const Fit f = fit_of(handle(), solid(head()));
	CHECK(f.kind == Fit::Kind::Snug && f.home && f.seated);
	CHECK(f.most < 0.02f && f.clearance < 0.02f && std::fabs(f.stops_at - 58.0f) < 1e-3f);
}

// A tenon 0.2 mm fat (0.1 each side) drives home with the mallet; 1.2 mm fat, it won't go
// past the mouth; 0.4 mm thin all round, it rattles. (Interference is how far the mortise's
// wood runs into the tenon's side, along its normal.)
TEST(a_tenon_fat_or_thin) {
	const Body mortised = solid(head());
	const Fit fat = fit_of(handle_with(0.2f, 0.0f), mortised);
	CHECK(fat.kind == Fit::Kind::Drives && fat.home && std::fabs(fat.most - 0.1f) < 0.02f);
	const Fit too_fat = fit_of(handle_with(1.2f, 0.0f), mortised);
	CHECK(too_fat.kind == Fit::Kind::WontGo && !too_fat.home && too_fat.stops_at <= 1.0f); // (the surface is sampled a millimetre apart)
	const Fit thin = fit_of(handle_with(-0.4f, -0.4f), mortised);
	CHECK(thin.kind == Fit::Kind::Loose && thin.home && thin.seated && std::fabs(thin.clearance - 0.2f) < 0.03f);
}

// A mortise chopped true for 30 mm and 0.6 mm narrower each side below: the tenon stops
// 30 mm in.
TEST(a_tenon_stops_where_its_mortise_narrows) {
	Body b;
	b.base = Primitive::box({55, 35, 27.5f}, {55, 35, 27.5f}, 1.0f);
	b.base_material = mat::Oak;
	for (const auto &[lo, hi] : {std::pair{vec3(40, 29, -300), vec3(70, 41, 30)}, std::pair{vec3(40, 29.6f, 30), vec3(70, 40.4f, 300)}}) {
		Edit e;
		e.prim = Primitive::box((lo + hi) * 0.5f, (hi - lo) * 0.5f);
		e.op = Op::Subtract;
		b.add(e);
	}
	const Fit f = fit_of(handle(), b);
	CHECK(f.kind == Fit::Kind::WontGo && !f.home && std::fabs(f.stops_at - 30.0f) < 1.0f);
}
