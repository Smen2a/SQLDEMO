#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/shaping.h"

#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

constexpr float kTop = 12.5f;
const vec3 kUp{0, 0, 1};

struct Work_ {
	Body body;
	Octree octree;
	MaterialTable materials = MaterialTable::standard();
	explicit Work_(const Body &b) : body(b) { octree.build(body); }
	Work work() const { return {body, octree, materials}; }
	float height(float x, float y) const {
		const auto hit = raycast(body, octree, {x, y, 80.0f}, {0, 0, -1}, 200.0f, 1e-4f);
		return hit ? hit->point.z : std::nanf("");
	}
	void apply(const std::vector<Edit> &edits) {
		for (const Edit &e : edits) {
			CHECK(body.add(e));
		}
		octree.build(body);
	}
};

} // namespace

// A rasp removes steadily, on the push: a cabinet rasp takes 0.05 mm of oak in a 150 mm
// push with all its face on the work (more where it bears on less: here its 200 mm face
// overhangs the board's end); nothing on the pull. A coarse one twice that, a fine one
// half, walnut goes faster, pressing harder takes more. It never tears out.
TEST(a_rasp_removes_steadily_on_the_push_and_never_tears_out) {
	Work_ ash(demo::board(mat::Ash));
	const Wood wood = ash.work().wood({0, 0, 10});
	const Wood oak = Wood::of(MaterialTable::standard()[mat::Oak]);
	Rasp cabinet;
	CHECK(std::fabs(cabinet.removal_per_mm(oak) * 150.0f - 0.05f) < 0.001f);
	auto stroke = rasp_stroke(cabinet, ash.work(), {-30, 0, kTop}, kUp, {1, 0, 0}, 60.0f);
	stroke->move_to({30, 0, kTop});
	const float pushed = stroke->state().depth;
	stroke->move_to({-30, 0, kTop});
	CHECK(stroke->state().depth == pushed && pushed > 0.0f); // the pull takes nothing
	for (int i = 1; i <= 8; ++i) {
		stroke->move_to({i % 2 ? 30.0f : -30.0f, 0, kTop});
	}
	ash.apply(stroke->edits());
	const float taken = kTop - ash.height(0, 0);
	// Five 60 mm pushes, its face three quarters on the board.
	const float full = cabinet.removal_per_mm(wood) * 300.0f;
	std::printf("    a cabinet rasp, five 60 mm pushes on ash: %.3f mm (%.3f bearing fully, on %.0f%% of its face)\n",
			double(taken), double(full), double(100.0f * stroke->state().contact));
	CHECK(taken > full / 0.8f - 0.01f && taken < full / 0.75f + 0.01f);
	CHECK(stroke->edits().size() == 1); // one pass, no chips, uphill or down

	Rasp coarse = cabinet, fine = cabinet, hard = cabinet;
	coarse.coarseness = 1.0f;
	fine.coarseness = 0.25f;
	hard.pressure = 2.0f;
	CHECK(std::fabs(coarse.removal_per_mm(wood) - 2.0f * cabinet.removal_per_mm(wood)) < 1e-7f);
	CHECK(std::fabs(fine.removal_per_mm(wood) - 0.5f * cabinet.removal_per_mm(wood)) < 1e-7f);
	CHECK(std::fabs(hard.removal_per_mm(wood) - 2.0f * cabinet.removal_per_mm(wood)) < 1e-7f);
	const Work_ walnut(demo::board(mat::Walnut));
	CHECK(cabinet.removal_per_mm(walnut.work().wood({0, 0, 10})) > cabinet.removal_per_mm(wood));
}

// Its stiff face rests on what stands highest: rasped over a raised strip, it takes the
// strip down and leaves the face beside it alone.
TEST(a_rasp_rests_on_the_high_spots) {
	Body b = demo::board(mat::Ash);
	Edit strip;
	strip.prim = Primitive::box({0, 0, kTop + 0.5f}, {30, 4, 0.5f});
	strip.op = Op::Union;
	strip.material = mat::Ash;
	CHECK(b.add(strip));
	Work_ raised(b);
	auto stroke = rasp_stroke(Rasp{}, raised.work(), {-30, 0, kTop + 1.0f}, kUp, {1, 0, 0}, 60.0f);
	for (int i = 1; i <= 6; ++i) {
		stroke->move_to({i % 2 ? 30.0f : -30.0f, 0, kTop + 1.0f});
	}
	raised.apply(stroke->edits());
	const float took = kTop + 1.0f - raised.height(0, 0);
	std::printf("    three pushes over an 8 mm strip: %.2f mm off it, on %.0f%% of its face\n", double(took),
			double(100.0f * stroke->state().contact));
	CHECK(took > 0.1f && took < 1.0f);
	CHECK(std::fabs(raised.height(0, 10) - kTop) < 1e-3f);
}

