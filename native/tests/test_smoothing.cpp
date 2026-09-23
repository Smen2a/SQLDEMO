#include "test.h"

#include "body/layer.h"
#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "edit/session.h"
#include "eval/query.h"
#include "tools/smoothing.h"
#include "tools/tools.h"

#include <algorithm>
#include <memory>

using namespace sdf;

namespace {

// A block of edge-sharp wood, 160 x 100 x 25 mm: its arrises are what sanding rounds.
Body sharp_board() {
	Body b;
	b.base = Primitive::box({0, 0, 0}, {80, 50, 12.5f});
	b.base_material = mat::Ash;
	return b;
}

const vec3 kBisector = gl::normalize(vec3(0, -1, 1)); // out of the front top arris (y = -50, z = 12.5)

// How far in from the sharp arris the surface now is, along the bisector at x.
float removed_at_arris(const EditSession &s, float x) {
	const auto hit = raycast(s.body(), s.octree(), vec3(x, -50, 12.5f) + kBisector * 10.0f, -kBisector, 30.0f, 1e-4f);
	return hit ? hit->t - 10.0f : -1.0f;
}

// Rubs a sponge back and forth along the front top arris, x from -30 to 30, `passes`
// times, working it every 6 mm as SdfBody's worker would; then commits.
void sand_arris(EditSession &s, int passes, int grit = 120) {
	tools::SandingSponge sponge;
	sponge.grit = grit;
	auto stroke = tools::hand_sanding_stroke(sponge, {-30, -50, 12.5f}, kBisector, {1, 0, 0});
	CHECK(stroke->deferred());
	for (int pass = 0; pass < passes; ++pass) {
		for (int k = 1; k <= 30; ++k) {
			stroke->move_to({pass % 2 == 0 ? -30.0f + 2.0f * float(k) : 30.0f - 2.0f * float(k), -50, 12.5f});
			if (k % 3 == 0) {
				const tools::StrokeUpdate u = stroke->work(s.body(), s.octree());
				CHECK(u.empty() || s.revise_stroke(u.drop, u.edits, u.changed));
			}
		}
	}
	s.commit();
}

} // namespace

// B-splines reproduce linear fields: a layer whose phi is the field it covers changes
// nothing, however its weight varies; one that offsets it applies the offset where it
// weighs 1 and nothing where it weighs 0.
TEST(smoothing_layer_reproduces_planes_and_blends) {
	const float h = 0.25f;
	auto plane = [](vec3 p) { return 0.3f * p.x - 0.5f * p.y + 0.8f * p.z - 1.0f; };
	auto build = [&](float offset) {
		std::vector<std::pair<Layer::Coord, std::shared_ptr<const Layer::Block>>> blocks;
		for (int bz = -1; bz <= 0; ++bz) {
			for (int by = -1; by <= 0; ++by) {
				for (int bx = -2; bx <= 1; ++bx) {
					auto b = std::make_shared<Layer::Block>();
					for (int i = 0; i < Layer::kBlockSamples; ++i) {
						const vec3 p = vec3(float(8 * bx + i % 8), float(8 * by + (i / 8) % 8), float(8 * bz + i / 64)) * h;
						b->phi[i] = plane(p) + offset;
						b->w[i] = std::clamp(1.0f - std::fabs(p.x) / 3.0f, 0.0f, 1.0f); // 1 at x = 0, 0 beyond |x| = 3
					}
					b->finish();
					blocks.push_back({{bx, by, bz}, b});
				}
			}
		}
		return Layer(h, blocks, {});
	};
	const Layer same = build(0.0f), offset = build(0.2f);
	t::Rng rng(5);
	float worst_same = 0.0f, worst_centre = 0.0f, worst_outside = 0.0f;
	for (int i = 0; i < 2000; ++i) {
		const vec3 p = rng.vec(-1.5f, 1.5f) + vec3(rng.uniform(-2.5f, 2.5f), 0, 0);
		worst_same = std::max(worst_same, std::fabs(same.apply(p, plane(p)) - plane(p)));
		const vec3 centre(rng.uniform(-0.2f, 0.2f), rng.uniform(-1.5f, 1.5f), rng.uniform(-1.5f, 1.5f));
		// Near x = 0, w~ is the B-spline of a tent: 1 minus a little.
		worst_centre = std::max(worst_centre, std::fabs(offset.apply(centre, plane(centre)) - (plane(centre) + 0.2f)));
		const vec3 out(rng.uniform(5.0f, 20.0f), rng.uniform(-1, 1), rng.uniform(-1, 1));
		worst_outside = std::max(worst_outside, std::fabs(offset.apply(out, plane(out)) - plane(out)));
		// range() bounds what apply() can give in a cube.
		const vec3 lo = rng.vec(-3.0f, 2.0f);
		const Layer::Range r = offset.range(lo, 0.7f);
		const vec3 q = lo + rng.vec(0.0f, 0.7f);
		const float v = offset.apply(q, plane(q));
		CHECK(v >= std::min(plane(q), r.phi_lo) - 1e-5f && v <= std::max(plane(q), r.phi_hi) + 1e-5f);
	}
	CHECK(worst_same < 1e-5f);
	CHECK(worst_centre < 0.2f * 0.1f);
	CHECK(worst_outside == 0.0f);
}

