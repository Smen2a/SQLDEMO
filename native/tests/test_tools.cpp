#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/tools.h"

#include <algorithm>

using namespace sdf;

namespace {

// Height of the surface under (x, y), found by a ray straight down; NaN where it misses.
struct Heights {
	Body body;
	Octree octree;

	explicit Heights(const Body &b) : body(b) { octree.build(body); }
	float at(float x, float y) const {
		const auto hit = raycast(body, octree, {x, y, 60.0f}, {0, 0, -1}, 200.0f, 1e-4f);
		return hit ? hit->point.z : std::nanf("");
	}
};

Body board_with(const std::vector<Edit> &edits) {
	Body b = demo::board(mat::Ash);
	for (const Edit &e : edits) {
		CHECK(b.add(e));
	}
	return b;
}

} // namespace

TEST(raycast_hits_boxes_and_spheres) {
	Body box;
	box.base = Primitive::box({0, 0, 0}, {20, 10, 5});
	Octree bo;
	bo.build(box);
	auto hit = raycast(box, bo, {3, 2, 50}, {0, 0, -1}, 100.0f, 1e-4f);
	CHECK(hit.has_value());
	if (hit) {
		CHECK_NEAR(hit->point.z, 5.0, 2e-4);
		CHECK_NEAR(hit->t, 45.0, 2e-4);
		CHECK(gl::dot(hit->normal, vec3(0, 0, 1)) > 0.999f);
	}
	hit = raycast(box, bo, {-50, 1, 0}, {1, 0, 0}, 100.0f, 1e-4f);
	CHECK(hit.has_value() && std::fabs(hit->point.x + 20.0f) < 2e-4f && hit->normal.x < -0.999f);
	CHECK(!raycast(box, bo, {100, 100, 50}, {0, 0, -1}, 100.0f).has_value());
	CHECK(!raycast(box, bo, {3, 2, 50}, {0, 0, -1}, 40.0f).has_value()); // stops short

	Body ball;
	ball.base = Primitive::sphere({0, 0, 0}, 10);
	Octree so;
	so.build(ball);
	t::Rng rng(4);
	for (int i = 0; i < 50; ++i) {
		const vec3 dir = rng.unit(), from = -dir * 40.0f;
		hit = raycast(ball, so, from, dir, 100.0f, 1e-4f);
		CHECK(hit.has_value() && std::fabs(gl::length(hit->point) - 10.0f) < 2e-4f &&
				gl::dot(hit->normal, -dir) > 0.999f);
	}

	// A box turned by a Frame: its local z is the frame's normal.
	const vec3 n = gl::normalize(vec3(0, 1, 1));
	const tools::Frame f = tools::Frame::at({0, 0, 0}, n, {1, 0, 0});
	Body turned;
	turned.base = Primitive::box({0, 0, 0}, {5, 5, 2}, 0, f.rotation());
	Octree to;
	to.build(turned);
	hit = raycast(turned, to, n * 50.0f, -n, 100.0f, 1e-4f);
	CHECK(hit.has_value() && std::fabs(hit->t - 48.0f) < 2e-4f && gl::dot(hit->normal, n) > 0.999f);
}

TEST(chisel_pares_to_depth_across_its_width) {
	const tools::Chisel chisel; // 12 mm wide
	const float top = 12.5f, depth = 1.5f;
	const vec3 start(-30, 0, top), end(20, 0, top), up(0, 0, 1);
	std::vector<Edit> cut = chisel.paring(start, end, up, depth);
	CHECK(cut.size() == 2);
	cut.push_back(chisel.lift_out(chisel.edge(start, end, up, depth), end - start, up, depth));
	const Heights h(board_with(cut));
	const float ramp = depth / std::tan(chisel.approach_deg * 3.14159265f / 180.0f);
	for (float x = -30 + ramp + 0.5f; x < 19.5f; x += 2.0f) {
		for (float y : {-5.5f, 0.0f, 5.5f}) {
			CHECK_NEAR(h.at(x, y), top - depth, 2e-3);
		}
		CHECK_NEAR(h.at(x, 6.5f), top, 2e-3); // beside the blade
	}
	CHECK_NEAR(h.at(-31.0f, 0.0f), top, 2e-3); // before the entry
	const float in_ramp = h.at(-30 + ramp * 0.5f, 0.0f);
	CHECK(in_ramp > top - depth + 0.2f && in_ramp < top - 0.2f);
	const float lifting = h.at(20.0f + ramp * 0.5f, 0.0f); // the lift-out rises again
	CHECK(lifting > top - depth + 0.2f && lifting < top - 0.2f);
	CHECK_NEAR(h.at(20.0f + ramp + 1.0f, 0.0f), top, 2e-3);
	// A push shorter than the ramp is all ramp.
	CHECK(chisel.paring(start, start + vec3(2, 0, 0), up, depth).size() == 1);
	CHECK(chisel.paring(start, start, up, depth).empty());
}

