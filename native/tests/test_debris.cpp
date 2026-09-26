#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/catalog.h"
#include "tools/cutting.h"
#include "tools/debris.h"
#include "edit/session.h"
#include "tools/shaping.h"
#include "tools/smoothing.h"

#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

// The workshop board (160 x 100 x 25 mm, centred), as in test_cutting.cpp.
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

// The stroke making `plan`, pushed a millimetre at a time along it and lifted off, with
// what came off gathered after each move (from the body as it was before the stroke).
Debris make(const Board &board, const CutPlan &plan, std::vector<Edit> *edits = nullptr) {
	auto stroke = planned_stroke(plan);
	Debris d;
	const int steps = int(std::ceil(plan.length)) + 2;
	for (int i = 0; i <= steps; ++i) {
		stroke->move_to(plan.start + plan.path * float(i));
		stroke->debris(board.body, board.octree, d);
	}
	std::vector<Edit> made = stroke->edits();
	for (const Edit &e : stroke->finish()) {
		made.push_back(e);
	}
	stroke->debris(board.body, board.octree, d, true);
	if (edits) {
		*edits = made;
	}
	return d;
}

// The volume a cut took off the top of the board over `lo`..`hi` (x, y), by columns: the
// surface's height before and after, looking straight down.
float removed(const Board &before, const Board &after, vec2 lo, vec2 hi, vec2 h);
float removed(const Board &before, const Board &after, vec2 lo, vec2 hi, float h = 0.2f) {
	return removed(before, after, lo, hi, vec2(h));
}

float removed(const Board &before, const Board &after, vec2 lo, vec2 hi, vec2 h) {
	double v = 0.0;
	for (float y = lo.y + 0.5f * h.y; y < hi.y; y += h.y) {
		for (float x = lo.x + 0.5f * h.x; x < hi.x; x += h.x) {
			const vec3 from(x, y, kTop + 2.0f);
			const auto a = raycast(before.body, before.octree, from, -kUp, 40.0f, 1e-4f);
			const auto b = raycast(after.body, after.octree, from, -kUp, 40.0f, 1e-4f);
			if (a && b) {
				v += double(b->t - a->t);
			}
		}
	}
	return float(v) * h.x * h.y;
}

int pieces(const Debris &d) {
	int n = 0;
	for (const ShavingSample &s : d.shaving) {
		n += s.starts;
	}
	return n;
}

} // namespace

// Pared along the board from its end, a bench chisel's shaving is sampled every half
// millimetre from where it went in to where it stopped, as thick as the cut is deep and as
// wide as the edge; its volume is what the cut took off the board. Along the grain it
// comes off in one piece.
TEST(a_paring_cut_reports_its_shaving) {
	const Board ash;
	const CutPlan plan = plan_cut(variant("bench_12", 20.0f), ash.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 40.0f,
			0.3f, 0.0f, 1);
	CHECK(plan.open && std::fabs(plan.depth - 0.3f) < 1e-3f);
	std::vector<Edit> edits;
	const Debris d = make(ash, plan, &edits);
	CHECK(d.ended && d.chips.empty() && d.dust.empty());
	CHECK(!d.shaving.empty());
	if (d.shaving.empty()) {
		return;
	}
	CHECK(d.shaving.front().s < 0.6f && d.shaving.back().s > plan.length - 0.6f);
	bool even = true;
	for (std::size_t i = 0; i < d.shaving.size(); ++i) {
		const ShavingSample &s = d.shaving[i];
		even = even && (i == 0 || std::fabs(s.s - d.shaving[i - 1].s - d.step) < 1e-3f);
		if (s.s < 1.0f) {
			continue; // over the board's rounded end edge it is thinner
		}
		even = even && std::fabs(s.thickness - 0.3f) < 0.02f && std::fabs(s.width - plan.width) < 1e-3f;
		even = even && std::fabs(s.point.z - (kTop - 0.15f)) < 0.02f;
	}
	CHECK(even);
	CHECK(pieces(d) == 1);
	Board cut = ash;
	cut.add(edits);
	const float taken = removed(ash, cut, {-80.0f, -8.0f}, {-30.0f, 8.0f});
	const float shaving = d.shaving_volume();
	std::printf("    shaving %.1f mm^3 in %zu samples, the board lost %.1f mm^3 (%.1f%%)\n", double(shaving),
			d.shaving.size(), double(taken), double(100.0f * (shaving - taken) / taken));
	CHECK(std::fabs(shaving - taken) < 0.15f * taken);
}

