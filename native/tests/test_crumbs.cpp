#include "test.h"

#include "adf/adf.h"
#include "body/materials.h"
#include "demo/gallery.h"
#include "pieces/hull.h"
#include "pieces/parts.h"
#include "pieces/pieces.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

using namespace sdf;

namespace {

constexpr float kTop = 12.5f; // the workshop board: 160 x 100 x 25 mm, centred

Edit slot(vec3 lo, vec3 hi) {
	Edit e;
	e.prim = Primitive::box((lo + hi) * 0.5f, (hi - lo) * 0.5f);
	e.op = Op::Subtract;
	return e;
}

// A block at the top of the board (x, y from `lo` to `hi`, `depth` down from the top) cut
// free: slotted round with 0.8 mm kerfs and cut under. Adds the cuts' bounds to `around`.
void cut_free(Body &body, vec2 lo, vec2 hi, float depth, Aabb &around) {
	const float k = 0.8f, bottom = kTop - depth;
	const std::vector<Edit> cuts = {slot({lo.x - k, lo.y - k, bottom - k}, {lo.x, hi.y + k, kTop + 1}),
			slot({hi.x, lo.y - k, bottom - k}, {hi.x + k, hi.y + k, kTop + 1}),
			slot({lo.x - k, lo.y - k, bottom - k}, {hi.x + k, lo.y, kTop + 1}),
			slot({lo.x - k, hi.y, bottom - k}, {hi.x + k, hi.y + k, kTop + 1}),
			slot({lo.x - k, lo.y - k, bottom - k}, {hi.x + k, hi.y + k, bottom})};
	for (const Edit &e : cuts) {
		CHECK(body.add(e));
		around.include(e.bounds());
	}
}

struct Cut {
	Body body;
	Octree octree;
	Adf adf;
	explicit Cut(const Body &b) : body(b) {
		octree.build(body);
		adf.build(body, octree);
	}
};

double hull_volume(const ConvexHull &h) {
	vec3 c(0.0f);
	for (const vec3 &p : h.points) {
		c = c + p;
	}
	c = c / float(h.points.size());
	double v = 0.0;
	for (const auto &t : h.triangles) {
		const vec3 a = h.points[std::size_t(t[0])] - c, b = h.points[std::size_t(t[1])] - c,
				   d = h.points[std::size_t(t[2])] - c;
		v += double(gl::dot(a, gl::cross(b, d))) / 6.0;
	}
	return v;
}

} // namespace

// A box's corners, and points on its faces and inside it: the hull is the box, 12
// triangles wound outwards, a closed surface (each edge in two triangles, once each way),
// every point on or inside it. Too few points, or all in a plane, make no hull.
TEST(a_convex_hull_is_closed_and_holds_its_points) {
	std::vector<vec3> points;
	for (int c = 0; c < 8; ++c) {
		points.push_back({c & 1 ? 2.0f : -2.0f, c & 2 ? 1.0f : -1.0f, c & 4 ? 3.0f : 0.0f});
	}
	t::Rng rng(3);
	for (int i = 0; i < 40; ++i) {
		points.push_back({rng.uniform(-2, 2), rng.uniform(-1, 1), rng.uniform(0, 3)});
	}
	points.push_back({0.0f, 0.0f, 3.0f}); // on the top face
	const ConvexHull h = convex_hull(points);
	std::printf("    %zu corners, %zu triangles, %.3f mm^3 (24)\n", h.points.size(), h.triangles.size(), hull_volume(h));
	CHECK(h.points.size() == 8);
	CHECK(h.triangles.size() == 12);
	CHECK(std::fabs(hull_volume(h) - 24.0) < 1e-3);
	std::map<std::pair<int, int>, int> edges;
	for (const auto &t : h.triangles) {
		for (int k = 0; k < 3; ++k) {
			edges[{t[std::size_t(k)], t[std::size_t((k + 1) % 3)]}] += 1;
		}
	}
	bool closed = true;
	for (const auto &[e, count] : edges) {
		closed = closed && count == 1 && edges.count({e.second, e.first}) == 1;
	}
	CHECK(closed);
	// Every point on or behind every face (outward normals).
	float worst = -1e9f;
	for (const auto &t : h.triangles) {
		const vec3 a = h.points[std::size_t(t[0])];
		const vec3 n = gl::normalize(gl::cross(h.points[std::size_t(t[1])] - a, h.points[std::size_t(t[2])] - a));
		for (const vec3 &p : points) {
			worst = std::max(worst, gl::dot(n, p - a));
		}
	}
	CHECK(worst < 1e-4f);
	CHECK(convex_hull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}).empty());
	CHECK(convex_hull({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}, {0.5f, 0.5f, 0}}).empty());
}