TEST(saw_kerf_is_narrow_and_as_deep_as_sawn) {
	const tools::Saw saw;
	const float top = 12.5f;
	const Heights h(board_with({saw.kerf_cut({0, 0, top}, {0, 1, 0}, {0, 0, 1}, 6.0f)}));
	for (float y = -45.0f; y <= 45.0f; y += 9.0f) {
		CHECK_NEAR(h.at(0.0f, y), top - 6.0f, 2e-3);
		CHECK_NEAR(h.at(saw.kerf * 0.5f + 0.1f, y), top, 2e-3);
		CHECK_NEAR(h.at(-saw.kerf * 0.5f - 0.1f, y), top, 2e-3);
	}
	// Sawn right through: nothing left under the kerf.
	const Heights through(board_with({saw.kerf_cut({0, 0, top}, {0, 1, 0}, {0, 0, 1}, 30.0f)}));
	CHECK(std::isnan(through.at(0.0f, 10.0f)));
	CHECK_NEAR(through.at(1.0f, 10.0f), top, 2e-3);
}

TEST(sanding_lowers_its_footprint_and_feathers_the_edge) {
	const tools::SandingBlock block;
	const float top = 12.5f, depth = 0.3f;
	const tools::Frame plane = tools::Frame::at({0, 0, top}, {0, 0, 1}, {1, 0, 0});
	const Heights h(board_with({block.pass(plane, {-20, -10}, {20, 10}, depth)}));
	for (float x = -17.0f; x <= 17.0f; x += 4.25f) {
		for (float y : {-7.0f, 0.0f, 7.0f}) {
			CHECK_NEAR(h.at(x, y), top - depth, 2e-3);
		}
	}
	CHECK_NEAR(h.at(24.0f, 0.0f), top, 2e-3);
	CHECK_NEAR(h.at(0.0f, 14.0f), top, 2e-3);
	// Across the edge the surface climbs steadily, never dipping below the pass.
	float previous = h.at(15.0f, 0.0f), lowest = previous;
	bool monotone = true;
	for (float x = 15.05f; x <= 24.0f; x += 0.05f) {
		const float z = h.at(x, 0.0f);
		monotone = monotone && z >= previous - 1e-4f;
		lowest = std::min(lowest, z);
		previous = z;
	}
	CHECK(monotone);
	CHECK(lowest >= top - depth - 2e-3f);
	tools::SandingBlock fine;
	fine.grit = 240;
	CHECK(block.removal_per_mm() > fine.removal_per_mm());
}

TEST(tool_models_are_made_of_their_materials) {
	auto material_at = [](const Body &b, vec3 p) {
		const Sample s = b.sample(p);
		CHECK(s.d < 0.0f);
		return int(s.t < 0.5f ? s.m0 : s.m1);
	};
	const tools::Chisel chisel;
	const Body c = chisel.model();
	const float a = chisel.approach_deg * 3.14159265f / 180.0f;
	const vec3 u(-std::cos(a), 0, std::sin(a)), v(std::sin(a), 0, std::cos(a));
	CHECK(material_at(c, u * 40.0f + v * (chisel.thickness * 0.5f)) == mat::Steel);
	CHECK(material_at(c, u * 150.0f + v * (chisel.thickness * 0.5f)) == mat::Ash);
	CHECK(c.sample(u * 0.3f + v * (chisel.thickness * 0.9f)).d > 0.0f); // ground away by the bevel
	CHECK(c.bounds().hi.z > 60.0f); // the handle rises behind the edge

	const Body s = tools::Saw{}.model();
	CHECK(material_at(s, {0, 0, 30}) == mat::Steel);
	CHECK(material_at(s, {151, 0, 20}) == mat::Walnut);
	CHECK(material_at(s, {0, 0, 63}) == mat::Brass);
	CHECK(s.sample({153, 0, 58}).d > 0.0f); // the hand hole

	const Body b = tools::SandingBlock{}.model();
	CHECK(material_at(b, {0, 0, 0.4f}) == mat::Abrasive);
	CHECK(material_at(b, {0, 0, 10}) == mat::Cork);
}