// Severing the fibres (across the grain) the shaving crumbles into short pieces; where the
// edge passes over air (an earlier groove) it breaks too, and nothing comes off there.
TEST(a_shaving_breaks_across_the_grain_and_over_a_gap) {
	const Board ash;
	const CutPlan across = plan_cut(variant("bench_12", 20.0f), ash.work(), {0, -49.5f, kTop}, kUp, {0, 1, 0}, 30.0f,
			0.2f, 0.0f, 1);
	const Debris d = make(ash, across);
	std::printf("    across the grain: %d pieces of about %.1f mm in %.0f mm\n", pieces(d), double(across.shaving_piece()),
			double(across.length));
	CHECK(across.grain > 0.15f && pieces(d) >= 2);
	CHECK(std::fabs(float(pieces(d)) - std::ceil(across.length / across.shaving_piece())) <= 1.0f);

	Board grooved;
	grooved.add({[] {
		Edit e;
		e.prim = Primitive::box({-60.0f, 0, kTop}, {2.0f, 60.0f, 1.0f});
		e.op = Op::Subtract;
		return e;
	}()});
	const CutPlan along = plan_cut(variant("bench_12", 20.0f), grooved.work(), {-79.5f, 0, kTop}, kUp, {1, 0, 0}, 40.0f,
			0.3f, 0.0f, 1);
	const Debris g = make(grooved, along);
	bool gap = true, after = false;
	for (const ShavingSample &s : g.shaving) {
		const float x = along.start.x + s.s;
		gap = gap && (x < -62.0f || x > -58.0f); // nothing came off over the groove
		after = after || (x > -58.0f && x < -57.4f && s.starts);
	}
	CHECK(gap && after && pieces(g) == 2);
}

// Against the grain the split tears out below the cut: each chip is reported once, where
// the edge reaches it, measured as the material it took beyond the cut. The shaving and the
// chips together are what the board lost.
TEST(tear_out_chips_are_reported_once) {
	const Board oak(mat::Oak);
	const CutPlan uphill = plan_cut(variant("bench_12", 33.0f), oak.work(), {30, 0, kTop}, kUp, {-1, 0, 0}, 60.0f, 0.3f,
			0.0f, 7);
	CHECK(!uphill.chips.empty());
	std::vector<Edit> edits;
	const Debris d = make(oak, uphill, &edits);
	CHECK(!d.chips.empty() && d.chips.size() <= uphill.chips.size());
	bool sized = true;
	for (const Chip &c : d.chips) {
		sized = sized && c.volume > 0.0f && c.size.x > 0.0f && c.size.y > 0.0f && c.size.z > 0.0f &&
				c.volume <= c.size.x * c.size.y * c.size.z * 1.001f;
		sized = sized && std::fabs(gl::dot(c.frame.z, kUp) - 1.0f) < 1e-4f;
	}
	CHECK(sized);
	Board cut = oak;
	cut.add(edits);
	const float taken = removed(oak, cut, {-34.0f, -10.0f}, {34.0f, 10.0f});
	const float came = d.shaving_volume() + d.chip_volume();
	std::printf("    %zu chips, %.1f mm^3; with the shaving %.1f mm^3, the board lost %.1f mm^3 (%.1f%%)\n", d.chips.size(),
			double(d.chip_volume()), double(came), double(taken), double(100.0f * (came - taken) / taken));
	CHECK(std::fabs(came - taken) < 0.15f * taken);
}

// A chop near the end of the board pops the chip between off with the blow; the slit
// itself takes nothing away (the wood is pressed aside), so there is no shaving.
TEST(a_chop_pops_off_its_chip) {
	const Board oak(mat::Oak);
	const CutPlan edge = plan_cut(variant("bench_12", 90.0f), oak.work(), {-77.5f, 0, kTop}, kUp, {-1, 0, 0}, 10.0f,
			0.0f, 0.0f, 1);
	CHECK(edge.chop && edge.chips.size() == 1);
	auto stroke = planned_stroke(edge);
	Debris d;
	stroke->debris(oak.body, oak.octree, d);
	CHECK(d.chips.empty()); // not struck yet
	stroke->move_to(edge.start);
	stroke->debris(oak.body, oak.octree, d);
	stroke->move_to(edge.start);
	stroke->debris(oak.body, oak.octree, d, true);
	CHECK(d.shaving.empty() && d.chips.size() == 1);
	if (!d.chips.empty()) {
		const Chip &c = d.chips.front();
		std::printf("    popped off: %.1f x %.1f x %.1f mm, %.1f mm^3\n", double(c.size.x), double(c.size.y),
				double(c.size.z), double(c.volume));
		// Between the slit and the end face (2.5 mm less half the slit), as wide as the edge.
		CHECK(c.size.x > 1.5f && c.size.x < 2.8f && std::fabs(c.size.y - 12.0f) < 0.6f);
		CHECK(c.frame.origin.x < -77.5f && c.frame.origin.x > -80.0f);
	}
}

