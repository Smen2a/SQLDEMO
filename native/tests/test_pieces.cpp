#include "test.h"

#include "adf/adf.h"
#include "body/materials.h"
#include "demo/gallery.h"
#include "edit/session.h"
#include "pieces/pieces.h"
#include "tools/tools.h"

#include <algorithm>
#include <cstdio>

using namespace sdf;

namespace {

constexpr float kTop = 12.5f; // the workshop board: 160 x 100 x 25 mm, centred

// The board with a saw kerf across it at x = 30, `depth` deep from the top face.
Body sawn_board(float depth) {
	Body body = demo::board(mat::Oak);
	CHECK(body.add(tools::Saw{}.kerf_cut({30, 0, kTop}, {0, 1, 0}, {0, 0, 1}, depth)));
	return body;
}

// The kerf's middle plane (as the saw stroke reports it).
Plane kerf_plane() {
	return Plane{{30, 0, kTop}, {-1, 0, 0}};
}

} // namespace

// A saw kerf short of the far face leaves the board whole; one through it leaves two parts.
TEST(plane_clear_tells_when_a_saw_is_through) {
	for (const float depth : {24.5f, 26.0f}) {
		const Body body = sawn_board(depth);
		Octree octree;
		octree.build(body);
		std::size_t samples = 0;
		const bool clear = plane_clear(body, octree, kerf_plane(), body.bounds(), 0.05f, &samples);
		std::printf("    kerf %.1f mm deep in a 25 mm board: %s (%zu samples)\n", double(depth),
				clear ? "apart" : "joined", samples);
		CHECK(clear == (depth > 25.0f));
	}
	// A plane through solid wood is never clear, nor one grazing a face.
	const Body whole = demo::board(mat::Oak);
	Octree octree;
	octree.build(whole);
	CHECK(!plane_clear(whole, octree, kerf_plane(), whole.bounds()));
	CHECK(plane_clear(whole, octree, Plane{{0, 0, kTop + 0.5f}, {0, 0, 1}}, whole.bounds()));
}

// The saw stroke reports its kerf's plane once it is through, and only then.
TEST(saw_strokes_report_their_plane_once_through) {
	auto saw = tools::saw_stroke(tools::Saw{}, {30, 0, kTop}, {0, 0, 1}, {0, 1, 0}, 0.5f, 26.0f);
	for (int k = 0; k < 200 && !saw->separation(); ++k) {
		saw->move_to({30, k % 2 ? -10.0f : 10.0f, kTop});
		if (k == 2) {
			CHECK(!saw->separation()); // 20 mm of travel: 10 mm deep
		}
	}
	const std::optional<Separation> cut = saw->separation();
	CHECK(cut.has_value());
	if (cut) {
		CHECK(std::fabs(cut->plane.distance({30, 17, -3})) < 1e-5f);
		CHECK(std::fabs(std::fabs(cut->plane.normal.x) - 1.0f) < 1e-5f);
		CHECK(cut->gap == tools::Saw{}.kerf);
	}
}

// A piece (the body with the half-space behind a plane kept) has the body's field near the
// surface on its own side, and none beyond the plane.
TEST(pieces_keep_the_body_on_their_side) {
	const Body body = sawn_board(26.0f);
	const Plane plane = kerf_plane();
	Body left = body, right = body;
	CHECK(left.add(plane.keep_behind()));
	CHECK(right.add(plane.flipped().keep_behind()));
	t::Rng rng(5);
	int compared = 0;
	for (int i = 0; i < 20000; ++i) {
		const vec3 p(rng.uniform(-82, 82), rng.uniform(-52, 52), rng.uniform(-14, 14));
		const float d = body.distance(p), side = plane.distance(p);
		if (std::fabs(d) > 0.3f || std::fabs(side) < 0.5f) {
			continue;
		}
		const Body &mine = side < 0.0f ? left : right, &other = side < 0.0f ? right : left;
		CHECK(std::fabs(mine.distance(p) - d) < 1e-5f);
		CHECK(other.distance(p) >= std::fabs(side) - 1e-4f);
		++compared;
	}
	CHECK(compared > 1000);
}

