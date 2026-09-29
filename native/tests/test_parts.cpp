#include "test.h"

#include "adf/adf.h"
#include "body/materials.h"
#include "demo/gallery.h"
#include "pieces/parts.h"
#include "pieces/pieces.h"
#include "tools/tools.h"

#include <cstdio>
#include <vector>

using namespace sdf;

namespace {

constexpr float kTop = 12.5f; // the workshop board: 160 x 100 x 25 mm, centred
constexpr double kBoard = 160.0 * 100.0 * 25.0;

// A slot cut out of the body: everything in the box (lo, hi).
Edit slot(vec3 lo, vec3 hi) {
	Edit e;
	e.prim = Primitive::box((lo + hi) * 0.5f, (hi - lo) * 0.5f);
	e.op = Op::Subtract;
	return e;
}

struct Cut {
	Body body;
	Octree octree;
	Adf adf;
	explicit Cut(const Body &b) : body(b) {
		octree.build(body);
		adf.build(body, octree);
	}
	Parts parts(const Aabb *region = nullptr) const { return find_parts(body, octree, adf, region); }
};

bool near(double a, double b, double fraction) {
	return std::fabs(a - b) <= fraction * b;
}

} // namespace

// The board is one part, as big as it is.
TEST(a_whole_board_is_one_part) {
	const Cut board(demo::board(mat::Ash));
	const Parts parts = board.parts();
	std::printf("    %d part(s), %.0f mm^3 (%.0f), in %.1f ms, %zu exact checks\n", int(parts.parts.size()),
			parts.parts.empty() ? 0.0 : parts.parts[0].volume, kBoard, parts.ms, parts.exact_checks);
	CHECK(parts.parts.size() == 1);
	CHECK(near(parts.parts[0].volume, kBoard, 0.02));
	CHECK(std::fabs(parts.parts[0].centre.x) < 0.5f && std::fabs(parts.parts[0].centre.z) < 0.5f);
}

// Two saw kerfs right through leave three parts, largest first; one stopping 0.3 mm short
// of the far face leaves the board whole.
TEST(saw_kerfs_through_leave_parts) {
	Body body = demo::board(mat::Oak);
	CHECK(body.add(tools::Saw{}.kerf_cut({-30, 0, kTop}, {0, 1, 0}, {0, 0, 1}, 26.0f)));
	CHECK(body.add(tools::Saw{}.kerf_cut({40, 0, kTop}, {0, 1, 0}, {0, 0, 1}, 26.0f)));
	const Cut through(body);
	const Parts parts = through.parts();
	std::printf("    %d parts: %.0f, %.0f, %.0f mm^3 in %.1f ms (%zu exact checks)\n", int(parts.parts.size()),
			parts.parts.size() > 0 ? parts.parts[0].volume : 0.0, parts.parts.size() > 1 ? parts.parts[1].volume : 0.0,
			parts.parts.size() > 2 ? parts.parts[2].volume : 0.0, parts.ms, parts.exact_checks);
	CHECK(parts.parts.size() == 3);
	if (parts.parts.size() == 3) {
		CHECK(near(parts.parts[0].volume, 69.2 * 100.0 * 25.0, 0.02)); // the middle, -29.6 to 39.6
		CHECK(near(parts.parts[1].volume, 49.6 * 100.0 * 25.0, 0.02)); // the left end
		CHECK(near(parts.parts[2].volume, 39.6 * 100.0 * 25.0, 0.02)); // the right end
		CHECK(parts.parts[1].centre.x < -30.0f && parts.parts[2].centre.x > 40.0f);
		CHECK(parts.parts[2].bounds.lo.x > 40.0f && parts.parts[1].bounds.hi.x < -30.0f);
	}

	Body shy = demo::board(mat::Oak);
	CHECK(shy.add(tools::Saw{}.kerf_cut({30, 0, kTop}, {0, 1, 0}, {0, 0, 1}, 24.7f)));
	CHECK(Cut(shy).parts().parts.size() == 1);
}

