#include "test.h"

#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/reference_renderer.h"
#include "io/png.h"

using namespace sdf;

namespace {

// A panel worked with every operator and blend, including cuts deep enough to swallow
// earlier ones (so cells reset) and unions of other materials that later cuts pass
// through (so resets must keep material history).
Body worked_panel(t::Rng &rng, int edits) {
	Body b;
	b.base = Primitive::box({0, 0, 0}, {60, 40, 8});
	b.base_material = 0;
	const Op ops[] = {Op::Subtract, Op::Subtract, Op::Subtract, Op::Union, Op::Subtract, Op::Engrave, Op::Groove,
		Op::Tongue, Op::Paint, Op::Intersect};
	const Blend blends[] = {Blend::Hard, Blend::Chamfer, Blend::Round, Blend::Smooth, Blend::SmoothC2, Blend::Profile};
	for (int i = 0; i < edits; ++i) {
		Edit e;
		e.op = ops[i % 10];
		e.blend = blends[(i / 3) % 6];
		e.r = rng.uniform(0.2f, 2.0f);
		e.r2 = rng.uniform(0.2f, 2.0f);
		e.shape = EdgeProfile(i % 3);
		e.material = std::uint16_t(1 + i % 3);
		const vec3 c(rng.uniform(-55, 55), rng.uniform(-35, 35), rng.uniform(2, 9));
		switch (i % 5) {
			case 0: e.prim = Primitive::sphere(c, rng.uniform(2, 9)); break;
			case 1: e.prim = Primitive::box(c, rng.vec(1, 8), rng.uniform(0, 0.5f), rng.rotation()); break;
			case 2: e.prim = Primitive::capsule(c, c + rng.vec(-12, 12), rng.uniform(1, 3)); break;
			case 3:
				e.prim = Primitive::sweep(c, c + rng.vec(-10, 10), c + rng.vec(-25, 25), {0, 0, 1},
						i % 2 ? ToolProfile::v_tool(60, 6) : ToolProfile::gouge(3, 5, 6));
				break;
			default:
				e.prim = Primitive::sweep(c, c + rng.vec(-25, 25), {0, 0, 1}, ToolProfile::flat(rng.uniform(2, 8), 6));
		}
		if (e.op == Op::Intersect) {
			// Keep intersections gentle: trim with a large sphere rather than erasing the panel.
			e.prim = Primitive::sphere({0, 0, -20}, rng.uniform(75, 90));
		}
		if (i % 17 == 0) {
			// A big hog-out that swallows whole cells and everything cut there before.
			e.op = Op::Subtract;
			e.prim = Primitive::box(c + vec3(0, 0, 4), rng.vec(8, 14));
		}
		b.add(e);
	}
	return b;
}

float weight_of(const Sample &s, float id) {
	return (s.m0 == id ? 1 - s.t : 0) + (s.m1 == id ? s.t : 0);
}

void check_agreement(const Body &b, const Octree &oct, t::Rng &rng, int points) {
	for (int k = 0; k < points; ++k) {
		const vec3 p(rng.uniform(-70, 70), rng.uniform(-50, 50), rng.uniform(-14, 22));
		const Sample full = b.sample_exhaustive(p);
		const Sample pruned = oct.sample(b, p);
		if (std::fabs(full.d) > 1e-4f) {
			CHECK((pruned.d < 0) == (full.d < 0));
		}
		// Within the value margin of the surface, values and materials are exact too.
		if (std::fabs(full.d) < 0.5f * oct.value_margin()) {
			CHECK_NEAR(pruned.d, full.d, 1e-4);
			for (float id = 0; id < 4; ++id) {
				CHECK_NEAR(weight_of(pruned, id), weight_of(full, id), 1e-4);
			}
		}
	}
}

} // namespace

TEST(octree_agrees_with_exhaustive_evaluation) {
	t::Rng rng(30);
	for (int trial = 0; trial < 6; ++trial) {
		const Body b = worked_panel(rng, 120);
		Octree oct;
		oct.build(b);
		check_agreement(b, oct, rng, 30000);
	}
}

TEST(octree_resets_keep_material_history) {
	// Putty added, then a long hog-out that runs through the putty and on into plain wood:
	// where it swallows plain-wood cells their tapes reset, but inside the putty the cut
	// faces must still be putty, so there the history has to survive.
	Body b;
	b.base = Primitive::box({0, 0, 0}, {60, 30, 8});
	Edit putty;
	putty.prim = Primitive::sphere({-30, 0, 8}, 12);
	putty.op = Op::Union;
	putty.material = 3;
	b.add(putty);
	Edit hog;
	hog.prim = Primitive::box({0, 0, 10}, {50, 8, 7});
	hog.op = Op::Subtract;
	b.add(hog);
	Octree oct;
	OctreeParams deep;
	deep.leaf_budget = 1;        // forces subdivision...
	deep.feature_fraction = 0.0f; // ...below the edits' own scale, so cells fit inside the cut
	oct.build(b, deep);
	bool any_reset = false;
	for (const Octree::Leaf &l : oct.leaves()) {
		for (std::uint32_t e : l.tape) {
			any_reset |= (e & Octree::kResetBit) != 0;
		}
	}
	CHECK(any_reset);
	t::Rng rng(31);
	for (int k = 0; k < 40000; ++k) {
		const vec3 p(rng.uniform(-62, 62), rng.uniform(-20, 20), rng.uniform(-2, 22));
		const Sample full = b.sample_exhaustive(p), pruned = oct.sample(b, p);
		// Material is only meaningful on the surface; the guarantee covers value_margin.
		if (std::fabs(full.d) < 0.5f * oct.value_margin()) {
			CHECK_NEAR(weight_of(pruned, 3), weight_of(full, 3), 1e-4);
		}
	}
}