// Set on the board with a normal two degrees off (as the pointer's can be), a rasp still
// lies flat on it and takes an even cut across its face, not a wedge off one side; nor does
// an earlier shallow cut under part of its long face tip it.
TEST(a_rasp_set_a_little_askew_lies_flat) {
	Body board = demo::board(mat::Ash);
	Edit shaved;
	shaved.prim = Primitive::box({-65, 0, kTop}, {15, 60, 0.04f});
	shaved.op = Op::Subtract;
	CHECK(board.add(shaved)); // 0.04 mm off x -80..-50
	Work_ ash(board);
	auto stroke = rasp_stroke(Rasp{}, ash.work(), {-30, 0, kTop}, gl::normalize(vec3(0, -0.036f, 1)), {1, 0, 0}, 60.0f,
			5.0f);
	for (int i = 1; i <= 5; ++i) {
		stroke->move_to({i % 2 ? 30.0f : -30.0f, 0, kTop});
	}
	ash.apply(stroke->edits());
	const float left = kTop - ash.height(0, -8), right = kTop - ash.height(0, 8);
	const float before = kTop - ash.height(-20, 0), after = kTop - ash.height(20, 0);
	std::printf("    set 2 degrees askew: %.3f and %.3f mm off either side, %.3f and %.3f along; on %.0f%% of its face\n",
			double(left), double(right), double(before), double(after), double(100.0f * stroke->state().contact));
	CHECK(left > 0.1f && std::fabs(left - right) < 0.005f && std::fabs(before - after) < 0.005f);
	CHECK(stroke->state().contact > 0.5f);
}

// Tilted 45 degrees about its stroke along the board's front top arris, a rasp takes a
// chamfer off it: the arris goes, the faces either side keep their places a little way off.
TEST(a_tilted_rasp_chamfers_an_arris) {
	Work_ ash(demo::board(mat::Ash));
	Rasp rasp;
	rasp.tilt_deg = -45.0f; // set on the top face, turned out over the front edge
	// The front top arris runs along x at y = -50, z = 12.5.
	const vec3 arris{-30, -50, kTop};
	auto stroke = rasp_stroke(rasp, ash.work(), arris, kUp, {1, 0, 0}, 60.0f);
	for (int i = 1; i <= 30; ++i) {
		stroke->move_to(arris + vec3(i % 2 ? 60.0f : 0.0f, 0, 0));
	}
	ash.apply(stroke->edits());
	const float corner = ash.body.distance({0, -49.6f, kTop - 0.4f}), top = ash.body.distance({0, -40, kTop - 0.1f});
	std::printf("    after 15 pushes: %.2f mm into the corner, the old corner %.2f mm outside the work, the top face "
				"10 mm in %.2f\n",
			double(stroke->state().depth), double(corner), double(top));
	CHECK(corner > 0.1f && top < 0.0f);
}