// A slot a tenth of a millimetre wide, thinner than the ADF's voxels there, still parts the
// board: the exact field along the samples' edges finds the air between them.
TEST(a_slot_thinner_than_a_voxel_parts_the_board) {
	Body body = demo::board(mat::Ash);
	CHECK(body.add(slot({9.95f, -60, -20}, {10.05f, 60, 20})));
	const Cut cut(body);
	const Parts parts = cut.parts();
	std::printf("    a 0.1 mm slot: %d parts, %zu exact checks\n", int(parts.parts.size()), parts.exact_checks);
	CHECK(parts.parts.size() == 2);
}

// Two slots meeting at a corner cut it off: an island no single plane separates. Looked at
// only round the second slot, the material there is already in two parts; round a slot
// that stops short of the first, it is not.
TEST(slots_meeting_cut_off_a_corner) {
	Body body = demo::board(mat::Walnut);
	CHECK(body.add(slot({59.6f, 30, -20}, {60.4f, 60, 20})));
	const Edit second = slot({59.6f, 29.6f, -20}, {90, 30.4f, 20});
	CHECK(body.add(second));
	const Cut cut(body);
	const Parts parts = cut.parts();
	const double corner = (80.0 - 60.4) * (50.0 - 30.4) * 25.0;
	std::printf("    %d parts, the corner %.0f mm^3 (%.0f), in %.1f ms\n", int(parts.parts.size()),
			parts.parts.size() > 1 ? parts.parts[1].volume : 0.0, corner, parts.ms);
	CHECK(parts.parts.size() == 2);
	if (parts.parts.size() == 2) {
		CHECK(near(parts.parts[1].volume, corner, 0.03));
		CHECK(parts.parts[1].bounds.lo.x > 60.0f && parts.parts[1].bounds.lo.y > 30.0f);
		CHECK(parts.of_node(cut.adf.leaf(parts.parts[1].centre)) == 1 ||
				cut.adf.nodes()[std::size_t(cut.adf.leaf(parts.parts[1].centre))].brick >= 0);
	}
	const Aabb around = second.bounds().expanded(2.0f);
	const Parts local = cut.parts(&around);
	std::printf("    round the second slot: %d parts in %.1f ms (%zu exact checks)\n", local.count(1.0), local.ms,
			local.exact_checks);
	CHECK(local.count(1.0) == 2);

	Body short_of = demo::board(mat::Walnut);
	CHECK(short_of.add(slot({59.6f, 30, -20}, {60.4f, 60, 20})));
	const Edit stops = slot({62.0f, 29.6f, -20}, {90, 30.4f, 20});
	CHECK(short_of.add(stops));
	const Cut joined(short_of);
	const Aabb round_it = stops.bounds().expanded(2.0f);
	CHECK(joined.parts(&round_it).count(1.0) == 1);
	CHECK(joined.parts().count(1.0) == 1);
}

