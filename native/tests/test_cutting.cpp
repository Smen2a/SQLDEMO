#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "tools/catalog.h"
#include "tools/cutting.h"
#include "tools/debris.h"

#include <cstdio>
#include <cstring>

using namespace sdf;
using namespace sdf::tools;

namespace {

// The workshop board (160 x 100 x 25 mm, centred): its grain runs along x, rising out of
// the top face towards +x (downhill that way).
struct Board {
	Body body;
	Octree octree;
	MaterialTable materials = MaterialTable::standard();

	explicit Board(std::uint16_t wood = mat::Ash) : body(demo::board(wood)) { octree.build(body); }
	Work work() const { return {body, octree, materials}; }
	void add(const std::vector<Edit> &edits) {
		for (const Edit &e : edits) {
			CHECK(body.add(e));
		}
		octree.build(body);
	}
};

constexpr float kTop = 12.5f;
const vec3 kUp{0, 0, 1};

Chisel variant(const char *id, float approach) {
	const ChiselVariant *v = find_chisel(id);
	CHECK(v != nullptr);
	Chisel c = v ? v->chisel : Chisel{};
	c.approach_deg = approach;
	return c;
}

bool has(const CutPlan &p, unsigned warning) {
	return (p.warnings & warning) != 0;
}

void describe(const char *what, const CutPlan &p) {
	std::string warnings;
	for (const std::string &w : warning_names(p.warnings)) {
		warnings += (warnings.empty() ? "" : ", ") + w;
	}
	std::printf("    %s: %.2f mm deep, %.0f of %.0f N, grain %.2f, slope %+d, %zu chips%s%s\n", what, double(p.depth),
			double(p.force), double(p.available), double(p.grain), p.slope, p.chips.size(),
			warnings.empty() ? "" : "; ", warnings.c_str());
}

} // namespace

// Pared by hand from the board's end, along the grain and downhill, a 12 mm bench chisel
// takes about half a millimetre of ash (asked for more, it stays shallower): cleanly, with
// its chip free in front from the start. Across the grain from a side edge it takes half
// that. Walnut, softer, gives a little more.
TEST(a_bench_chisel_pares_what_a_hand_can_push) {
	const Board ash;
	const Chisel bench = variant("bench_12", 20.0f);
	const CutPlan along = plan_cut(bench, ash.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 40.0f, 1.0f, 0.0f, 1);
	describe("ash along the grain from the end", along);
	CHECK(along.depth > 0.4f && along.depth < 0.6f);
	CHECK(has(along, kShallow) && !has(along, kTearOut) && !has(along, kSkates));
	CHECK(along.slope == 1 && along.grain < 0.05f);
	CHECK(along.floor.front().y > 0.4f); // in at its depth at once
	CHECK(std::fabs(along.force - along.available) < 0.02f * along.available);

	const CutPlan across = plan_cut(bench, ash.work(), {0, -49.5f, kTop}, kUp, {0, 1, 0}, 30.0f, 1.0f, 0.0f, 1);
	describe("ash across the grain from the side", across);
	CHECK(across.depth > 0.18f && across.depth < 0.32f);
	CHECK(across.grain > 0.15f && across.grain < 0.35f);

	const Board walnut(mat::Walnut);
	const CutPlan soft = plan_cut(bench, walnut.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 40.0f, 1.0f, 0.0f, 1);
	describe("walnut along the grain from the end", soft);
	CHECK(soft.depth > along.depth + 0.05f);
}

