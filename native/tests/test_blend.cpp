#include "test.h"

using namespace sdf;

namespace {

struct ModeCase {
	Blend mode;
	EdgeProfile shape;
	const char *name;
};

const ModeCase kModes[] = {
	{Blend::Hard, EdgeProfile::ArcConcave, "hard"},
	{Blend::Chamfer, EdgeProfile::ArcConcave, "chamfer"},
	{Blend::Round, EdgeProfile::ArcConcave, "round"},
	{Blend::Smooth, EdgeProfile::ArcConcave, "smooth"},
	{Blend::SmoothC2, EdgeProfile::ArcConcave, "smooth_c2"},
	{Blend::Profile, EdgeProfile::ArcConcave, "profile_arc_concave"},
	{Blend::Profile, EdgeProfile::ArcConvex, "profile_arc_convex"},
	{Blend::Profile, EdgeProfile::Ogee, "profile_ogee"},
};

vec2 blend_union(const ModeCase &m, float a, float b, float r, float r2) {
	return gl::sdf_union(a, b, int(m.mode), r, r2, int(m.shape));
}

bool is_rigid(Blend mode) {
	return mode != Blend::Smooth && mode != Blend::SmoothC2;
}

// Chamfer, round and profile need the same radius on both sides to be symmetric.
bool uses_r2(Blend mode) {
	return mode == Blend::Chamfer || mode == Blend::Profile;
}

Primitive random_solid(t::Rng &rng) {
	const vec3 c = rng.vec(-1.5f, 1.5f);
	switch (int(rng.uniform(0, 3))) {
		case 0:
			return Primitive::sphere(c, rng.uniform(1, 2.5f));
		case 1:
			return Primitive::box(c, rng.vec(0.6f, 2.2f), 0, rng.rotation());
		default:
			return Primitive::capsule(c, c + rng.vec(-2, 2), rng.uniform(0.5f, 1.5f));
	}
}

} // namespace

TEST(blends_reduce_to_hard_as_radius_vanishes) {
	t::Rng rng(10);
	for (const ModeCase &m : kModes) {
		for (int i = 0; i < 2000; ++i) {
			const float a = rng.uniform(-5, 5), b = rng.uniform(-5, 5);
			const vec2 u = blend_union(m, a, b, 1e-7f, 1e-7f);
			CHECK_NEAR(u.x, std::min(a, b), 1e-5);
		}
	}
}

// Outside the support radius used for culling, every mode must produce the hard result's
// surface; rigid modes must reproduce its distance exactly too.
TEST(blends_have_compact_support) {
	t::Rng rng(11);
	for (const ModeCase &m : kModes) {
		for (int i = 0; i < 20000; ++i) {
			const float r = rng.uniform(0.2f, 2);
			const float r2 = uses_r2(m.mode) ? rng.uniform(0.2f, 2) : r;
			Edit e;
			e.op = Op::Union;
			e.blend = m.mode;
			e.r = r;
			e.r2 = r2;
			e.prim = Primitive::sphere({0, 0, 0}, 0); // bounds-free: we only want the radius
			const float support = e.influence();
			const float a = rng.uniform(-3, 12), b = rng.uniform(-3, 12);
			if (std::max(a, b) < support) {
				continue;
			}
			const float blended = blend_union(m, a, b, r, r2).x;
			const float hard = std::min(a, b);
			if (is_rigid(m.mode)) {
				CHECK_NEAR(blended, hard, 1e-4);
			} else {
				CHECK((blended < 0) == (hard < 0));
			}
		}
	}
}

TEST(blend_lipschitz_bounds_hold) {
	t::Rng rng(12);
	const Op ops[] = {Op::Union, Op::Subtract, Op::Intersect};
	for (const ModeCase &m : kModes) {
		for (Op op : ops) {
			for (int i = 0; i < 25; ++i) {
				Body body;
				body.base = random_solid(rng);
				Edit e;
				e.prim = random_solid(rng);
				e.op = op;
				e.blend = m.mode;
				e.r = rng.uniform(0.2f, 1.2f);
				e.r2 = uses_r2(m.mode) ? rng.uniform(0.2f, 1.2f) : e.r;
				e.shape = m.shape;
				body.edits.push_back(e);
				const float l = t::sampled_lipschitz([&](vec3 p) { return body.distance(p); }, {0, 0, 0}, 4, 1500, rng);
				CHECK_LE(l, body.lipschitz() * 1.001);
			}
		}
	}
}

TEST(blend_weights_are_well_formed) {
	t::Rng rng(13);
	for (const ModeCase &m : kModes) {
		for (int i = 0; i < 5000; ++i) {
			const float a = rng.uniform(-3, 3), b = rng.uniform(-3, 3);
			const float r = rng.uniform(0.1f, 2);
			const vec2 u = blend_union(m, a, b, r, r);
			CHECK(u.y >= 0.0f && u.y <= 1.0f);
			if (is_rigid(m.mode)) {
				CHECK(u.y == 0.0f || u.y == 1.0f);
			}
			// Symmetric radii make the distance symmetric in its operands — except the
			// ogee, whose S-curve is directional by design.
			if (m.shape != EdgeProfile::Ogee) {
				CHECK_NEAR(blend_union(m, b, a, r, r).x, u.x, 1e-5);
			}
		}
	}
	// The polynomial blends split material evenly exactly on the medial surface.
	CHECK_NEAR(gl::sdf_union(0.3f, 0.3f, gl::SDF_BLEND_SMOOTH, 1, 1, 0).y, 0.5, 1e-6);
	CHECK_NEAR(gl::sdf_union(0.3f, 0.3f, gl::SDF_BLEND_SMOOTH_C2, 1, 1, 0).y, 0.5, 1e-6);
}