// A spokeshave's shaving is as thick as it is set, the width of its blade.
TEST(a_spokeshave_takes_an_even_shaving) {
	const Board ash;
	Spokeshave shave;
	const CutPlan plan = plan_spokeshave(shave, ash.work(), {-60.0f, 0, kTop}, kUp, {1, 0, 0}, 50.0f, 0.1f, 1);
	const Debris d = make(ash, plan);
	CHECK(!d.shaving.empty());
	bool even = true;
	for (const ShavingSample &s : d.shaving) {
		even = even && std::fabs(s.thickness - plan.depth) < 0.01f && std::fabs(s.width - shave.blade_width) < 1e-3f;
	}
	CHECK(even && pieces(d) == 1);
}

namespace {

// A stroke moved through `path`, what came off gathered after each move from the body as
// it was (a previewed stroke), and its merged cut.
Debris rub(const Board &board, Stroke &stroke, const std::vector<vec3> &path, std::vector<Edit> &edits) {
	Debris d;
	for (const vec3 &p : path) {
		stroke.move_to(p);
		stroke.debris(board.body, board.octree, d);
	}
	edits = stroke.edits();
	stroke.debris(board.body, board.octree, d, true);
	return d;
}

// Back and forth between a and b, `strokes` times, a millimetre a move.
std::vector<vec3> reciprocate(vec3 a, vec3 b, int strokes) {
	std::vector<vec3> path;
	const int steps = int(std::ceil(gl::length(b - a)));
	for (int k = 0; k < strokes; ++k) {
		for (int i = 1; i <= steps; ++i) {
			const float t = float(i) / float(steps);
			path.push_back(k % 2 == 0 ? a + (b - a) * t : b + (a - b) * t);
		}
	}
	return path;
}

} // namespace

// Sawdust is the kerf the saw took: its width times the depth it went down times the
// material along its line (the board's width, across it). It leaves at the kerf's ends,
// the board's two sides.
TEST(sawdust_is_the_kerf_taken) {
	const Board ash;
	auto saw = saw_stroke(Saw{}, {0, 0, kTop}, kUp, {0, 1, 0}, 0.02f, 26.0f);
	std::vector<Edit> edits;
	const Debris d = rub(ash, *saw, reciprocate({0, -30, kTop}, {0, 30, kTop}, 5), edits);
	Board cut = ash;
	cut.add(edits);
	const float taken = removed(ash, cut, {-0.6f, -51.0f}, {0.6f, 51.0f}, vec2(0.05f, 0.5f));
	std::printf("    %zu puffs, %.1f mm^3 of sawdust; the kerf took %.1f mm^3 (%.1f%%)\n", d.dust.size(),
			double(d.dust_volume()), double(taken), double(100.0f * (d.dust_volume() - taken) / taken));
	CHECK(taken > 100.0f && std::fabs(d.dust_volume() - taken) < 0.05f * taken);
	bool ends = !d.dust.empty();
	for (const Dust &puff : d.dust) {
		ends = ends && std::fabs(std::fabs(puff.point.y) - 49.5f) < 1.0f && std::fabs(puff.point.z - kTop) < 1e-3f;
		ends = ends && puff.direction.y * puff.point.y > 0.0f; // thrown out of the kerf's end
	}
	CHECK(ends && d.shaving.empty() && d.chips.empty());
}