// In the middle of a face, bevel down, a chisel held flatter than its bevel (plus 2
// degrees of clearance) rides on its bevel and cuts nothing; tipped past it, it dives at
// the difference until it levels. Diving steeply it digs in, but never below the depth
// asked (no chip goes deeper).
TEST(mid_face_a_chisel_skates_until_tipped_past_its_bevel) {
	const Board ash;
	const vec3 middle{-20, 0, kTop};
	const CutPlan flat = plan_cut(variant("bench_12", 20.0f), ash.work(), middle, kUp, {1, 0, 0}, 40.0f, 0.3f, 0.0f, 1);
	describe("held at 20 degrees", flat);
	CHECK(has(flat, kSkates) && flat.edits().empty());

	const CutPlan tipped = plan_cut(variant("bench_12", 33.0f), ash.work(), middle, kUp, {1, 0, 0}, 40.0f, 0.3f, 0.0f, 1);
	describe("tipped to 33 degrees", tipped);
	CHECK(!has(tipped, kSkates) && !has(tipped, kDigsIn));
	CHECK(tipped.floor.front().y == 0.0f); // it has to dive in
	const float ramp = 0.3f / std::tan(6.0f * 3.14159265f / 180.0f);
	CHECK(tipped.depth_at(0.5f * ramp) > 0.12f && tipped.depth_at(0.5f * ramp) < 0.18f);
	CHECK(std::fabs(tipped.depth_at(ramp + 2.0f) - 0.3f) < 1e-3f);
	CHECK(!tipped.edits().empty());

	const CutPlan steep = plan_cut(variant("bench_12", 45.0f), ash.work(), middle, kUp, {1, 0, 0}, 40.0f, 0.3f, 0.0f, 1);
	describe("tipped to 45 degrees", steep);
	CHECK(has(steep, kDigsIn) && std::fabs(steep.depth - 0.3f) < 1e-3f);
	bool shallow = true;
	for (const vec2 &f : steep.floor) {
		shallow = shallow && f.y <= 0.3f + 1e-4f;
	}
	CHECK(shallow);
}

// Against the grain (uphill: the fibres run down into the wood ahead) the split tears out
// below the cut; with the grain the same cut is clean. Chips are seeded: the same seed,
// the same chips.
TEST(against_the_grain_it_tears_out) {
	const Board oak(mat::Oak);
	const Chisel c = variant("bench_12", 33.0f);
	const CutPlan downhill = plan_cut(c, oak.work(), {-30, 0, kTop}, kUp, {1, 0, 0}, 60.0f, 0.3f, 0.0f, 7);
	const CutPlan uphill = plan_cut(c, oak.work(), {30, 0, kTop}, kUp, {-1, 0, 0}, 60.0f, 0.3f, 0.0f, 7);
	describe("oak downhill", downhill);
	describe("oak uphill", uphill);
	CHECK(downhill.slope == 1 && !has(downhill, kTearOut) && downhill.chips.empty());
	CHECK(uphill.slope == -1 && has(uphill, kTearOut) && !uphill.chips.empty());
	const CutPlan again = plan_cut(c, oak.work(), {30, 0, kTop}, kUp, {-1, 0, 0}, 60.0f, 0.3f, 0.0f, 7);
	CHECK(again.chips.size() == uphill.chips.size());
	for (std::size_t i = 0; i < again.chips.size() && i < uphill.chips.size(); ++i) {
		CHECK(std::memcmp(&again.chips[i].prim, &uphill.chips[i].prim, sizeof(Primitive)) == 0);
	}
}

// Across the grain out of the side of the board, the unsupported fibres at the exit break
// away; along the grain out of the end, they do not.
TEST(across_the_grain_it_breaks_out_at_the_exit) {
	const Board ash;
	const CutPlan out = plan_cut(variant("bench_12", 33.0f), ash.work(), {0, 30, kTop}, kUp, {0, 1, 0}, 30.0f, 0.2f, 0.0f,
			3);
	describe("across, out of the side", out);
	CHECK(has(out, kBreaksOut));
	const CutPlan end = plan_cut(variant("bench_12", 33.0f), ash.work(), {60, 0, kTop}, kUp, {1, 0, 0}, 30.0f, 0.2f, 0.0f,
			3);
	describe("along, out of the end", end);
	CHECK(!has(end, kBreaksOut));
}