// The worker measures both sides as soon as a cut leaves two parts, turning the cut so the
// smaller side is in front (it comes away). Each piece keeps the half-space behind its own
// face: just outside that face the field is the plain distance to it, with no kink where
// the other face would have been (which the ADF would refine along the whole face).
TEST(measured_sides_put_the_smaller_piece_in_front) {
	const Body body = sawn_board(26.0f);
	Octree octree;
	octree.build(body);
	Adf adf;
	adf.build(body, octree);
	const Separation cut{kerf_plane(), tools::Saw{}.kerf}; // its front is the longer side (x < 30)
	const PieceSides sides = measure_sides(adf, cut, true);
	std::printf("    sides: %.0f mm^3 behind, %.0f in front (the offcut); hulls of %zu and %zu points\n",
			sides.volume[0], sides.volume[1], sides.hull[0].size(), sides.hull[1].size());
	CHECK(sides.cut.plane.normal.x > 0.5f); // turned: the front is now x > 30
	CHECK(std::fabs(sides.volume[1] - 49.6 * 100.0 * 25.0) < 0.02 * 49.6 * 100.0 * 25.0);
	CHECK(std::fabs(sides.volume[0] - 109.6 * 100.0 * 25.0) < 0.02 * 109.6 * 100.0 * 25.0);
	CHECK(sides.centre[1].x > 30.0f && sides.centre[0].x < 30.0f);
	for (const vec3 &p : sides.hull[1]) {
		CHECK(p.x > 30.3f);
	}
	Body offcut = body;
	CHECK(offcut.add(sides.cut.front().keep_behind()));
	float worst = 0.0f;
	for (float y = -45.0f; y <= 45.0f; y += 9.7f) {
		for (float z = -11.0f; z <= 11.0f; z += 4.3f) {
			for (float d = 0.05f; d < 0.75f; d += 0.1f) { // in the kerf, outside the offcut's face at x = 30.4
				worst = std::max(worst, std::fabs(offcut.distance({30.4f - d, y, z}) - d));
			}
		}
	}
	std::printf("    outside the offcut's face the field strays %.4f mm from the distance to it\n", double(worst));
	CHECK(worst < 0.006f);
}

// Volumes and hull points from the ADF: a box's volume within 2%, on either side of a plane,
// and hull points on the box's surface, behind the plane, reaching its corners.
TEST(piece_volumes_and_hull_points_from_the_adf) {
	Body body;
	body.base = Primitive::box(vec3(0.0f), vec3(20, 15, 10));
	Octree octree;
	octree.build(body);
	Adf adf;
	adf.build(body, octree);
	const double full = volume(adf);
	const Plane plane{{5, 0, 0}, {1, 0, 0}};
	vec3 centre(0.0f);
	const double behind = volume(adf, &plane, &centre);
	std::printf("    box volume %.0f mm^3 (24000), behind x = 5: %.0f (15000), centred at x = %.2f (-7.5)\n", full,
			behind, double(centre.x));
	CHECK(std::fabs(full - 24000.0) < 0.02 * 24000.0);
	CHECK(std::fabs(behind - 15000.0) < 0.02 * 15000.0);
	CHECK(std::fabs(centre.x + 7.5f) < 0.2f && std::fabs(centre.y) < 0.2f && std::fabs(centre.z) < 0.2f);

	const std::vector<vec3> points = hull_points(adf, &plane, 256);
	CHECK(!points.empty() && points.size() <= 256);
	float lowest_x = 1e9f, highest_x = -1e9f;
	bool on_surface = true;
	for (const vec3 &p : points) {
		on_surface = on_surface && std::fabs(body.distance(p)) < 0.05f && plane.distance(p) < 0.0f;
		lowest_x = std::min(lowest_x, p.x);
		highest_x = std::max(highest_x, p.x);
	}
	CHECK(on_surface);
	CHECK(lowest_x < -19.9f);
	CHECK(highest_x > 4.0f && highest_x <= 5.0f); // up to the plane

	const Aabb clipped = clip_box(body.bounds(), plane);
	CHECK(std::fabs(clipped.hi.x - 5.0f) < 1e-4f && std::fabs(clipped.lo.x - body.bounds().lo.x) < 1e-4f);
	CHECK(std::fabs(clipped.hi.y - body.bounds().hi.y) < 1e-4f);
}

// Dropping the last step takes it off without leaving it to redo.
TEST(edit_session_drops_the_last_step) {
	EditSession s;
	s.reset(demo::board(mat::Ash));
	CHECK(s.set_stroke({tools::Saw{}.kerf_cut({30, 0, kTop}, {0, 1, 0}, {0, 0, 1}, 26.0f)}));
	s.commit();
	const std::vector<Edit> before = s.body().edits();
	CHECK(s.set_stroke({kerf_plane().keep_behind()}));
	s.commit();
	CHECK(s.body().edits().size() == before.size() + 1);
	CHECK(s.drop_last_step());
	CHECK(s.body().edits().size() == before.size());
	CHECK(!s.can_redo());
	CHECK(s.can_undo());
}
