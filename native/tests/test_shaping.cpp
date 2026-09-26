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

// `strokes` strokes of the tool back and forth over `length` mm from `start` along +x.
std::vector<Edit> worked(Stroke &stroke, vec3 start, float length, int strokes) {
	for (int i = 1; i <= strokes; ++i) {
		stroke.move_to(start + vec3(i % 2 ? length : 0.0f, 0, 0));
	}
	return stroke.edits();
}

} // namespace

// A rasp removes steadily: a cabinet rasp takes about a quarter of a millimetre of ash in
// ten 60 mm strokes; a coarse one twice that, a fine one half, walnut goes faster, pressing
// harder takes more. It never tears out, whichever way it goes.
TEST(a_rasp_removes_steadily_and_never_tears_out) {
	Work_ ash(demo::board(mat::Ash));
	const Wood wood = ash.work().wood({0, 0, 10});
	Rasp cabinet;
	auto stroke = rasp_stroke(cabinet, wood, {-30, 0, kTop}, kUp, {1, 0, 0}, 60.0f);
	ash.apply(worked(*stroke, {-30, 0, kTop}, 60.0f, 10));
	const float taken = kTop - ash.height(0, 0);
	const float expected = cabinet.removal_per_mm(wood) * 600.0f;
	std::printf("    a cabinet rasp, ten 60 mm strokes on ash: %.3f mm (%.3f expected)\n", double(taken), double(expected));
	CHECK(std::fabs(taken - expected) < 0.02f && taken > 0.2f && taken < 0.3f);
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

// Tilted 45 degrees about its stroke along the board's front top arris, a rasp takes a
// chamfer off it: the arris goes, the faces either side keep their places a little way off.
TEST(a_tilted_rasp_chamfers_an_arris) {
	Work_ ash(demo::board(mat::Ash));
	Rasp rasp;
	rasp.tilt_deg = -45.0f; // set on the top face, turned out over the front edge
	// The front top arris runs along x at y = -50, z = 12.5.
	const vec3 arris{-30, -50, kTop};
	auto stroke = rasp_stroke(rasp, ash.work().wood({0, 0, 10}), arris, kUp, {1, 0, 0}, 60.0f);
	for (int i = 1; i <= 30; ++i) {
		stroke->move_to(arris + vec3(i % 2 ? 60.0f : 0.0f, 0, 0));
	}
	ash.apply(stroke->edits());
	const float corner = ash.body.distance({0, -49.6f, kTop - 0.4f}), top = ash.body.distance({0, -40, kTop - 0.1f});
	std::printf("    after 30 strokes: the old corner %.2f mm outside the work, the top face 10 mm in %.2f\n", double(corner),
			double(top));
	CHECK(corner > 0.1f && top < 0.0f);
}

// A card scraper takes about 0.006 mm a 100 mm stroke of ash: twenty strokes, a tenth of a
// millimetre, feathered at its sides.
TEST(a_card_scraper_takes_a_whisper) {
	Work_ ash(demo::board(mat::Ash));
	const Wood wood = ash.work().wood({0, 0, 10});
	CardScraper scraper;
	auto stroke = scraper_stroke(scraper, wood, {-50, 0, kTop}, kUp, {1, 0, 0}, 100.0f);
	ash.apply(worked(*stroke, {-50, 0, kTop}, 100.0f, 20));
	const float taken = kTop - ash.height(0, 0);
	std::printf("    twenty 100 mm strokes: %.3f mm\n", double(taken));
	CHECK(taken > 0.08f && taken < 0.15f);
	CHECK(scraper.removal_per_mm(wood) * 100.0f < 0.01f);
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