// Round the cut that freed it, the corner reaches beyond what is looked at, so the whole
// board is looked at; a chip cut free is found round its cut; round a cut that freed
// nothing, one look does.
TEST(an_island_is_found_round_the_cut_that_freed_it) {
	Body body = demo::board(mat::Walnut);
	CHECK(body.add(slot({59.6f, 30, -20}, {60.4f, 60, 20})));
	const Edit second = slot({59.6f, 29.6f, -20}, {90, 30.4f, 20});
	CHECK(body.add(second));
	const Cut cut(body);
	const Island found = find_island(cut.body, cut.octree, cut.adf, second.bounds().expanded(2.0f));
	std::printf("    the corner: part %d of %d after %d looks, %.1f ms (the whole board: %.1f ms)\n", found.island,
			int(found.parts.parts.size()), found.passes, found.ms, cut.parts().ms);
	CHECK(found.island >= 0 && found.passes >= 2);
	if (found.island >= 0) {
		const Parts::Part &corner = found.parts.parts[std::size_t(found.island)];
		CHECK(near(corner.volume, (80.0 - 60.4) * (50.0 - 30.4) * 25.0, 0.03));
		CHECK(!corner.touches_region);
	}

	// A 6 mm block at the top, slotted round and cut under: found in the first look.
	Body chipped = demo::board(mat::Walnut);
	const std::vector<Edit> cuts = {slot({-3.8f, -3.8f, 7}, {-3, 3.8f, 13}), slot({3, -3.8f, 7}, {3.8f, 3.8f, 13}),
			slot({-3.8f, -3.8f, 7}, {3.8f, -3, 13}), slot({-3.8f, 3, 7}, {3.8f, 3.8f, 13}),
			slot({-3.8f, -3.8f, 7}, {3.8f, 3.8f, 8})};
	Aabb around;
	for (const Edit &e : cuts) {
		CHECK(chipped.add(e));
		around.include(e.bounds());
	}
	const Cut chip(chipped);
	const Island block = find_island(chip.body, chip.octree, chip.adf, around.expanded(2.0f));
	std::printf("    a 6 mm block cut free: part %d, %.0f mm^3 (162), %d look, %.1f ms\n", block.island,
			block.island >= 0 ? block.parts.parts[std::size_t(block.island)].volume : 0.0, block.passes, block.ms);
	CHECK(block.island >= 0 && block.passes == 1);
	if (block.island >= 0) {
		CHECK(near(block.parts.parts[std::size_t(block.island)].volume, 162.0, 0.05));
	}

	Body notch = demo::board(mat::Walnut);
	const Edit groove = slot({-20, -60, 8}, {-10, 60, 20});
	CHECK(notch.add(groove));
	const Cut grooved(notch);
	const Island none = find_island(grooved.body, grooved.octree, grooved.adf, groove.bounds().expanded(2.0f));
	std::printf("    a groove: %d part(s) round it in %d look, %.1f ms (%zu exact checks)\n", none.parts.count(1.0),
			none.passes, none.ms, none.parts.exact_checks);
	CHECK(none.island < 0 && none.passes == 1);
}

namespace {

// The corner cut off by two slots meeting (see slots_meeting_cut_off_a_corner).
Body corner_cut(float web = 0.0f) {
	Body body = demo::board(mat::Walnut);
	CHECK(body.add(slot({59.6f, 30, -20}, {60.4f, 60, 20})));
	CHECK(body.add(slot({59.6f, 29.6f, web > 0.0f ? -12.5f + web : -20.0f}, {90, 30.4f, 20})));
	return body;
}

bool on_corner(vec3 p) {
	return p.x > 60.0f && p.y > 30.0f;
}

} // namespace