// A gouge with its corners out of the wood takes a chip with free sides: in the middle of a
// face it goes a millimetre deep where a flat chisel of its width stays at half that. Deeper
// than its sweep (a shallow #3's is under a millimetre), its corners bury and tear.
TEST(a_gouge_with_its_corners_out_goes_deeper) {
	const Board ash;
	const vec3 middle{-20, 0, kTop};
	const CutPlan gouge = plan_cut(variant("gouge_7_12", 30.0f), ash.work(), middle, kUp, {1, 0, 0}, 40.0f, 1.0f, 0.0f, 1);
	const CutPlan chisel = plan_cut(variant("bench_12", 33.0f), ash.work(), middle, kUp, {1, 0, 0}, 40.0f, 1.0f, 0.0f, 1);
	describe("#7 gouge", gouge);
	describe("bench chisel", chisel);
	CHECK(std::fabs(gouge.depth - 1.0f) < 1e-3f && !has(gouge, kShallow) && !has(gouge, kCornersBuried));
	CHECK(chisel.depth < 0.6f && has(chisel, kShallow));
	const Board walnut(mat::Walnut);
	const CutPlan buried = plan_cut(variant("gouge_3_12", 30.0f), walnut.work(), middle, kUp, {1, 0, 0}, 40.0f, 1.5f,
			0.0f, 1);
	describe("#3 gouge in walnut, 1.5 mm asked", buried);
	CHECK(buried.depth > variant("gouge_3_12", 30.0f).corner_depth() && has(buried, kCornersBuried));
	CHECK(variant("gouge_3_12", 30.0f).corner_depth() < variant("gouge_7_12", 30.0f).corner_depth());
}

// A skewed edge slices: the same shaving takes less force.
TEST(a_skewed_edge_slices_for_less_force) {
	const Board ash;
	const CutPlan square = plan_cut(variant("bench_12", 20.0f), ash.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 30.0f,
			0.2f, 0.0f, 1);
	const CutPlan skewed = plan_cut(variant("bench_12", 20.0f), ash.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 30.0f,
			0.2f, 30.0f, 1);
	describe("square", square);
	describe("skewed 30 degrees", skewed);
	CHECK(skewed.force < 0.8f * square.force);
	CHECK(skewed.width < square.width);
}

// Chopping: a mallet blow drives a slit, 2-3 mm across the grain in oak for a 12 mm bench
// chisel, further along the grain (where it splits the wood), less at the next blow. Far
// from an open face it only makes the slit; near one, the chip between pops off. A mortise
// chisel's blows go half as far again; a paring chisel is never struck.
TEST(a_chop_drives_a_slit_and_pops_chips_near_an_edge) {
	Board oak(mat::Oak);
	const Chisel bench = variant("bench_12", 90.0f);
	const CutPlan across = plan_cut(bench, oak.work(), {0, 0, kTop}, kUp, {1, 0, 0}, 10.0f, 0.0f, 0.0f, 1);
	describe("bench chisel chop across the grain", across);
	CHECK(across.chop && across.blow > 2.0f && across.blow < 3.0f);
	CHECK(has(across, kSlitOnly) && !has(across, kPopsOff) && !has(across, kSplits));
	const CutPlan along = plan_cut(bench, oak.work(), {0, 0, kTop}, kUp, {0, 1, 0}, 10.0f, 0.0f, 0.0f, 1);
	describe("along the grain", along);
	CHECK(along.blow > 1.8f * across.blow && has(along, kSplits));

	oak.add(across.edits());
	const CutPlan second = plan_cut(bench, oak.work(), {0, 0, kTop}, kUp, {1, 0, 0}, 10.0f, 0.0f, 0.0f, 1);
	describe("the second blow", second);
	CHECK(second.depth > across.depth && second.blow < across.blow);

	const Board fresh(mat::Oak);
	const CutPlan edge = plan_cut(bench, fresh.work(), {-77.5f, 0, kTop}, kUp, {-1, 0, 0}, 10.0f, 0.0f, 0.0f, 1);
	describe("2.5 mm from the end, waste towards it", edge);
	CHECK(has(edge, kPopsOff) && edge.chips.size() == 1);

	Chisel mortise = variant("mortise_8", 90.0f), bench8 = variant("bench_12", 90.0f);
	bench8.width = 8.0f;
	const CutPlan heavy = plan_cut(mortise, fresh.work(), {0, 0, kTop}, kUp, {1, 0, 0}, 10.0f, 0.0f, 0.0f, 1);
	const CutPlan light = plan_cut(bench8, fresh.work(), {0, 0, kTop}, kUp, {1, 0, 0}, 10.0f, 0.0f, 0.0f, 1);
	describe("mortise chisel", heavy);
	CHECK(std::fabs(heavy.blow / light.blow - 1.5f) < 0.01f);

	const CutPlan pushed = plan_cut(variant("paring_25", 90.0f), fresh.work(), {0, 0, kTop}, kUp, {1, 0, 0}, 10.0f, 0.0f,
			0.0f, 1);
	describe("paring chisel", pushed);
	CHECK(has(pushed, kNotStruck) && pushed.blow < 0.1f);
}

