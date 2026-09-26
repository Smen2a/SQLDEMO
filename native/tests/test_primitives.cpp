#include "test.h"

using namespace sdf;

TEST(primitives_known_distances) {
	const Primitive sphere = Primitive::sphere({0, 0, 0}, 2);
	CHECK_NEAR(sphere.eval({3, 0, 0}), 1.0, 1e-6);
	CHECK_NEAR(sphere.eval({0, 0, 0}), -2.0, 1e-6);

	const Primitive box = Primitive::box({0, 0, 0}, {1, 2, 3});
	CHECK_NEAR(box.eval({2, 0, 0}), 1.0, 1e-6);
	CHECK_NEAR(box.eval({0, 0, 0}), -1.0, 1e-6);
	CHECK_NEAR(box.eval({2, 3, 0}), std::sqrt(2.0), 1e-6);

	// Rounding keeps the faces in place and only eases the edges.
	const Primitive rounded = Primitive::box({0, 0, 0}, {1, 1, 1}, 0.25f);
	CHECK_NEAR(rounded.eval({2, 0, 0}), 1.0, 1e-6);
	CHECK_NEAR(rounded.eval({1, 1, 0}), std::sqrt(2.0) * 0.25 - 0.25, 1e-6);

	// 90 degrees about z swaps the x and y extents.
	const Primitive turned = Primitive::box({0, 0, 0}, {1, 2, 3}, 0, quat_axis_angle({0, 0, 1}, 1.5707963f));
	CHECK_NEAR(turned.eval({3, 0, 0}), 1.0, 1e-5);
	CHECK_NEAR(turned.eval({0, 2, 0}), 1.0, 1e-5);

	const Primitive cyl = Primitive::cylinder({0, 0, 0}, 1, 2);
	CHECK_NEAR(cyl.eval({0, 3, 0}), 1.0, 1e-6);
	CHECK_NEAR(cyl.eval({2, 0, 0}), 1.0, 1e-6);

	const Primitive cap = Primitive::capsule({0, 0, 0}, {0, 4, 0}, 1);
	CHECK_NEAR(cap.eval({0, 6, 0}), 1.0, 1e-6);
	CHECK_NEAR(cap.eval({2, 2, 0}), 1.0, 1e-6);

	const Primitive plane = Primitive::plane({0, 0, 1}, 0);
	CHECK_NEAR(plane.eval({1, 2, 5}), 5.0, 1e-6);
}

TEST(primitives_are_lipschitz_1) {
	t::Rng rng(1);
	for (int i = 0; i < 40; ++i) {
		const vec3 c = rng.vec(-2, 2);
		const Primitive prims[] = {
			Primitive::sphere(c, rng.uniform(0.5f, 3)),
			Primitive::box(c, rng.vec(0.5f, 3), rng.uniform(0, 0.4f), rng.rotation()),
			Primitive::cylinder(c, rng.uniform(0.5f, 2), rng.uniform(0.5f, 3), rng.uniform(0, 0.4f), rng.rotation()),
			Primitive::capsule(c, c + rng.vec(-3, 3), rng.uniform(0.3f, 1.5f)),
			Primitive::plane(rng.unit(), rng.uniform(-1, 1)),
		};
		for (const Primitive &p : prims) {
			const float l = t::sampled_lipschitz([&](vec3 q) { return p.eval(q); }, c, 5, 400, rng);
			CHECK_LE(l, 1.001);
		}
	}
}

TEST(tool_profiles) {
	using gl::sdf_tool_profile;
	const ToolProfile flat = ToolProfile::flat(4, 10);
	CHECK_NEAR(sdf_tool_profile({0, -1}, flat.kind, flat.params), 1.0, 1e-6);
	CHECK_NEAR(sdf_tool_profile({3, 5}, flat.kind, flat.params), 1.0, 1e-6);
	CHECK_NEAR(sdf_tool_profile({0, 5}, flat.kind, flat.params), -2.0, 1e-6);

	// 60 degree V-tool: walls at 30 degrees either side of vertical.
	const ToolProfile vee = ToolProfile::v_tool(60, 10);
	const float tan30 = std::tan(0.5235988f);
	CHECK_NEAR(sdf_tool_profile({0, -1}, vee.kind, vee.params), 1.0, 1e-6);
	CHECK_NEAR(sdf_tool_profile({tan30 * 5, 5}, vee.kind, vee.params), 0.0, 1e-5);
	CHECK_NEAR(sdf_tool_profile({0, 5}, vee.kind, vee.params), -2.5, 1e-5);
	CHECK_NEAR(vee.extent(), 10 / std::cos(0.5235988), 1e-4);

	const ToolProfile gouge = ToolProfile::gouge(5, 10, 10);
	CHECK_NEAR(sdf_tool_profile({0, -1}, gouge.kind, gouge.params), 1.0, 1e-6);
	CHECK_NEAR(sdf_tool_profile({0, 0}, gouge.kind, gouge.params), 0.0, 1e-6);
	CHECK_NEAR(sdf_tool_profile({6, 7}, gouge.kind, gouge.params), 1.0, 1e-6);
}