// Hand sanding rounds an arris over, more with more sanding (curvature flow: the radius
// grows as the square root of the sanding done), and nowhere adds material, leaves the
// faces away from the arris alone and touches nothing out of its reach.
TEST(hand_sanding_rounds_arrises_and_only_removes) {
	const Body board = sharp_board();
	Octree original;
	original.build(board);
	float removed[3];
	int passes[3] = {1, 2, 4};
	for (int run = 0; run < 3; ++run) {
		EditSession s;
		s.reset(board);
		sand_arris(s, passes[run]);
		CHECK(s.body().edits().size() == 1 && s.body().edits()[0].op == Op::Layer);
		removed[run] = removed_at_arris(s, 0.0f);
		// Along the stroke it is even; past the reach of its ends, untouched.
		CHECK_NEAR(removed_at_arris(s, 20.0f), removed[run], 0.02);
		CHECK_NEAR(removed_at_arris(s, 45.0f), 0.0, 1e-4);
		// The faces stay where they were, 5 mm and more from the arris.
		for (float x = -20.0f; x <= 20.0f; x += 5.0f) {
			const auto top = raycast(s.body(), s.octree(), {x, -45, 30}, {0, 0, -1}, 40.0f, 1e-4f);
			const auto front = raycast(s.body(), s.octree(), {x, -70, 7.5f}, {0, 1, 0}, 40.0f, 1e-4f);
			CHECK(top && front);
			if (top && front) {
				CHECK_NEAR(top->point.z, 12.5, 1e-4);
				CHECK_NEAR(front->point.y, -50.0, 1e-4);
			}
		}
		// Only removed: the field never falls below the original's near the arris.
		t::Rng rng(11);
		float added = 0.0f;
		for (int i = 0; i < 4000; ++i) {
			const vec3 p = vec3(rng.uniform(-40, 40), -50, 12.5f) + vec3(0, rng.uniform(-3, 3), rng.uniform(-3, 3));
			added = std::max(added, original.distance(board, p) - s.octree().distance(s.body(), p));
		}
		CHECK(added < 1e-4f);
	}
	std::printf("    removed along the bisector after 1, 2, 4 passes: %.3f, %.3f, %.3f mm\n", double(removed[0]),
			double(removed[1]), double(removed[2]));
	CHECK(removed[0] > 0.1f);
	for (int run = 1; run < 3; ++run) {
		const float ratio = removed[run] / removed[run - 1];
		CHECK(ratio > 1.25f && ratio < 1.6f); // sqrt(2)
	}
}

