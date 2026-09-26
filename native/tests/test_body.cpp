#include "test.h"

#include <fstream>
#include <iterator>
#include <sstream>

using namespace sdf;

namespace {

// A 150 x 100 x 20 mm panel, top face at z = 10.
Body panel() {
	Body b;
	b.base = Primitive::box({0, 0, 0}, {75, 50, 10});
	b.base_material = 0;
	return b;
}

Edit v_cut(vec3 a, vec3 b, float depth_below_top) {
	Edit e;
	e.prim = Primitive::sweep({a.x, a.y, 10 - depth_below_top}, {b.x, b.y, 10 - depth_below_top}, {0, 0, 1},
			ToolProfile::v_tool(60, 20));
	e.op = Op::Subtract;
	return e;
}

std::string slurp(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

} // namespace

TEST(v_cut_removes_a_groove) {
	Body b = panel();
	b.add(v_cut({-80, 0, 0}, {80, 0, 0}, 3));
	CHECK(b.distance({0, 0, 8}) > 0);     // inside the groove: removed
	CHECK(b.distance({0, 0, 6.5f}) < 0);  // below the groove bottom (z = 7)
	CHECK(b.distance({0, 3, 9}) < 0);     // beside the groove: half-width at z = 9 is 1.15
	CHECK_NEAR(b.distance({0, 0, 7}), 0.0, 1e-4);
	// Cut faces stay the body's material.
	const Sample s = b.sample({0, 1.2f, 9});
	CHECK(s.m0 == 0 && s.m1 == 0);
}

TEST(round_subtract_eases_the_arris) {
	// A square channel with a 1 mm rounded edge where it meets the top face.
	Body hard = panel(), eased = panel();
	Edit e;
	e.prim = Primitive::sweep({-80, 0, 6}, {80, 0, 6}, {0, 0, 1}, ToolProfile::flat(6, 20));
	e.op = Op::Subtract;
	hard.add(e);
	e.blend = Blend::Round;
	e.r = e.r2 = 1.0f;
	eased.add(e);
	// Just inside the sharp corner at (y = 3, z = 10), the hard cut keeps material and the
	// rounded one has removed it.
	const vec3 corner(0, 3.15f, 9.85f);
	CHECK(hard.distance(corner) < 0);
	CHECK(eased.distance(corner) > 0);
	// Away from the corner both agree.
	CHECK_NEAR(hard.distance({0, 10, 8}), eased.distance({0, 10, 8}), 1e-5);
	CHECK_NEAR(hard.distance({0, 0, 3}), eased.distance({0, 0, 3}), 1e-5);
}

TEST(smooth_union_mixes_materials_and_hard_selects) {
	Body b = panel();
	Edit putty;
	putty.prim = Primitive::sphere({0, 0, 10}, 6);
	putty.op = Op::Union;
	putty.blend = Blend::Smooth;
	putty.r = 4;
	putty.material = 2;
	b.add(putty);

	// Deep in the putty: all putty. Far away on the panel: all wood.
	const Sample top = b.sample({0, 0, 16});
	CHECK_NEAR(top.m1 == 2 ? top.t : 1 - top.t, 1.0, 1e-4);
	const Sample far = b.sample({60, 40, 10});
	CHECK(far.m0 == 0 && far.t == 0);

	// Somewhere across the seam the weights are genuinely mixed.
	bool mixed = false;
	for (float y = 4; y < 12; y += 0.1f) {
		const Sample s = b.sample({0, y, 10.5f});
		const float w_putty = (s.m0 == 2 ? 1 - s.t : 0) + (s.m1 == 2 ? s.t : 0);
		mixed |= w_putty > 0.2f && w_putty < 0.8f;
	}
	CHECK(mixed);

	// The same union with a hard blend never mixes.
	putty.blend = Blend::Hard;
	b.replace(0, putty);
	for (float y = 4; y < 12; y += 0.1f) {
		const Sample s = b.sample({0, y, 10.5f});
		CHECK(s.t == 0 || s.t == 1);
	}
}

TEST(paint_changes_material_not_shape) {
	Body b = panel();
	Edit paint;
	paint.prim = Primitive::sphere({20, 0, 10}, 5);
	paint.op = Op::Paint;
	paint.material = 7;
	b.add(paint);
	const vec3 inside(20, 0, 9.5f), outside(40, 0, 9.5f);
	CHECK_NEAR(b.distance(inside), -0.5, 1e-5);
	const Sample s = b.sample(inside);
	CHECK((s.m0 == 7 && s.t == 0) || (s.m1 == 7 && s.t == 1));
	CHECK(b.sample(outside).m0 == 0);

	// A soft transition gives intermediate weights across its width.
	paint.r = 2;
	b.replace(0, paint);
	const Sample edge = b.sample({25, 0, 9.5f}); // guide distance ~0 here
	CHECK(edge.t > 0.2f && edge.t < 0.8f);
}

// Whole bodies built from realistic carving edits stay within the declared bound.
TEST(carved_body_lipschitz_bound_holds) {
	t::Rng rng(20);
	const Blend blends[] = {Blend::Hard, Blend::Round, Blend::Chamfer, Blend::Smooth};
	for (int trial = 0; trial < 6; ++trial) {
		Body b = panel();
		for (int i = 0; i < 25; ++i) {
			const vec3 a(rng.uniform(-60, 60), rng.uniform(-40, 40), 0);
			const vec3 c = a + vec3(rng.uniform(-20, 20), rng.uniform(-20, 20), 0);
			const vec3 ctrl = (a + c) * 0.5f + vec3(rng.uniform(-4, 4), rng.uniform(-4, 4), 0);
			const float depth = rng.uniform(1, 5);
			const ToolProfile tools[] = {ToolProfile::v_tool(60, 20), ToolProfile::gouge(4, 6, 20),
				ToolProfile::flat(5, 20)};
			Edit e;
			e.prim = Primitive::sweep({a.x, a.y, 10 - depth}, {ctrl.x, ctrl.y, 10 - depth}, {c.x, c.y, 10 - depth},
					{0, 0, 1}, tools[i % 3]);
			e.op = Op::Subtract;
			e.blend = blends[i % 4];
			e.r = e.r2 = rng.uniform(0.2f, 1.0f);
			b.add(e);
		}
		const float bound = b.lipschitz();
		const float l = t::sampled_lipschitz([&](vec3 p) { return b.distance(p); }, {0, 0, 8}, 50, 20000, rng, 0.05f);
		CHECK_LE(l, bound * 1.001);
	}
}

// The per-point culling in Body::sample must never change the sign of the field, and must
// reproduce the exhaustive value exactly wherever the skipped primitives are exact SDFs.
TEST(culled_sampling_matches_exhaustive) {
	t::Rng rng(21);
	const Op ops[] = {Op::Union, Op::Subtract, Op::Engrave, Op::Groove, Op::Tongue, Op::Paint};
	const Blend blends[] = {Blend::Hard, Blend::Chamfer, Blend::Round, Blend::Smooth, Blend::SmoothC2, Blend::Profile};
	for (int trial = 0; trial < 8; ++trial) {
		// Exact primitives only (spheres, boxes, capsules, flat sweeps), so values must match.
		Body exact = panel();
		// Everything, including bound-type sweeps (V, gouge, curves), so only signs must match.
		Body any = panel();
		for (int i = 0; i < 40; ++i) {
			Edit e;
			e.op = ops[i % 6];
			e.blend = blends[(i / 6) % 6];
			e.r = rng.uniform(0.2f, 2.5f);
			e.r2 = rng.uniform(0.2f, 2.5f);
			e.shape = EdgeProfile(i % 3);
			e.material = std::uint16_t(1 + i % 4);
			const vec3 c(rng.uniform(-70, 70), rng.uniform(-45, 45), rng.uniform(4, 14));
			switch (i % 4) {
				case 0: e.prim = Primitive::sphere(c, rng.uniform(2, 8)); break;
				case 1: e.prim = Primitive::box(c, rng.vec(1, 6), rng.uniform(0, 0.5f), rng.rotation()); break;
				case 2: e.prim = Primitive::capsule(c, c + rng.vec(-10, 10), rng.uniform(1, 3)); break;
				default:
					e.prim = Primitive::sweep(c, c + rng.vec(-20, 20), {0, 0, 1}, ToolProfile::flat(rng.uniform(2, 6), 20));
			}
			exact.add(e);
			if (i % 3 == 0) {
				e.prim = Primitive::sweep(c, c + rng.vec(-8, 8), c + rng.vec(-20, 20), {0, 0, 1},
						i % 2 ? ToolProfile::v_tool(60, 20) : ToolProfile::gouge(3, 5, 20));
			}
			any.add(e);
		}
		for (int k = 0; k < 20000; ++k) {
			const vec3 p(rng.uniform(-85, 85), rng.uniform(-60, 60), rng.uniform(-15, 25));
			const Sample a = exact.sample(p), b = exact.sample_exhaustive(p);
			CHECK_NEAR(a.d, b.d, 1e-4);
			const float wa = (a.m0 == 1 ? 1 - a.t : 0) + (a.m1 == 1 ? a.t : 0);
			const float wb = (b.m0 == 1 ? 1 - b.t : 0) + (b.m1 == 1 ? b.t : 0);
			CHECK_NEAR(wa, wb, 1e-4);
			const float c1 = any.distance(p), c2 = any.sample_exhaustive(p).d;
			if (std::fabs(c2) > 1e-4f) {
				CHECK((c1 < 0) == (c2 < 0));
			}
		}
	}
}

TEST(shared_shader_copies_in_sync) {
	const std::string root = SDF_REPO_ROOT;
	const char *names[] = {"sdf_common", "sdf_primitives", "sdf_blend", "sdf_material"};
	for (const char *n : names) {
		const std::string native = slurp(root + "/native/src/core/shared/" + n + ".glsl");
		const std::string godot = slurp(root + "/game/shaders/sdf/" + n + ".gdshaderinc");
		CHECK(!native.empty());
		if (native != godot) {
			t::fail(__FILE__, __LINE__, std::string(n) + " differs from its Godot copy; run tools/sync_shaders.sh");
		}
	}
}

// A curve tighter than the tool can follow is not a cut a real tool makes; its distance
// bound degrades badly enough to hurt everything around it, so the body refuses it.
TEST(body_rejects_strokes_tighter_than_the_tool) {
	Body b = panel();
	Edit gentle;
	gentle.prim = Primitive::sweep({-30, 0, 8}, {0, 6, 8}, {30, 0, 8}, {0, 0, 1}, ToolProfile::gouge(3, 5, 4));
	CHECK(b.add(gentle));
	Edit tight = gentle;
	tight.prim = Primitive::sweep({-3, 0, 8}, {0, 6, 8}, {3, 0, 8}, {0, 0, 1}, ToolProfile::gouge(3, 5, 4));
	CHECK(!b.add(tight));
	CHECK(b.edits().size() == 1);
	// The same tight curve is fine for a narrow veiner.
	tight.prim = Primitive::sweep({-3, 0, 8}, {0, 6, 8}, {3, 0, 8}, {0, 0, 1}, ToolProfile::gouge(0.4f, 0.6f, 1.0f));
	CHECK(b.add(tight));
}