// A card scraper takes a hundredth of a millimetre of oak from each point its burr is pushed
// over, however long the stroke: ten 100 mm pushes, a tenth of a millimetre; nothing on the
// pull; only as far as it went.
TEST(a_card_scraper_takes_a_whisper) {
	Work_ ash(demo::board(mat::Ash));
	const Wood wood = ash.work().wood({0, 0, 10});
	CardScraper scraper;
	CHECK(std::fabs(scraper.per_pass(Wood::of(MaterialTable::standard()[mat::Oak])) - 0.01f) < 1e-5f);
	auto stroke = scraper_stroke(scraper, ash.work(), {-50, 0, kTop}, kUp, {1, 0, 0}, 100.0f);
	stroke->move_to({50, 0, kTop});
	const float one = stroke->state().depth;
	stroke->move_to({-50, 0, kTop});
	CHECK(stroke->state().depth == one); // the pull takes nothing
	for (int i = 1; i < 19; ++i) {
		stroke->move_to({i % 2 ? 50.0f : -50.0f, 0, kTop});
	}
	ash.apply(stroke->edits());
	const float taken = kTop - ash.height(0, 0);
	std::printf("    one 100 mm push: %.4f mm; ten: %.3f mm\n", double(one), double(taken));
	CHECK(std::fabs(one - scraper.per_pass(wood)) < 1e-4f);
	CHECK(std::fabs(taken - 10.0f * scraper.per_pass(wood)) < 0.01f);

	// Pushed half the way only, it scrapes only there.
	Work_ half(demo::board(mat::Ash));
	auto short_ = scraper_stroke(scraper, half.work(), {-50, 0, kTop}, kUp, {1, 0, 0}, 100.0f, 10.0f);
	for (int i = 1; i <= 10; ++i) {
		short_->move_to({i % 2 ? 0.0f : -50.0f, 0, kTop});
	}
	half.apply(short_->edits());
	std::printf("    pushed 50 mm of 100: %.3f mm off at 25 mm, %.3f at 75 mm\n", double(kTop - half.height(-25, 0)),
			double(kTop - half.height(25, 0)));
	CHECK(kTop - half.height(-25, 0) > 0.3f && std::fabs(half.height(25, 0) - kTop) < 1e-3f);
}

// A spokeshave's sole sets its shaving: an even 0.1 mm on the flat top of the board from
// its first millimetre (no ramp, unlike a chisel mid-face), within what two hands push.
TEST(a_spokeshave_takes_an_even_shaving_on_a_flat_face) {
	const Work_ ash(demo::board(mat::Ash));
	const CutPlan plan = plan_spokeshave(Spokeshave{}, ash.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 60.0f, 0.1f, 1);
	std::printf("    %.2f mm deep, %.0f of %.0f N, from %.2f to %.2f mm along\n", double(plan.depth), double(plan.force),
			double(plan.available), double(plan.floor.front().x), double(plan.length));
	CHECK(plan.open && std::fabs(plan.depth - 0.1f) < 1e-4f && plan.force < plan.available);
	for (const vec2 &f : plan.floor) {
		CHECK(std::fabs(f.y - 0.1f) < 0.01f);
	}
	CHECK(!plan.edits().empty());
}

// On a convex surface its short sole rocks, and the edge follows the curve, 0.1 mm below it;
// over a hollow shorter than its sole it bridges and leaves the hollow alone.
TEST(a_spokeshave_follows_a_curve_and_bridges_a_hollow) {
	Body crown;
	crown.base = Primitive::cylinder({0, 0, -40}, 60.0f, 50.0f); // axis along y: the top curves along x
	crown.base_material = mat::Ash;
	crown.grain_axis = {1, 0, 0};
	const Work_ convex(crown);
	const vec3 top{-30, 0, 20.0f};
	const vec3 at = top + vec3(0, 0, convex.height(-30, 0) - 20.0f);
	const CutPlan plan = plan_spokeshave(Spokeshave{}, convex.work(), at, kUp, {1, 0, 0}, 60.0f, 0.1f, 1);
	float worst = 0.0f;
	for (float s = 5.0f; s <= 55.0f; s += 5.0f) {
		const float surface = convex.height(at.x + s, 0) - at.z, floor = -plan.depth_at(s);
		worst = std::max(worst, std::fabs(surface - floor - 0.1f));
	}
	std::printf("    over a 60 mm radius crown: the edge within %.3f mm of 0.1 mm below the surface\n", double(worst));
	CHECK(worst < 0.03f);

	Body hollow = demo::board(mat::Ash);
	CHECK(hollow.add(tools::Chisel{}.paring({-5, -60, kTop}, {-5, 60, kTop}, kUp, 1.0f)[0])); // a groove across
	Work_ grooved(hollow);
	const CutPlan bridged = plan_spokeshave(Spokeshave{}, grooved.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 60.0f, 0.1f, 1);
	std::printf("    over a 1 mm groove: the edge %.2f mm below the top in it, %.2f beyond\n",
			double(bridged.depth_at(35.0f)), double(bridged.depth_at(10.0f)));
	CHECK(std::fabs(bridged.depth_at(10.0f) - 0.1f) < 0.02f);
	CHECK(bridged.depth_at(35.0f) < 0.3f); // it does not dive into the groove
}