// Octree tapes holding a layer evaluate as the whole edit list does; the field's gradient
// stays within the bound the tapes carry, where a ray can read it (outside the surface and
// just inside); and the ADF gives a layer no exact cells (the shader cannot evaluate one).
TEST(smoothing_layers_prune_soundly_and_bound_their_gradient) {
	EditSession s;
	s.reset(sharp_board());
	sand_arris(s, 3);
	// A chisel cut across the sanded arris afterwards: later edits apply over the layer.
	CHECK(s.set_stroke(tools::Chisel{}.paring({0, -35, 12.5f}, {0, -60, 12.5f}, {0, 0, 1}, 1.5f)));
	s.commit();
	t::Rng rng(23);
	float worst = 0.0f, steepest = 0.0f, bound = 0.0f;
	for (int i = 0; i < 20000; ++i) {
		const vec3 p = vec3(rng.uniform(-40, 40), -50, 12.5f) + vec3(0, rng.uniform(-3, 3), rng.uniform(-3, 3));
		const float d = s.octree().distance(s.body(), p);
		if (std::fabs(d) < 1.0f) {
			worst = std::max(worst, std::fabs(d - s.body().sample_exhaustive(p).d));
		}
		if (d > -0.8f) {
			const float h = 0.02f;
			const vec3 q = p + rng.unit() * h;
			steepest = std::max(steepest, std::fabs(s.octree().distance(s.body(), q) - d) / h);
			const Octree::Step step = s.octree().step(s.body(), p, {1, 0, 0}, true);
			bound = std::max(bound, step.lipschitz);
		}
	}
	std::printf("    tapes vs every edit: %.2g mm; steepest sampled gradient %.3f, bound %.3f\n", double(worst),
			double(steepest), double(bound));
	CHECK(worst < 1e-5f);
	CHECK(steepest <= bound + 1e-2f);
	for (const Adf::ExactCell &cell : s.adf().exact_cells()) {
		for (std::uint32_t k = 0; k < cell.count; ++k) {
			CHECK(s.body().edits()[s.adf().exact_tape()[cell.offset + k] & ~Octree::kResetBit].op != Op::Layer);
		}
	}
}

// A layer revised in place as the sponge moves (re-pruning and re-sampling only where it
// changed) leaves the octree and ADF as a fresh build of the same body would; undo takes
// it away again.
TEST(edit_session_smoothing_matches_fresh_builds) {
	const Body board = sharp_board();
	EditSession s;
	s.reset(board);
	sand_arris(s, 2);
	Octree fresh;
	fresh.build(s.body());
	Adf fresh_adf;
	fresh_adf.build(s.body(), fresh, s.adf().params());
	// Near the surface round the arris (where the ADF keeps bricks), from rays at it.
	t::Rng rng(29);
	float worst_octree = 0.0f, worst_adf = 0.0f;
	for (int i = 0; i < 3000; ++i) {
		const float a = rng.uniform(-1.2f, 1.2f);
		const vec3 dir = -(kBisector * std::cos(a) + gl::normalize(vec3(0, 1, 1)) * std::sin(a));
		const auto hit = raycast(s.body(), fresh, vec3(rng.uniform(-45, 45), -50, 12.5f) - dir * 10.0f, dir, 30.0f, 1e-4f);
		if (!hit) {
			continue;
		}
		const vec3 p = hit->point + hit->normal * rng.uniform(-0.2f, 0.2f);
		const float d = fresh.distance(s.body(), p);
		worst_octree = std::max(worst_octree, std::fabs(s.octree().distance(s.body(), p) - d));
		// Only in cells the surface passes through: an update keeps old splits, so either may
		// have a small empty cell by the surface there, which stores just its centre value.
		auto brick = [&](const Adf &adf) { return adf.nodes()[std::size_t(adf.leaf(p))].brick >= 0; };
		if (!brick(s.adf()) || !brick(fresh_adf)) {
			continue;
		}
		worst_adf = std::max(worst_adf, std::fabs(s.adf().distance(s.body(), s.octree(), p) - fresh_adf.distance(s.body(), fresh, p)));
	}
	std::printf("    worst difference from fresh builds: octree %.2g mm, ADF %.2g mm\n", double(worst_octree), double(worst_adf));
	CHECK(worst_octree <= 1e-5f);
	CHECK(worst_adf <= 2.0f * s.adf().params().tolerance);
	CHECK(s.undo());
	CHECK(s.body().edits().empty());
	CHECK_NEAR(removed_at_arris(s, 0.0f), 0.0, 1e-4);
}