// Three blocks cut free at the top of the board: 2 and 3 mm ones (crumbs, under 30 mm^3) and a
// 6 mm one (an island). With crumbs, the check finds both crumbs at once and no island; cut out
// together with one region and kept from the board, they are gone and the 6 mm block is not;
// the next check finds it as the island. Each crumb measured: its volume, and a hull round it.
TEST(crumbs_are_found_and_cut_out_together) {
	Body body = demo::board(mat::Ash);
	Aabb around;
	cut_free(body, {-22.0f, 0.0f}, {-20.0f, 2.0f}, 2.0f, around); // 8 mm^3
	cut_free(body, {20.0f, 0.0f}, {23.0f, 3.0f}, 2.0f, around);   // 18 mm^3
	cut_free(body, {-3.0f, -3.0f}, {3.0f, 3.0f}, 4.5f, around);   // 162 mm^3
	const Cut cut(body);
	const Island found = find_island(cut.body, cut.octree, cut.adf, around.expanded(2.0f), 1.0, 2.0f, 0.05f, 30.0);
	std::printf("    %zu crumb(s), island %d, of %zu parts, %d look(s), %.1f ms\n", found.crumbs.size(), found.island,
			found.parts.parts.size(), found.passes, found.ms);
	CHECK(found.crumbs.size() == 2 && found.island < 0);
	if (found.crumbs.size() != 2) {
		return;
	}
	const CutOut out = cut_out(cut.body, cut.octree, cut.adf, found.parts, found.crumbs);
	std::printf("    cut out together: %s, %zu leaves, %.1f ms\n", out.region ? "yes" : out.failed, out.leaves, out.ms);
	CHECK(out.region != nullptr);
	if (!out.region) {
		return;
	}
	const std::vector<Crumb> crumbs = measure_crumbs(cut.adf, found.parts, found.crumbs, *out.region);
	double crumbed = 0.0;
	for (const Crumb &c : crumbs) {
		const ConvexHull h = convex_hull(c.hull, 1e-3f);
		std::printf("    a crumb of %.2f mm^3 at (%.1f, %.1f, %.1f): %zu hull points, a hull of %zu corners, %.2f mm^3\n",
				c.volume, double(c.centre.x), double(c.centre.y), double(c.centre.z), c.hull.size(), h.points.size(),
				hull_volume(h));
		crumbed += c.volume;
		CHECK(!h.empty());
		const double expected = c.centre.x < 0.0f ? 8.0 : 18.0;
		CHECK(std::fabs(c.volume - expected) < 0.15 * expected);
		CHECK(std::fabs(hull_volume(h) - expected) < 0.3 * expected);
	}
	Body kept = cut.body;
	CHECK(kept.add(Edit::keep(out.region, Region::Rest)));
	const Cut after(kept);
	std::printf("    kept from the board: %.1f mm^3 less (the crumbs %.1f)\n", volume(cut.adf) - volume(after.adf), crumbed);
	CHECK(std::fabs((volume(cut.adf) - volume(after.adf)) - crumbed) < 0.15 * crumbed);
	CHECK(kept.distance({-21.0f, 1.0f, kTop - 1.0f}) > 0.0f);   // a crumb's middle: gone
	CHECK(kept.distance({21.5f, 1.5f, kTop - 1.0f}) > 0.0f);
	CHECK(kept.distance({0.0f, 0.0f, kTop - 2.0f}) < 0.0f);     // the 6 mm block: still there
	// The next check: the island.
	const Island next = find_island(after.body, after.octree, after.adf, around.expanded(2.0f), 1.0, 2.0f, 0.05f, 30.0);
	CHECK(next.crumbs.empty() && next.island >= 0);
	if (next.island >= 0) {
		CHECK(std::fabs(next.parts.parts[std::size_t(next.island)].volume - 162.0) < 0.05 * 162.0);
	}
	// Without crumbs, as before: the smallest part of a cubic millimetre or more is the island.
	const Island old = find_island(cut.body, cut.octree, cut.adf, around.expanded(2.0f));
	CHECK(old.crumbs.empty() && old.island >= 0 &&
			std::fabs(old.parts.parts[std::size_t(old.island)].volume - 8.0) < 0.15 * 8.0);
}

// A crumb cut free at the board's corner: the margin round it reaches past the octree's
// root cube (a millimetre round the board), where the octree knows only how far the cube
// is; the air there is still shown clear, and the crumb is cut out.
TEST(a_crumb_at_the_edge_is_cut_out) {
	Body body = demo::board(mat::Ash);
	Aabb around;
	cut_free(body, {74.0f, -50.0f}, {80.0f, -48.0f}, 2.0f, around); // 24 mm^3, less the arris
	const Cut cut(body);
	const Island found = find_island(cut.body, cut.octree, cut.adf, around.expanded(2.0f), 30.0, 2.0f, 0.05f, 30.0);
	CHECK(found.crumbs.size() == 1 && found.island < 0);
	if (found.crumbs.size() != 1) {
		return;
	}
	const CutOut out = cut_out(cut.body, cut.octree, cut.adf, found.parts, found.crumbs);
	std::printf("    a crumb of %.1f mm^3 at the corner: %s, %zu leaves, %.1f ms\n",
			found.parts.parts[std::size_t(found.crumbs[0])].volume, out.region ? "cut out" : out.failed, out.leaves, out.ms);
	CHECK(out.region != nullptr);
	if (!out.region) {
		return;
	}
	Body kept = cut.body;
	CHECK(kept.add(Edit::keep(out.region, Region::Rest)));
	CHECK(kept.distance({77.0f, -49.0f, kTop - 1.0f}) > 0.0f); // gone
	CHECK(kept.distance({70.0f, -45.0f, kTop - 1.0f}) < 0.0f); // the board is not
}