// A sanding block's dust is the depth of its pass over the patch it covered; a rasp's, the
// depth of its pass over its face's width and length. Both leave where the tool is.
TEST(sanding_and_rasp_dust_is_what_they_take) {
	const Board ash;
	SandingBlock block;
	block.grit = 60;
	auto sanding = sanding_stroke(block, {0, 0, kTop}, kUp, {1, 0, 0});
	std::vector<vec3> circle;
	for (int i = 1; i <= 240; ++i) {
		const float a = float(i) * 0.1f;
		circle.push_back({12.0f * std::cos(a) - 12.0f, 8.0f * std::sin(a), kTop});
	}
	std::vector<Edit> edits;
	const Debris d = rub(ash, *sanding, circle, edits);
	Board sanded = ash;
	sanded.add(edits);
	const float taken = removed(ash, sanded, {-70.0f, -40.0f}, {40.0f, 40.0f}, 0.5f);
	std::printf("    sanding block: %zu puffs, %.1f mm^3 of dust; the board lost %.1f mm^3 (%.1f%%)\n", d.dust.size(),
			double(d.dust_volume()), double(taken), double(100.0f * (d.dust_volume() - taken) / taken));
	CHECK(taken > 5.0f && std::fabs(d.dust_volume() - taken) < 0.2f * taken);

	const RaspVariant *cabinet = find_rasp("rasp_cabinet");
	CHECK(cabinet != nullptr);
	for (const bool round : {false, true}) {
		Rasp rasp = cabinet ? cabinet->rasp : Rasp{};
		rasp.round = round;
		auto rasping = rasp_stroke(rasp, ash.work().wood({0, 0, 10}), {-30, 0, kTop}, kUp, {1, 0, 0}, 60.0f);
		const Debris r = rub(ash, *rasping, reciprocate({-30, 0, kTop}, {30, 0, kTop}, 10), edits);
		Board rasped = ash;
		rasped.add(edits);
		const float lost = removed(ash, rasped, {-32.0f, -14.0f}, {32.0f, 14.0f}, 0.25f);
		std::printf("    %s rasp: %zu puffs of %.2f mm grains, %.1f mm^3; the board lost %.1f mm^3 (%.1f%%)\n",
				round ? "half-round" : "flat", r.dust.size(), r.dust.empty() ? 0.0 : double(r.dust[0].grain),
				double(r.dust_volume()), double(lost), double(100.0f * (r.dust_volume() - lost) / lost));
		CHECK(lost > 1.0f && std::fabs(r.dust_volume() - lost) < 0.2f * lost);
	}
}

// A sponge's dust is what its flow took out of the material, reported by the flow itself
// (it may be working on another thread): rubbed along the front top arris, as much as the
// arris lost, from where it was rubbed.
TEST(sponge_dust_is_what_its_flow_takes) {
	Body sharp;
	sharp.base = Primitive::box({0, 0, 0}, {80, 50, 12.5f});
	sharp.base_material = mat::Ash;
	EditSession s;
	s.reset(sharp);
	const vec3 bisector = gl::normalize(vec3(0, -1, 1));
	auto stroke = hand_sanding_stroke(SandingSponge{}, {-30, -50, 12.5f}, bisector, {1, 0, 0});
	Debris d;
	for (int pass = 0; pass < 3; ++pass) {
		for (int k = 1; k <= 30; ++k) {
			stroke->move_to({pass % 2 == 0 ? -30.0f + 2.0f * float(k) : 30.0f - 2.0f * float(k), -50, 12.5f});
			if (k % 3 == 0) {
				const StrokeUpdate u = stroke->work(s.body(), s.octree());
				CHECK(u.empty() || s.revise_stroke(u.drop, u.edits, u.changed));
				stroke->debris(s.body(), s.octree(), d);
			}
		}
	}
	stroke->debris(s.body(), s.octree(), d, true);
	s.commit();
	// What the arris lost: the box's material there no longer in the body, slice by slice.
	double lost = 0.0;
	const float h = 0.1f;
	for (float x = -44.5f; x < 45.0f; x += 1.0f) {
		for (float y = -50.0f + 0.5f * h; y < -44.0f; y += h) {
			for (float z = 12.5f - 0.5f * h; z > 6.5f; z -= h) {
				lost += s.octree().distance(s.body(), {x, y, z}) >= 0.0f;
			}
		}
	}
	lost *= double(h * h * 1.0f);
	vec3 centre(0.0f);
	for (const Dust &puff : d.dust) {
		centre = centre + puff.point * puff.volume;
	}
	centre = centre / std::max(d.dust_volume(), 1e-6f);
	std::printf("    %zu puffs, %.2f mm^3 of dust, centred at (%.1f, %.1f, %.1f); the arris lost %.2f mm^3 (%.1f%%)\n",
			d.dust.size(), double(d.dust_volume()), double(centre.x), double(centre.y), double(centre.z), lost,
			100.0 * (double(d.dust_volume()) - lost) / lost);
	CHECK(lost > 1.0 && std::fabs(double(d.dust_volume()) - lost) < 0.1 * lost);
	CHECK(std::fabs(centre.x) < 5.0f && centre.y < -48.0f && centre.z > 10.5f);
}