// The "sanded" demo: a small block, so the sponge's reach runs out past the octree's root
// cube (where the octree only bounds distances), and all its motion is worked at once, so
// the surface moves well into the layer's band before the weights see it. Neither may
// loosen the layers' bounds, and the second layer, over the first and a groove, keeps the
// groove's (flat) floor where it was.
TEST(sanded_demo_layers_keep_tight_bounds) {
	Body b;
	Camera c;
	CHECK(demo::named_demo("sanded", b, c));
	int layers = 0;
	for (const Edit &e : b.edits()) {
		if (e.op == Op::Layer) {
			++layers;
			CHECK(e.layer->gradient() < 1.6f);
			CHECK(e.layer->ramp() < 0.3f);
			CHECK(e.layer->max_offset() > 0.3f);
		}
	}
	CHECK(layers == 2);
	Octree octree;
	octree.build(b);
	const auto floor = raycast(b, octree, {0, 4, 30}, {0, 0, -1}, 40.0f, 1e-4f);
	CHECK(floor && std::fabs(floor->point.z - 8.8f) < 1e-3f);
	// Rounded over in the middle of the front arris, sharp past the sponge's reach.
	const vec3 bisector = gl::normalize(vec3(0, -1, 1));
	for (float x : {0.0f, 27.0f}) {
		const auto hit = raycast(b, octree, vec3(x, -20, 10) + bisector * 10.0f, -bisector, 30.0f, 1e-4f);
		CHECK(hit);
		if (hit) {
			CHECK(x == 0.0f ? hit->t - 10.0f > 0.3f : std::fabs(hit->t - 10.0f) < 1e-3f);
		}
	}
}

// Along its longest side, a body's octree root reaches only a millimetre past it, so the
// sponge's reach round an end arris runs out of the root, where the octree only bounds the
// distance. The layer must sample the true field there.
TEST(hand_sanding_an_end_arris_past_the_octree_root) {
	EditSession s;
	s.reset(sharp_board());
	const vec3 bisector = gl::normalize(vec3(1, 0, 1)); // out of the right end's top arris
	auto stroke = tools::hand_sanding_stroke(tools::SandingSponge{}, {80, -30, 12.5f}, bisector, {0, 1, 0});
	for (int pass = 0; pass < 4; ++pass) {
		for (int k = 1; k <= 30; ++k) {
			stroke->move_to({80, pass % 2 == 0 ? -30.0f + 2.0f * float(k) : 30.0f - 2.0f * float(k), 12.5f});
		}
		const tools::StrokeUpdate u = stroke->work(s.body(), s.octree());
		CHECK(u.empty() || s.revise_stroke(u.drop, u.edits, u.changed));
	}
	s.commit();
	const Layer &layer = *s.body().edits().back().layer;
	std::printf("    gradient %.3f, ramp %.3f\n", double(layer.gradient()), double(layer.ramp()));
	CHECK(layer.gradient() < 1.6f && layer.ramp() < 0.3f);
	const auto hit = raycast(s.body(), s.octree(), vec3(80, 0, 12.5f) + bisector * 10.0f, -bisector, 30.0f, 1e-4f);
	CHECK(hit && hit->t - 10.0f > 0.3f);
	const auto end = raycast(s.body(), s.octree(), {100, 0, 0}, {-1, 0, 0}, 40.0f, 1e-4f);
	CHECK(end && std::fabs(end->point.x - 80.0f) < 1e-4f);
}