TEST(octree_incremental_matches_full_build) {
	t::Rng rng(32);
	const Body target = worked_panel(rng, 150);
	Body b;
	b.base = target.base;
	b.base_material = target.base_material;
	Octree incremental;
	incremental.build(b);
	for (std::size_t i = 0; i < target.edits().size(); ++i) {
		b.add(target.edits()[i]);
		incremental.add_edit(b, std::uint32_t(i));
	}
	check_agreement(b, incremental, rng, 30000);
}

// Realistic carving: V, gouge and flat strokes, mostly hard with some eased edges.
Body carving_session(t::Rng &rng, int strokes) {
	Body b;
	b.base = Primitive::box({0, 0, 0}, {60, 40, 8});
	const ToolProfile tools[] = {ToolProfile::v_tool(60, 5), ToolProfile::gouge(3, 5, 4), ToolProfile::flat(4, 4)};
	for (int i = 0; i < strokes; ++i) {
		// Later strokes go deeper, as roughing gives way to detail.
		const float depth = 0.5f + 6.0f * float(i) / float(strokes) * rng.uniform(0.3f, 1.0f);
		const vec3 a(rng.uniform(-55, 55), rng.uniform(-35, 35), 8 - depth);
		const vec3 c = a + vec3(rng.uniform(-15, 15), rng.uniform(-15, 15), 0);
		const vec3 ctrl = (a + c) * 0.5f + vec3(rng.uniform(-3, 3), rng.uniform(-3, 3), 0);
		Edit e;
		e.prim = Primitive::sweep(a, ctrl, c, {0, 0, 1}, tools[i % 3]);
		e.op = Op::Subtract;
		if (i % 5 == 0) {
			e.blend = Blend::Round;
			e.r = e.r2 = 0.5f;
		}
		b.add(e);
	}
	return b;
}

TEST(octree_keeps_tapes_short) {
	const Body panel = demo::carved_panel_body();
	Octree oct;
	oct.build(panel);
	const Octree::Stats s = oct.stats();
	std::printf("    carved panel: %zu edits -> %zu surface leaves, mean tape %.2f, max %zu\n", panel.edits().size(),
			s.surface_leaves, s.mean_surface_tape, s.max_tape);
	CHECK_LE(s.mean_surface_tape, 10.0);

	t::Rng rng(33);
	const Body session = carving_session(rng, 2000);
	Octree session_oct;
	session_oct.build(session);
	const Octree::Stats ss = session_oct.stats();
	std::printf("    2000-stroke session: %zu surface leaves, mean tape %.2f, max %zu\n", ss.surface_leaves,
			ss.mean_surface_tape, ss.max_tape);
	CHECK_LE(ss.mean_surface_tape, 16.0);
	check_agreement(session, session_oct, rng, 20000);
}

// End to end: tracing the pruned tapes, with empty-cell skipping and cell-clamped steps,
// must reproduce the reference renderer's image of the full edit list. Primary visibility,
// normals and shadows only read values near the surface, where tapes are exact, so they
// must match closely. Ambient occlusion samples up to 4.5 mm out, beyond the 1 mm value
// margin, where tapes are only valid bounds; widening the margin to cover it would cost
// ~60% longer tapes on busy bodies, so that term is allowed to drift slightly.
TEST(octree_rendering_matches_reference) {
	const Body panel = demo::carved_panel_body();
	Octree oct;
	oct.build(panel);
	for (bool ao : {false, true}) {
		RenderSettings rs;
		rs.width = 640;
		rs.height = 400;
		rs.light_dir = gl::normalize(vec3(-0.62f, -0.42f, 0.66f));
		rs.ambient_occlusion = ao;
		const Image ref = render(panel, MaterialTable::standard(), demo::carved_panel_camera(), rs);
		const Image img = render(panel, MaterialTable::standard(), demo::carved_panel_camera(), rs, &oct);
		const ImageDiff d = compare(img, ref, 24);
		std::printf("    octree vs full, ao=%d: mean %.3f  over %.4f%%  max %d\n", int(ao), d.mean_abs,
				d.fraction_over * 100, d.max_abs);
		CHECK_LE(d.mean_abs, ao ? 0.4 : 0.1);
		CHECK_LE(d.fraction_over, ao ? 0.004 : 0.001);
	}
}