TEST(round_matches_circular_fillet) {
	// Two perpendicular half-spaces x<0 and y<0: the round union's surface in the corner
	// is the quarter circle of radius r centred at (r, r).
	const float r = 1.5f;
	for (int i = 0; i <= 16; ++i) {
		const float ang = 3.1415926f * (1.0f + 0.5f * float(i) / 16.0f);
		const float x = r + r * std::cos(ang), y = r + r * std::sin(ang);
		CHECK_NEAR(gl::sdf_union(x, y, gl::SDF_BLEND_ROUND, r, r, 0).x, 0.0, 1e-5);
	}
}

TEST(chamfer_bevel_hits_its_widths) {
	// Asymmetric chamfer: the bevel runs from (r, 0) to (0, r2) in the operands' plane.
	const float r = 1.0f, r2 = 3.0f;
	CHECK_NEAR(gl::sdf_union(r, 0, gl::SDF_BLEND_CHAMFER, r, r2, 0).x, 0.0, 1e-6);
	CHECK_NEAR(gl::sdf_union(0, r2, gl::SDF_BLEND_CHAMFER, r, r2, 0).x, 0.0, 1e-6);
	CHECK(gl::sdf_union(0.4f, 1.2f, gl::SDF_BLEND_CHAMFER, r, r2, 0).x < 0);
}

TEST(edge_profiles_pass_through_their_endpoints) {
	const int shapes[] = {gl::SDF_PROFILE_ARC_CONCAVE, gl::SDF_PROFILE_ARC_CONVEX, gl::SDF_PROFILE_OGEE};
	for (int shape : shapes) {
		const float r = 2.0f, r2 = 0.5f;
		CHECK_NEAR(gl::sdf_union(r, 0, gl::SDF_BLEND_PROFILE, r, r2, shape).x, 0.0, 1e-5);
		CHECK_NEAR(gl::sdf_union(0, r2, gl::SDF_BLEND_PROFILE, r, r2, shape).x, 0.0, 1e-5);
	}
	// Convex fills more of the corner than concave; ogee sits in between at the midpoint.
	const float mid_concave = gl::sdf_union(0.5f, 0.5f, gl::SDF_BLEND_PROFILE, 1, 1, gl::SDF_PROFILE_ARC_CONCAVE).x;
	const float mid_convex = gl::sdf_union(0.5f, 0.5f, gl::SDF_BLEND_PROFILE, 1, 1, gl::SDF_PROFILE_ARC_CONVEX).x;
	const float mid_ogee = gl::sdf_union(0.5f, 0.5f, gl::SDF_BLEND_PROFILE, 1, 1, gl::SDF_PROFILE_OGEE).x;
	CHECK(mid_concave > 0);
	CHECK(mid_convex < 0);
	CHECK_NEAR(mid_ogee, 0.0, 1e-5);
}

TEST(guide_operators_cut_where_expected) {
	// Body: half-space z < 0. Guide: the plane x = 0.
	auto apply = [](int op, vec3 p, float r, float r2) {
		return gl::sdf_apply_edit(p.z, vec3(0), p.x, op, 0, r, r2, 0, 0).x;
	};
	CHECK(apply(gl::SDF_OP_ENGRAVE, {0, 0, -0.99f}, 1, 0) > 0);
	CHECK(apply(gl::SDF_OP_ENGRAVE, {0, 0, -1.01f}, 1, 0) < 0);
	CHECK(apply(gl::SDF_OP_ENGRAVE, {3, 0, -0.01f}, 1, 0) < 0);

	CHECK(apply(gl::SDF_OP_GROOVE, {0, 0, -1.9f}, 2, 0.5f) > 0);
	CHECK(apply(gl::SDF_OP_GROOVE, {0, 0, -2.1f}, 2, 0.5f) < 0);
	CHECK(apply(gl::SDF_OP_GROOVE, {0.6f, 0, -1}, 2, 0.5f) < 0);

	CHECK(apply(gl::SDF_OP_TONGUE, {0, 0, 1.9f}, 2, 0.5f) < 0);
	CHECK(apply(gl::SDF_OP_TONGUE, {0, 0, 2.1f}, 2, 0.5f) > 0);
	CHECK(apply(gl::SDF_OP_TONGUE, {0.6f, 0, 1}, 2, 0.5f) > 0);
}

TEST(material_mix_keeps_two_heaviest) {
	auto weight_of = [](vec3 st, float id) {
		float w = 0;
		if (st.x == id) w += 1 - st.z;
		if (st.y == id) w += st.z;
		return w;
	};
	vec3 st(0, 0, 0);
	st = gl::sdf_mat_mix(st, 1, 0.3f);
	CHECK_NEAR(weight_of(st, 0), 0.7, 1e-6);
	CHECK_NEAR(weight_of(st, 1), 0.3, 1e-6);
	// Mixing a third material drops the lightest (id 1: 0.15 vs 0.35 and 0.5).
	st = gl::sdf_mat_mix(st, 2, 0.5f);
	CHECK_NEAR(weight_of(st, 1), 0.0, 1e-6);
	CHECK_NEAR(weight_of(st, 0), 0.35 / 0.85, 1e-5);
	CHECK_NEAR(weight_of(st, 2), 0.5 / 0.85, 1e-5);
	// Re-mixing a material already present only shifts weight.
	const vec3 again = gl::sdf_mat_mix(st, 2, 1.0f);
	CHECK_NEAR(weight_of(again, 2), 1.0, 1e-6);
}
