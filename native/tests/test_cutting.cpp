#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "tools/catalog.h"
#include "tools/cutting.h"

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
// the difference until it levels. Diving steeply, it digs in.
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
	CHECK(has(steep, kDigsIn) && !steep.chips.empty());
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
