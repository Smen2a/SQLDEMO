#include "test.h"

#include "adf/adf.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "util/half.h"

#include <algorithm>
#include <cstdio>

using namespace sdf;

namespace {

struct Built {
	Body body;
	Octree octree;
	Adf adf;
};

Built build_demo(const char *name, const AdfParams &params = {}) {
	Built b;
	Camera camera;
	demo::named_demo(name, b.body, camera);
	b.octree.build(b.body);
	b.adf.build(b.body, b.octree, params);
	return b;
}

// Random points within `band` of the surface, found by stepping random points in the
// body's bounds onto the surface along the field's gradient.
std::vector<vec3> near_surface(const Built &b, int count, float band, t::Rng &rng) {
	const Aabb box = b.body.bounds();
	std::vector<vec3> points;
	for (int tries = 0; int(points.size()) < count && tries < count * 50; ++tries) {
		vec3 p(rng.uniform(box.lo.x, box.hi.x), rng.uniform(box.lo.y, box.hi.y), rng.uniform(box.lo.z, box.hi.z));
		for (int i = 0; i < 12; ++i) {
			const float d = b.octree.distance(b.body, p);
			if (std::fabs(d) < band) {
				points.push_back(p + rng.unit() * rng.uniform(0.0f, band * 0.5f));
				break;
			}
			const float h = 1e-3f;
			const vec3 g(b.octree.distance(b.body, p + vec3(h, 0, 0)) - d, b.octree.distance(b.body, p + vec3(0, h, 0)) - d,
					b.octree.distance(b.body, p + vec3(0, 0, h)) - d);
			const float gl = gl::length(g);
			if (gl < 1e-9f) {
				break;
			}
			p = p - g / gl * d;
		}
	}
	return points;
}

} // namespace

TEST(half_floats_round_trip) {
	const float cases[] = {0.0f, 1.0f, -2.0f, 0.1f, -0.004f, 65504.0f, 1e-5f, -3.1e-7f, 0.33333f};
	for (float f : cases) {
		const float back = half_to_float(float_to_half(f));
		// binary16 keeps 11 significant bits; subnormals below 2^-14 keep an absolute 2^-24.
		CHECK(std::fabs(back - f) <= std::max(std::fabs(f) * (1.0f / 2048.0f), 1.0f / 16777216.0f));
	}
	CHECK(float_to_half(1.0f) == 0x3C00);
	CHECK(float_to_half(-2.0f) == 0xC000);
	CHECK(float_to_half(65504.0f) == 0x7BFF);
	CHECK(float_to_half(1e6f) == 0x7C00);
	CHECK(float_to_half(1.0f + 1.0f / 2048.0f) == 0x3C00); // tie rounds to even
}

// The reconstruction must reproduce the surface: within the tolerance it was refined to
// wherever it was tested (voxel centres near the surface), and not much worse in between.
TEST(adf_matches_exact_field_near_surface) {
	const AdfParams params;
	for (const char *name : {"carved_panel", "sphere", "blend_2", "blend_5", "blend_7", "material_1", "material_4"}) {
		const Built b = build_demo(name, params);
		t::Rng rng(11);
		std::vector<float> errors;
		int exact = 0;
		for (const vec3 &p : near_surface(b, 3000, 0.1f, rng)) {
			errors.push_back(std::fabs(b.adf.distance(b.body, b.octree, p) - b.octree.distance(b.body, p)));
			exact += b.adf.nodes()[std::size_t(b.adf.leaf(p))].exact();
		}
		CHECK(errors.size() > 1000);
		std::sort(errors.begin(), errors.end());
		const float p99 = errors[errors.size() * 99 / 100], worst = errors.back();
		const Adf::Stats s = b.adf.stats();
		std::printf("    %-13s %6zu bricks %5.1f MB, %5zu exact cells (%4.1f%% of surface), finest voxel %.3f mm, %.2fs"
					" | error p99 %.4f max %.4f mm\n",
				name, s.bricks, double(s.bytes) / 1048576.0, s.exact_leaves, 100.0 * exact / double(errors.size()),
				double(s.finest_voxel), s.seconds, double(p99), double(worst));
		CHECK(p99 <= 2.0f * params.tolerance);
		CHECK(worst <= 10.0f * params.tolerance);
	}
}

// Away from the surface the reconstruction may be coarse, but never on the wrong side.
TEST(adf_sign_matches_exact_field) {
	for (const char *name : {"carved_panel", "sphere", "material_2"}) {
		const Built b = build_demo(name);
		t::Rng rng(5);
		const Aabb box = b.body.bounds().expanded(2.0f);
		int wrong = 0;
		for (int i = 0; i < 20000; ++i) {
			const vec3 p(rng.uniform(box.lo.x, box.hi.x), rng.uniform(box.lo.y, box.hi.y), rng.uniform(box.lo.z, box.hi.z));
			const float exact = b.octree.distance(b.body, p);
			if (std::fabs(exact) < 0.05f) {
				continue; // within the reconstruction's own error of the surface
			}
			wrong += (b.adf.distance(b.body, b.octree, p) < 0.0f) != (exact < 0.0f);
		}
		CHECK(wrong == 0);
	}
}