// Driving the tools through their strokes cuts what the direct cuts do, however the motion
// arrives.
TEST(tool_strokes_cut_what_their_tools_cut) {
	const float top = 12.5f;
	const vec3 up(0, 0, 1);
	auto run = [](tools::Stroke &stroke, const std::vector<vec3> &path) {
		std::vector<Edit> edits;
		for (const vec3 &p : path) {
			const tools::StrokeUpdate u = stroke.move_to(p);
			CHECK(u.drop <= edits.size());
			edits.resize(edits.size() - std::min(u.drop, edits.size()));
			edits.insert(edits.end(), u.edits.begin(), u.edits.end());
		}
		const std::vector<Edit> end = stroke.finish();
		edits.insert(edits.end(), end.begin(), end.end());
		return edits;
	};

	// Chisel: pushed in uneven steps, pulled back once (no effect), then on.
	const tools::Chisel chisel;
	auto push = tools::chisel_stroke(chisel, {-30, 0, top}, up, {1, 0, 0}, 1.5f);
	const Heights stepped(board_with(run(*push, {{-29, 0, top}, {-26, 0, top}, {-10, 0.3f, top}, {-20, 0, top}, {20, 0, top}})));
	std::vector<Edit> direct = chisel.paring({-30, 0, top}, {20, 0, top}, up, 1.5f);
	direct.push_back(chisel.lift_out(chisel.edge({-30, 0, top}, {20, 0, top}, up, 1.5f), {1, 0, 0}, up, 1.5f));
	const Heights reference(board_with(direct));
	for (float x = -32.0f; x <= 26.0f; x += 1.5f) {
		for (float y : {-5.0f, 0.0f, 5.0f, 7.0f}) {
			CHECK_NEAR(stepped.at(x, y), reference.at(x, y), 0.03);
		}
	}
	CHECK_NEAR(push->pose().origin.x, 20.0, 1e-3);
	// A long push stays a few edits: a ramp, 10 mm pieces and the lift-out.
	auto long_push = tools::chisel_stroke(chisel, {-60, 0, top}, up, {1, 0, 0}, 1.0f);
	std::vector<vec3> steps;
	for (float x = -58.0f; x <= 40.0f; x += 0.7f) {
		steps.push_back({x, 0, top});
	}
	const std::vector<Edit> pieces = run(*long_push, steps);
	CHECK(pieces.size() <= 14);
	// And its pieces join without leaving slivers: halfway down the flat run, the nearest
	// surface is the floor, half a millimetre away (a zero-thickness wall would read 0).
	const Body pared = board_with(pieces);
	float clearance = 1.0f;
	for (float x = -55.0f; x <= 37.0f; x += 0.01f) {
		clearance = std::min(clearance, pared.sample_exhaustive({x, 0.0f, top - 0.5f}).d);
	}
	CHECK_NEAR(clearance, 0.5, 1e-3);
	CHECK_NEAR(push->pose().origin.z, top - 1.5, 1e-3);

	// Saw: 20 strokes of 20 mm at 0.02 mm per mm: 8 mm deep, in slices.
	auto sawing = tools::saw_stroke(tools::Saw{}, {0, 0, top}, up, {0, 1, 0}, 0.02f);
	std::vector<vec3> strokes;
	for (int k = 0; k < 20; ++k) {
		strokes.push_back({0, k % 2 ? -10.0f : 10.0f, top});
	}
	strokes.insert(strokes.begin(), vec3(0, 0, top));
	const std::vector<Edit> slices = run(*sawing, strokes);
	CHECK(slices.size() >= 7 && slices.size() <= 9); // 1 mm slices, the last one open
	const Heights sawn(board_with(slices));
	for (float y = -40.0f; y <= 40.0f; y += 10.0f) {
		CHECK_NEAR(sawn.at(0.0f, y), top - 0.02f * (10.0f + 20.0f * 19.0f), 2e-3); // 390 mm of travel
		CHECK_NEAR(sawn.at(0.6f, y), top, 2e-3);
	}
	// Down the middle of the kerf the nearest surface is a wall, 0.4 mm away: no seams.
	float in_kerf = 1.0f;
	for (float z = top - 0.5f; z >= top - 7.3f; z -= 0.005f) {
		in_kerf = std::min(in_kerf, sawn.body.sample_exhaustive({0.0f, 5.0f, z}).d);
	}
	CHECK_NEAR(in_kerf, 0.4, 1e-3);

	// Sawn through, the saw stops going deeper.
	auto through = tools::saw_stroke(tools::Saw{}, {0, 0, top}, up, {0, 1, 0}, 0.05f, 26.0f);
	for (int k = 0; k < 40; ++k) {
		through->move_to({0, k % 2 ? -10.0f : 10.0f, top});
	}
	CHECK_NEAR(-through->pose().origin.z + top, 26.0, 1e-3);

	// Sanding: the pass replaces itself as the block moves; the last one stands.
	auto rub = tools::sanding_stroke(tools::SandingBlock{}, {0, 0, top}, up, {1, 0, 0});
	const std::vector<Edit> sanded = run(*rub, {{10, 0, top}, {-10, 0, top}, {10, 0, top}, {-10, 0, top}});
	CHECK(sanded.size() == 1);
	const Heights h(board_with(sanded));
	CHECK_NEAR(h.at(0.0f, 0.0f), top - tools::SandingBlock{}.removal_per_mm() * 70.0f, 2e-3);
}