// The stroke making a plan, pushed to its end and lifted out, commits exactly the plan's
// edits: what the preview showed is what the wood gets. Pushed part way, a prefix of it.
TEST(a_planned_stroke_makes_its_plan) {
	const Board oak(mat::Oak);
	const CutPlan plan = plan_cut(variant("bench_12", 33.0f), oak.work(), {30, 0, kTop}, kUp, {-1, 0, 0}, 50.0f, 0.3f,
			0.0f, 11);
	auto stroke = planned_stroke(plan);
	for (int i = 0; i <= 60; ++i) {
		stroke->move_to(plan.start + plan.path * float(i));
	}
	std::vector<Edit> made = stroke->edits();
	for (const Edit &e : stroke->finish()) {
		made.push_back(e);
	}
	const std::vector<Edit> planned = plan.edits();
	CHECK(made.size() == planned.size());
	for (std::size_t i = 0; i < made.size() && i < planned.size(); ++i) {
		CHECK(std::memcmp(&made[i].prim, &planned[i].prim, sizeof(Primitive)) == 0);
	}
	auto part = planned_stroke(plan);
	part->move_to(plan.start + plan.path * 20.0f);
	CHECK(std::fabs(part->pose().origin.x - (30.0f - 20.0f)) < 1e-3f);
	CHECK(part->edits().size() <= planned.size());
}

namespace {

Edit block(vec3 centre, vec3 half, Op op) {
	Edit e;
	e.prim = Primitive::box(centre, half);
	e.op = op;
	e.material = mat::Ash;
	return e;
}

// Whether the body holds material at p.
bool solid(const Board &b, vec3 p) {
	return b.octree.distance(b.body, p) < 0.0f;
}

} // namespace

// Pared at a raised part (a step 3 mm high across the board), the edge stops at its face:
// the plan ends there and says why, and nothing is cut under the step.
TEST(a_chisel_stops_at_a_step_and_never_cuts_under_it) {
	Board ash;
	ash.add({block({10, 0, kTop + 1.5f}, {10, 60, 1.5f}, Op::Union)}); // x 0..20, 3 mm high
	const CutPlan plan = plan_cut(variant("bench_12", 33.0f), ash.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 80.0f,
			0.3f, 0.0f, 1);
	describe("pared at a 3 mm step 40 mm ahead", plan);
	CHECK(plan.stop == kBlocked && has(plan, kBlocked));
	CHECK(plan.stop_at > 37.0f && plan.stop_at <= 40.0f && std::fabs(plan.length - plan.stop_at) < 1e-4f);
	CHECK(std::fabs(plan.wall - 3.0f) < 0.3f);
	Board cut = ash;
	cut.add(plan.edits());
	bool intact = true;
	for (float x = 0.5f; x < 20.0f; x += 1.0f) {
		for (float z = kTop - 0.25f; z < kTop + 3.0f; z += 0.5f) {
			intact = intact && solid(cut, {x, 0, z});
		}
	}
	CHECK(intact);
	CHECK(!solid(cut, {-20, 0, kTop - 0.15f})); // pared before it
}