// Updating only around new edits draws the same surface as building from scratch, within
// the tolerance both are built to. (The trees differ: an updated cell that was split before
// stays split, and cells outside an edit's reach keep bricks whose far-off samples came
// from the old tapes.)
TEST(adf_update_matches_rebuild) {
	Body body;
	Camera camera;
	demo::named_demo("carved_panel", body, camera);
	Octree octree;
	octree.build(body);
	Adf adf;
	adf.build(body, octree);
	for (const Edit &e : demo::random_strokes(body, 12, 3)) {
		if (!body.add(e)) {
			continue;
		}
		const std::size_t index = body.edits().size() - 1;
		octree.add_edit(body, std::uint32_t(index));
		adf.update(body, octree, Adf::dirty_region(body, octree, index));
		CHECK(adf.stats().rebuilt_bricks < adf.stats().bricks / 2); // only a neighbourhood
	}
	Adf fresh;
	fresh.build(body, octree);
	const Built probe{body, octree, fresh};
	t::Rng rng(9);
	float worst = 0.0f, worst_exact = 0.0f;
	for (const vec3 &p : near_surface(probe, 3000, 0.1f, rng)) {
		const float updated = adf.distance(body, octree, p);
		worst = std::max(worst, std::fabs(updated - fresh.distance(body, octree, p)));
		worst_exact = std::max(worst_exact, std::fabs(updated - octree.distance(body, p)));
	}
	CHECK(worst <= 2.0f * adf.params().tolerance);
	CHECK(worst_exact <= 10.0f * adf.params().tolerance);
}

TEST(adf_is_independent_of_thread_count) {
	AdfParams one, many;
	one.threads = 1;
	many.threads = 8;
	const Built a = build_demo("carved_panel", one), b = build_demo("carved_panel", many);
	CHECK(a.adf.nodes().size() == b.adf.nodes().size());
	CHECK(a.adf.brick_values() == b.adf.brick_values());
	CHECK(a.adf.material_values() == b.adf.material_values());
	bool same = a.adf.nodes().size() == b.adf.nodes().size();
	for (std::size_t i = 0; same && i < a.adf.nodes().size(); ++i) {
		const Adf::Node &x = a.adf.nodes()[i], &y = b.adf.nodes()[i];
		same = x.child == y.child && x.brick == y.brick && x.material == y.material && x.value == y.value;
	}
	CHECK(same);
}

// Lookups start at the grid block's node; they must land in a leaf containing the point,
// the one a descent from the root finds except on a shared face (where the block's rounded
// coordinate may pick the neighbour; both hold the point). And the grid must name, for
// every block, a node covering all of it.
TEST(adf_grid_lookup_matches_root_descent) {
	for (const char *name : {"carved_panel", "sphere", "blend_4"}) {
		const Built b = build_demo(name);
		const auto &nodes = b.adf.nodes();
		auto from_root = [&](vec3 p) {
			int node = 0;
			while (nodes[std::size_t(node)].child >= 0) {
				const Adf::Node &n = nodes[std::size_t(node)];
				const float half = n.size * 0.5f;
				node = n.child + int(p.x >= n.lo.x + half) + 2 * int(p.y >= n.lo.y + half) + 4 * int(p.z >= n.lo.z + half);
			}
			return node;
		};
		t::Rng rng(21);
		const Adf::Node &root = nodes[0];
		int outside = 0, other = 0;
		for (int i = 0; i < 200000; ++i) {
			const vec3 p = root.lo + vec3(rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f), rng.uniform(0.0f, 1.0f)) * root.size;
			const int found = b.adf.leaf(p);
			const Adf::Node &n = nodes[std::size_t(found)];
			const float tol = n.size * 1e-5f;
			outside += p.x < n.lo.x - tol || p.y < n.lo.y - tol || p.z < n.lo.z - tol || p.x > n.lo.x + n.size + tol ||
					p.y > n.lo.y + n.size + tol || p.z > n.lo.z + n.size + tol;
			other += found != from_root(p);
		}
		CHECK(outside == 0);
		CHECK(other <= 10);
		const float block = root.size / float(Adf::kGridSide);
		bool covering = true;
		for (int z = 0; z < Adf::kGridSide && covering; ++z) {
			for (int y = 0; y < Adf::kGridSide && covering; ++y) {
				for (int x = 0; x < Adf::kGridSide && covering; ++x) {
					const Adf::GridCell &g = b.adf.grid()[std::size_t(x + Adf::kGridSide * (y + Adf::kGridSide * z))];
					const Adf::Node &n = nodes[std::size_t(g.node)];
					const vec3 lo = root.lo + vec3(float(x), float(y), float(z)) * block;
					const float tol = block * 1e-3f;
					covering = std::fabs(n.size - root.size / float(1 << g.depth)) < tol && lo.x >= n.lo.x - tol &&
							lo.y >= n.lo.y - tol && lo.z >= n.lo.z - tol && lo.x + block <= n.lo.x + n.size + tol &&
							lo.y + block <= n.lo.y + n.size + tol && lo.z + block <= n.lo.z + n.size + tol &&
							(g.depth == Adf::kGridLevels || n.child < 0);
				}
			}
		}
		CHECK(covering);
	}
}