// Against the grain it tears out, but less than a chisel: its mouth keeps the split short.
// Its toe, half a sole ahead of the blade, stops at a step it cannot ride: the stroke ends
// half a sole short of it, and says why.
TEST(a_spokeshave_stops_at_a_step_ahead) {
	Body b = demo::board(mat::Ash);
	Edit step;
	step.prim = Primitive::box({30, 0, kTop + 1.5f}, {10, 60, 1.5f});
	step.op = Op::Union;
	step.material = mat::Ash;
	CHECK(b.add(step)); // 3 mm high from x = 20 to 40
	const Work_ w(b);
	const CutPlan plan = plan_spokeshave(Spokeshave{}, w.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 80.0f, 0.1f, 1);
	std::printf("    stops at %.1f mm (%s, a %.2f mm step)\n", double(plan.stop_at),
			warning_names(plan.stop).empty() ? "" : warning_names(plan.stop)[0].c_str(), double(plan.wall));
	CHECK(plan.stop == kBlocked && std::fabs(plan.stop_at - 39.0f) < 1.5f && std::fabs(plan.wall - 3.0f) < 0.2f);
	CHECK(std::fabs(plan.length - plan.stop_at) < 1e-4f);
}

// Two hands push the chip's own section: on an edge narrower than the blade it goes deeper
// than on a wide face; and no deeper than its mouth passes.
TEST(a_spokeshave_goes_deeper_on_a_narrow_edge_up_to_its_mouth) {
	Body oak = demo::board(mat::Oak);
	Body strip;
	strip.base = Primitive::box({0, 0, 0}, {80, 10, 12.5f});
	strip.base_material = mat::Oak;
	strip.grain_axis = oak.grain_axis;
	Body stick = strip;
	stick.base = Primitive::box({0, 0, 0}, {80, 2.5f, 12.5f});
	const Work_ wide(oak), narrow(strip), thin(stick);
	const CutPlan face = plan_spokeshave(Spokeshave{}, wide.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 60.0f, 0.8f, 1);
	const CutPlan edge = plan_spokeshave(Spokeshave{}, narrow.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 60.0f, 0.8f, 1);
	const CutPlan mouth = plan_spokeshave(Spokeshave{}, thin.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 60.0f, 1.5f, 1);
	std::printf("    in oak, asked 0.8 mm: %.2f mm on the face (%.0f N), %.2f on a 20 mm edge (%.0f N); asked 1.5 on a "
				"5 mm stick: %.2f\n",
			double(face.depth), double(face.force), double(edge.depth), double(edge.force), double(mouth.depth));
	CHECK((face.warnings & kShallow) && face.force <= face.available + 1.0f);
	CHECK(edge.depth > 2.0f * face.depth);
	CHECK(std::fabs(mouth.depth - Spokeshave{}.mouth) < 1e-4f && (mouth.warnings & kMouth));
}

// Set with a normal two degrees off across its blade, its sole still lies flat on the face:
// an even shaving across, not a wedge.
TEST(a_spokeshave_set_askew_lies_flat) {
	Work_ ash(demo::board(mat::Ash));
	const CutPlan plan = plan_spokeshave(Spokeshave{}, ash.work(), {-40, 0, kTop}, gl::normalize(vec3(0, -0.036f, 1)),
			{1, 0, 0}, 60.0f, 0.2f, 1);
	ash.apply(plan.edits());
	const float left = kTop - ash.height(-10, -18), right = kTop - ash.height(-10, 18);
	std::printf("    set 2 degrees askew: %.3f and %.3f mm off either side (planned %.3f)\n", double(left), double(right),
			double(plan.depth));
	CHECK(std::fabs(left - plan.depth) < 0.01f && std::fabs(right - plan.depth) < 0.01f);
}

TEST(a_spokeshave_tears_out_uphill) {
	const Work_ oak(demo::board(mat::Oak));
	int uphill = 0, downhill = 0;
	for (std::uint32_t seed = 1; seed <= 20; ++seed) {
		uphill += int(plan_spokeshave(Spokeshave{}, oak.work(), {40, 0, kTop}, kUp, {-1, 0, 0}, 70.0f, 0.1f, seed).chips.size());
		downhill += int(plan_spokeshave(Spokeshave{}, oak.work(), {-40, 0, kTop}, kUp, {1, 0, 0}, 70.0f, 0.1f, seed).chips.size());
	}
	std::printf("    tear-out chips in 20 passes: %d uphill, %d downhill\n", uphill, downhill);
	CHECK(uphill > 0 && downhill == 0);
}