// A chisel wider than a groove cannot go down it: its corners meet the walls. A narrower
// one pares its floor.
TEST(a_chisel_too_wide_for_a_groove_stops) {
	Board ash;
	ash.add({block({0, 0, kTop}, {70, 4, 3}, Op::Subtract)}); // 8 mm wide, 3 mm deep, along x
	const vec3 floor{-40, 0, kTop - 3.0f};
	const CutPlan wide = plan_cut(variant("bench_12", 33.0f), ash.work(), floor, kUp, {1, 0, 0}, 40.0f, 0.3f, 0.0f, 1);
	const CutPlan narrow = plan_cut(variant("bench_6", 33.0f), ash.work(), floor, kUp, {1, 0, 0}, 40.0f, 0.3f, 0.0f, 1);
	describe("12 mm chisel in an 8 mm groove", wide);
	describe("6 mm chisel", narrow);
	CHECK(wide.stop == kTooWide && wide.stop_at == 0.0f && wide.edits().empty());
	CHECK(narrow.stop_at < 0.0f && std::fabs(narrow.length - 40.0f) < 1e-3f && std::fabs(narrow.depth - 0.3f) < 1e-3f);
}

// A surface rising ahead, a little at each millimetre (stairs too low to be steps): gently
// (4.6 degrees) the edge follows it up once the chip is as thick as the hand can push;
// steeply (24 degrees) it stalls. Neither takes more force than the hand has.
TEST(a_rising_surface_is_followed_gently_or_stalls) {
	for (const float rise : {0.08f, 0.45f}) {
		Board ash;
		std::vector<Edit> stairs; // x 0..30: rise mm higher at each millimetre
		for (int i = 0; i < 30; ++i) {
			stairs.push_back(block({0.5f * float(i + 30), 0, kTop + rise * (float(i) + 0.5f)},
					{0.5f * float(30 - i), 60, 0.5f * rise}, Op::Union));
		}
		ash.add(stairs);
		const CutPlan plan = plan_cut(variant("bench_12", 33.0f), ash.work(), {-20, 0, kTop}, kUp, {1, 0, 0}, 45.0f,
				0.3f, 0.0f, 1);
		describe(rise < 0.1f ? "at a surface rising 4.6 degrees" : "at one rising 24 degrees", plan);
		CHECK(plan.force <= plan.available * 1.001f);
		if (rise < 0.1f) {
			std::printf("    the floor 45 mm on: %.2f mm above where it started\n", double(-plan.depth_at(45.0f)));
			CHECK(plan.stop_at < 0.0f && plan.depth_at(45.0f) < -1.0f);
		} else {
			std::printf("    stalls %.1f mm on (the rise begins 20 mm on)\n", double(plan.stop_at));
			CHECK(plan.stop == kStalls && plan.stop_at >= 18.0f && plan.stop_at < 25.0f);
		}
	}
}

// A gouge deepens its own channel, pass after pass: the chip over its edge is what stands
// over it there (a crescent), not a block from its floor to the surface either side. A
// chisel along the channel is blocked by its end.
TEST(a_gouge_deepens_its_own_channel) {
	Board ash;
	const Chisel gouge = variant("gouge_7_12", 35.0f);
	const CutPlan first = plan_cut(gouge, ash.work(), {-75, 0, kTop}, kUp, {1, 0, 0}, 35.0f, 2.0f, 0.0f, 1);
	ash.add(first.edits());
	const CutPlan second = plan_cut(gouge, ash.work(), {-66, 0, kTop - first.depth}, kUp, {1, 0, 0}, 26.0f, 2.0f,
			0.0f, 1);
	describe("a #7 gouge's first pass", first);
	describe("its second, in the channel", second);
	CHECK(first.depth > 1.0f && second.stop_at < 0.0f && second.depth > 0.5f);
	CHECK(second.force <= second.available * 1.001f);
	// The channel ends in a wall sloping back at the gouge's angle: a 6 mm chisel pared along
	// its floor is blocked there.
	ash.add(second.edits());
	const float floor = kTop - first.depth - second.depth;
	const CutPlan chisel = plan_cut(variant("bench_6", 30.0f), ash.work(), {-56, 0, floor}, kUp, {1, 0, 0}, 26.0f, 0.2f,
			0.0f, 1);
	describe("a 6 mm chisel along it", chisel);
	CHECK(chisel.stop == kBlocked && chisel.stop_at > 13.0f && chisel.stop_at <= 16.0f && chisel.wall > 1.0f);
	// However its millimetres fall on the sloping end, and however its plane leans: blocked.
	for (const float from : {-56.25f, -56.5f, -56.75f}) {
		for (const float lean : {-0.03f, 0.03f}) {
			const CutPlan p = plan_cut(variant("bench_6", 30.0f), ash.work(), {from, 0, floor},
					gl::normalize(vec3{0, lean, 1}), {1, 0, 0}, 26.0f, 0.2f, 0.0f, 1);
			if (p.stop != kBlocked) {
				std::printf("    from %.2f, leaning %+.2f: %s at %.1f\n", double(from), double(lean),
						warning_names(p.stop).empty() ? "no stop" : warning_names(p.stop).front().c_str(), double(p.stop_at));
			}
			CHECK(p.stop == kBlocked && p.wall > 1.0f);
		}
	}
}

