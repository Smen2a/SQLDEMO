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
	b.edits.push_back(v_cut({-80, 0, 0}, {80, 0, 0}, 3));
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
	hard.edits.push_back(e);
	e.blend = Blend::Round;
	e.r = e.r2 = 1.0f;
	eased.edits.push_back(e);
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
	b.edits.push_back(putty);

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
	b.edits.back().blend = Blend::Hard;
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
	b.edits.push_back(paint);
	const vec3 inside(20, 0, 9.5f), outside(40, 0, 9.5f);
	CHECK_NEAR(b.distance(inside), -0.5, 1e-5);
	const Sample s = b.sample(inside);
	CHECK((s.m0 == 7 && s.t == 0) || (s.m1 == 7 && s.t == 1));
	CHECK(b.sample(outside).m0 == 0);

	// A soft transition gives intermediate weights across its width.
	b.edits.back().r = 2;
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
			b.edits.push_back(e);
		}
		const float bound = b.lipschitz();
		const float l = t::sampled_lipschitz([&](vec3 p) { return b.distance(p); }, {0, 0, 8}, 50, 20000, rng, 0.05f);
		CHECK_LE(l, bound * 1.001);
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