// A flat chisel pushed along x removes exactly a box.
TEST(segment_sweep_is_exact) {
	t::Rng rng(2);
	const Primitive sweep = Primitive::sweep({-5, 0, 0}, {5, 0, 0}, {0, 0, 1}, ToolProfile::flat(4, 6));
	const Primitive box = Primitive::box({0, 0, 3}, {5, 2, 3});
	for (int i = 0; i < 2000; ++i) {
		const vec3 p = rng.vec(-9, 9);
		CHECK_NEAR(sweep.eval(p), box.eval(p), 1e-4);
	}
}

TEST(segment_sweeps_are_lipschitz_1) {
	t::Rng rng(3);
	const ToolProfile tools[] = {ToolProfile::flat(3, 4), ToolProfile::v_tool(60, 5), ToolProfile::gouge(4, 6, 5)};
	for (int i = 0; i < 30; ++i) {
		const vec3 a = rng.vec(-4, 4);
		const vec3 b = a + rng.unit() * rng.uniform(2, 8);
		for (const ToolProfile &tool : tools) {
			const Primitive s = Primitive::sweep(a, b, rng.unit(), tool);
			const float l = t::sampled_lipschitz([&](vec3 q) { return s.eval(q); }, (a + b) * 0.5f, 8, 400, rng);
			CHECK_LE(l, 1.001);
		}
	}
}

// A Bezier whose control points are collinear is just the straight stroke.
TEST(straight_bezier_matches_segment) {
	t::Rng rng(4);
	const ToolProfile vee = ToolProfile::v_tool(60, 5);
	const vec3 a(-6, 0, 1), b(6, 0, -1), up(0, 0.2f, 1);
	const Primitive seg = Primitive::sweep(a, b, up, vee);
	const Primitive even = Primitive::sweep(a, (a + b) * 0.5f, b, up, vee);
	const Primitive uneven = Primitive::sweep(a, gl::mix(a, b, 0.3f), b, up, vee);
	for (int i = 0; i < 2000; ++i) {
		const vec3 p = rng.vec(-9, 9);
		CHECK_NEAR(even.eval(p), seg.eval(p), 1e-3);
		// Uneven spacing changes the interior axial estimate but not the surface.
		if (std::fabs(seg.eval(p)) > 1e-2f) {
			CHECK((uneven.eval(p) < 0) == (seg.eval(p) < 0));
		}
	}
}

TEST(curved_sweeps_respect_declared_bound) {
	t::Rng rng(5);
	const ToolProfile tools[] = {ToolProfile::v_tool(60, 3), ToolProfile::gouge(3, 5, 3)};
	for (int i = 0; i < 30; ++i) {
		// Gentle curves in the surface plane: curvature radius well above the profile size.
		const vec3 a(-10, rng.uniform(-2, 2), 0);
		const vec3 c(10, rng.uniform(-2, 2), 0);
		const vec3 ctrl(rng.uniform(-3, 3), rng.uniform(-6, 6), rng.uniform(-1, 1));
		for (const ToolProfile &tool : tools) {
			const Primitive s = Primitive::sweep(a, ctrl, c, {0, 0, 1}, tool);
			const float bound = s.lipschitz();
			CHECK_LE(bound, 2.0);
			const float l = t::sampled_lipschitz([&](vec3 q) { return s.eval(q); }, {0, 0, 1}, 6, 600, rng);
			CHECK_LE(l, bound * 1.01);
		}
	}
}

TEST(primitive_bounds_contain_surface) {
	t::Rng rng(6);
	for (int i = 0; i < 30; ++i) {
		const Primitive prims[] = {
			Primitive::box(rng.vec(-2, 2), rng.vec(0.5f, 3), 0.1f, rng.rotation()),
			Primitive::cylinder(rng.vec(-2, 2), 1.5f, 2.5f, 0, rng.rotation()),
			Primitive::sweep(rng.vec(-3, 3), rng.vec(-3, 3), rng.unit(), ToolProfile::gouge(2, 3, 2)),
			Primitive::sweep({-5, 0, 0}, rng.vec(-2, 2), {5, 0, 0}, {0, 0, 1}, ToolProfile::v_tool(45, 2)),
		};
		for (const Primitive &p : prims) {
			const Aabb b = p.bounds();
			for (int k = 0; k < 400; ++k) {
				const vec3 q = rng.vec(-12, 12);
				const bool outside = q.x < b.lo.x || q.y < b.lo.y || q.z < b.lo.z || q.x > b.hi.x ||
						q.y > b.hi.y || q.z > b.hi.z;
				if (outside) {
					CHECK(p.eval(q) > 0);
				}
			}
		}
	}
}