// Nor does the blade go through the work: under a bar standing 2 mm clear of the surface,
// the blade rising behind the edge meets it, and the stroke stops there.
TEST(the_blade_meets_an_overhang) {
	Board ash;
	ash.add({block({0, 0, kTop + 3.5f}, {5, 60, 1.5f}, Op::Union)}); // x -5..5, from 2 mm above the top
	const CutPlan plan = plan_cut(variant("bench_12", 33.0f), ash.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 80.0f,
			0.3f, 0.0f, 1);
	describe("pared under a bar 2 mm clear", plan);
	CHECK(plan.stop == kBladeMeets && plan.stop_at > 38.0f && plan.stop_at < 48.0f);
}

// Tear-out and breakout are splinters along the grain: rounded scoops, never deeper than
// half as much again as the cut (a millimetre at most; breakout two), not boxes. (A pit with
// a flat floor and upright walls fills its bounding box; a rounded one, about two thirds.)
TEST(tear_out_is_shallow_rounded_splinters) {
	const Board oak(mat::Oak);
	const CutPlan uphill = plan_cut(variant("bench_12", 33.0f), oak.work(), {30, 0, kTop}, kUp, {-1, 0, 0}, 60.0f, 0.3f,
			0.0f, 7);
	const CutPlan across = plan_cut(variant("bench_12", 33.0f), oak.work(), {0, 30, kTop}, kUp, {0, 1, 0}, 30.0f, 0.3f,
			0.0f, 1);
	describe("uphill in oak", uphill);
	describe("across, out of the side", across);
	CHECK(!uphill.chips.empty() && has(across, kBreaksOut));
	for (const CutPlan *plan : {&uphill, &across}) {
		std::vector<Edit> taken = plan->floor_edits();
		for (std::size_t i = 0; i < plan->chips.size(); ++i) {
			Chip chip;
			if (!measure_chip(oak.body, oak.octree, plan->chips[i], taken, plan->path, kUp, chip, 20000)) {
				continue;
			}
			const float bottom = kTop - (chip.frame.origin.z - 0.5f * chip.size.z);
			const float box = chip.size.x * chip.size.y * chip.size.z;
			const bool breakout = plan == &across && i + 1 == plan->chips.size();
			const float limit = plan->depth + (breakout ? 2.0f : std::min(1.5f * plan->depth, 1.0f)) + 0.1f;
			std::printf("    chip %zu: %.2f mm deep (limit %.2f), fills %.0f%% of its box\n", i, double(bottom),
					double(limit), double(100.0f * chip.volume / box));
			CHECK(bottom <= limit);
			CHECK(chip.volume < 0.8f * box);
		}
	}
}

// Chopping: a tap drives the slit a third as far as a firm blow, a heavy blow further.
TEST(mallet_blows_tap_firm_heavy) {
	const Board oak(mat::Oak);
	const Chisel bench = variant("bench_12", 90.0f);
	float blows[3];
	const float strength[3] = {0.3f, 1.0f, 1.6f};
	for (int i = 0; i < 3; ++i) {
		blows[i] = plan_cut(bench, oak.work(), {0, 0, kTop}, kUp, {1, 0, 0}, 10.0f, 0.0f, 0.0f, 1, strength[i]).blow;
	}
	std::printf("    tap %.2f mm, firm %.2f mm, heavy %.2f mm\n", double(blows[0]), double(blows[1]), double(blows[2]));
	CHECK(blows[0] < blows[1] && blows[1] < blows[2] && blows[0] < 1.0f);
}