// Cut out, the corner and the rest are each the body where they are, and nothing where the
// other is: kept surfaces exact, dropped material gone (the field at least half the floor),
// and the field continuous across the gap between them.
TEST(an_island_cut_out_keeps_each_side_exactly) {
	const Cut cut(corner_cut());
	const Parts parts = cut.parts();
	CHECK(parts.parts.size() == 2);
	if (parts.parts.size() != 2) {
		return;
	}
	const CutOut out = cut_out(cut.body, cut.octree, cut.adf, parts, 1);
	std::printf("    %s: %zu leaves (finest %.3f mm), %zu evaluations, %d passes, %.1f ms\n",
			out.region ? "cut out" : out.failed, out.leaves, out.region ? double(out.region->finest()) : 0.0,
			out.evaluations, out.passes, out.ms);
	CHECK(out.region != nullptr);
	if (!out.region) {
		return;
	}
	Body island = cut.body, rest = cut.body;
	CHECK(island.add(Edit::keep(out.region, Region::Island)));
	CHECK(rest.add(Edit::keep(out.region, Region::Rest)));
	t::Rng rng(11);
	int kept = 0;
	float worst_kept = 0.0f, least_dropped = 1e9f;
	for (int i = 0; i < 40000; ++i) {
		const vec3 p(rng.uniform(40, 82), rng.uniform(10, 52), rng.uniform(-14, 14));
		const float d = cut.body.distance(p);
		if (std::fabs(d) > 0.3f) {
			continue;
		}
		const Body &mine = on_corner(p) ? island : rest, &other = on_corner(p) ? rest : island;
		worst_kept = std::max(worst_kept, std::fabs(mine.distance(p) - d));
		least_dropped = std::min(least_dropped, other.distance(p));
		++kept;
	}
	std::printf("    %d points near the surface: kept within %.6f mm, dropped at least %.3f mm\n", kept,
			double(worst_kept), double(least_dropped));
	CHECK(kept > 1000);
	CHECK(worst_kept < 1e-5f);
	CHECK(least_dropped >= 0.5f * Region::kFloor - 1e-4f);
	// Across the gaps, in steps of 5 um: never a jump.
	float steepest = 0.0f;
	for (float z = -12.0f; z <= 12.0f; z += 3.0f) {
		for (const Body *b : {&island, &rest}) {
			for (float x = 58.0f; x < 62.0f; x += 0.005f) {
				steepest = std::max(steepest, std::fabs(b->distance({x + 0.005f, 40, z}) - b->distance({x, 40, z})) / 0.005f);
			}
			for (float y = 28.0f; y < 32.0f; y += 0.005f) {
				steepest = std::max(steepest, std::fabs(b->distance({70, y + 0.005f, z}) - b->distance({70, y, z})) / 0.005f);
			}
		}
	}
	std::printf("    across the gaps the field's slope stays within %.3f\n", double(steepest));
	CHECK(steepest < 1.01f);
	// Each side's own ADF holds its own volume.
	Octree island_tree, rest_tree;
	island_tree.build(island);
	rest_tree.build(rest);
	Adf island_adf, rest_adf;
	island_adf.build(island, island_tree);
	rest_adf.build(rest, rest_tree);
	const double corner = (80.0 - 60.4) * (50.0 - 30.4) * 25.0;
	std::printf("    the corner's own ADF: %.0f mm^3 (%.0f), %zu bricks; the rest's %.0f mm^3, %zu bricks (the whole "
				"board's %zu)\n",
			volume(island_adf), corner, island_adf.stats().bricks, volume(rest_adf), rest_adf.stats().bricks,
			cut.adf.stats().bricks);
	CHECK(near(volume(island_adf), corner, 0.03));
	CHECK(near(volume(rest_adf), volume(cut.adf) - corner, 0.01));

	// Measured for the split: the corner comes away, the rest stays.
	const PieceSides sides = measure_island(cut.adf, parts, 1, out.region);
	std::printf("    measured: the rest %.0f mm^3, the corner %.0f mm^3 at (%.1f, %.1f, %.1f); hulls of %zu and %zu points\n",
			sides.volume[0], sides.volume[1], double(sides.centre[1].x), double(sides.centre[1].y),
			double(sides.centre[1].z), sides.hull[0].size(), sides.hull[1].size());
	CHECK(near(sides.volume[1], corner, 0.03));
	CHECK(near(sides.volume[0], volume(cut.adf) - corner, 0.01));
	CHECK(std::fabs(sides.centre[1].x - 70.2f) < 0.3f && std::fabs(sides.centre[1].y - 40.2f) < 0.3f);
	CHECK(!sides.hull[1].empty() && !sides.hull[0].empty());
	for (const vec3 &p : sides.hull[1]) {
		CHECK(on_corner(p));
	}
	for (const vec3 &p : sides.hull[0]) {
		CHECK(!on_corner(p) || p.x < 60.0f || p.y < 30.0f);
	}
}

// A web a hundredth of a millimetre thick left under the second slot: too thin for the ADF's samples,
// which take the corner for an island, but the cut-out finds the material joining it.
TEST(a_web_too_thin_to_sample_is_not_taken_for_a_gap) {
	const Cut cut(corner_cut(0.01f));
	const Parts parts = cut.parts();
	std::printf("    %d parts by the samples\n", int(parts.parts.size()));
	if (parts.parts.size() < 2) {
		return; // the samples saw it: nothing to cut out
	}
	const CutOut out = cut_out(cut.body, cut.octree, cut.adf, parts, 1);
	std::printf("    cut out: %s after %zu evaluations\n", out.region ? "yes" : out.failed, out.evaluations);
	CHECK(out.region == nullptr);
}

