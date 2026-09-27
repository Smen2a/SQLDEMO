#include "test.h"

#include "body/materials.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "eval/query.h"
#include "tools/cutting.h"
#include "tools/tools.h"

#include <cstdio>

using namespace sdf;
using namespace sdf::tools;

namespace {

// A block of wood, its top at z = 12.5 (or `top`), `wide` across the saw's line (y) and
// `long_` along x.
struct Wood_ {
	Body body;
	Octree octree;
	MaterialTable materials = MaterialTable::standard();

	Wood_(std::uint16_t wood, float wide, float long_ = 80.0f, float top = 12.5f, float bottom = -12.5f) {
		body.base = Primitive::box({0, 0, 0.5f * (top + bottom)}, {0.5f * long_, 0.5f * wide, 0.5f * (top - bottom)});
		body.base_material = wood;
		octree.build(body);
	}
	Work work() const { return {body, octree, materials}; }
	float at(float x, float y) const {
		const auto hit = raycast(body, octree, {x, y, 200.0f}, {0, 0, -1}, 400.0f, 1e-4f);
		return hit ? hit->point.z : std::nanf("");
	}
};

constexpr float kTop = 12.5f;
const vec3 kUp{0, 0, 1}, kAcross{0, 1, 0};

// The saw pushed (towards -y, its toe) from +100 to -100 and pulled back, `strokes` times,
// a millimetre a move; stops once it is through.
void strokes(Stroke &saw, int n, float x = 0.0f) {
	for (int k = 0; k < n && !saw.separation(); ++k) {
		for (float y = 99.0f; y >= -100.0f; y -= 1.0f) {
			saw.move_to({x, y, kTop});
		}
		for (float y = -99.0f; y <= 100.0f; y += 1.0f) {
			saw.move_to({x, y, kTop});
		}
	}
}

} // namespace

// A tenon saw cuts on the push, a bite a tooth: 0.0038 mm a millimetre pushed through a
// 25 mm chord of oak; nothing pulled. A chord four times as long goes a quarter as fast;
// walnut, softer, faster.
TEST(a_saw_cuts_on_the_push_at_the_real_rate) {
	const Wood_ oak(mat::Oak, 25.0f);
	auto saw = saw_stroke(Saw{}, oak.work(), {0, 0, kTop}, kUp, kAcross);
	for (float y = 1.0f; y <= 100.0f; y += 1.0f) {
		saw->move_to({0, y, kTop}); // pulled out towards the handle
	}
	CHECK(saw->state().depth == 0.0f);
	for (float y = 99.0f; y >= -100.0f; y -= 1.0f) {
		saw->move_to({0, y, kTop});
	}
	const float push = saw->state().depth;
	std::printf("    a 200 mm push through 25 mm of oak: %.3f mm\n", double(push));
	CHECK(std::fabs(push - 0.0038f * 200.0f) < 0.03f * push);

	const Wood_ wide(mat::Oak, 100.0f), walnut(mat::Walnut, 25.0f);
	auto across = saw_stroke(Saw{}, wide.work(), {0, 0, kTop}, kUp, kAcross);
	auto soft = saw_stroke(Saw{}, walnut.work(), {0, 0, kTop}, kUp, kAcross);
	for (float y = -1.0f; y >= -100.0f; y -= 1.0f) {
		across->move_to({0, y, kTop});
		soft->move_to({0, y, kTop});
	}
	std::printf("    100 mm push: through 100 mm of oak %.3f mm, 25 mm of walnut %.3f mm\n",
			double(across->state().depth), double(soft->state().depth));
	CHECK(std::fabs(across->state().depth / (0.5f * push) - 0.25f) < 0.02f);
	CHECK(std::fabs(soft->state().depth / (0.5f * push) - Wood::of(MaterialTable::standard()[mat::Oak]).hardness /
			Wood::of(MaterialTable::standard()[mat::Walnut]).hardness) < 0.03f);
}

// Through 25 mm of oak 50 mm wide in about 70 strokes of 200 mm (the plan says 50 to 80).
TEST(a_tenon_saw_goes_through_oak_in_seventy_strokes) {
	const Wood_ oak(mat::Oak, 50.0f);
	auto saw = saw_stroke(Saw{}, oak.work(), {0, 0, kTop}, kUp, kAcross, 1.0f, 1.0f, 26.0f);
	int n = 0;
	while (n < 200 && !saw->separation()) {
		strokes(*saw, 1);
		++n;
	}
	std::printf("    through in %d strokes\n", n);
	CHECK(n >= 50 && n <= 80);
	CHECK(saw->state().limit == "through");
}

// Its brass back stops it 59.5 mm below the top of the work, and says so.
TEST(a_saws_back_stops_it) {
	const Wood_ tall(mat::Oak, 25.0f, 80.0f, kTop, kTop - 90.0f);
	auto saw = saw_stroke(Saw{}, tall.work(), {0, 0, kTop}, kUp, kAcross, 1.0f, 100.0f, 91.0f);
	strokes(*saw, 20);
	std::printf("    %.1f mm deep: %s\n", double(saw->state().depth), saw->state().limit.c_str());
	CHECK(std::fabs(saw->state().depth - Saw{}.under_back()) < 1e-3f);
	CHECK(saw->state().limit == "back" && !saw->separation());
}

// The kerf runs where the teeth have been: sawn to and fro over 20 mm in a long piece, it
// reaches the blade's half length beyond that each way, no further.
TEST(a_kerf_runs_only_where_the_teeth_have_been) {
	const Wood_ long_piece(mat::Ash, 400.0f);
	auto saw = saw_stroke(Saw{}, long_piece.work(), {0, 0, kTop}, kUp, kAcross, 1.0f, 50.0f);
	for (int k = 0; k < 40; ++k) {
		for (float y = 9.0f; y >= -10.0f; y -= 1.0f) {
			saw->move_to({0, y, kTop});
		}
		for (float y = -9.0f; y <= 10.0f; y += 1.0f) {
			saw->move_to({0, y, kTop});
		}
	}
	Body cut = long_piece.body;
	for (const Edit &e : saw->edits()) {
		CHECK(cut.add(e));
	}
	Octree o;
	o.build(cut);
	auto height = [&](float y) {
		const auto hit = raycast(cut, o, {0, y, 200.0f}, {0, 0, -1}, 400.0f, 1e-4f);
		return hit ? hit->point.z : std::nanf("");
	};
	const float depth = saw->state().depth;
	std::printf("    %.1f mm deep; at 100 mm along %.2f, at 150 mm %.2f\n", double(depth), double(kTop - height(100.0f)),
			double(kTop - height(150.0f)));
	CHECK(depth > 2.0f);
	CHECK(std::fabs(height(100.0f) - (kTop - depth)) < 1e-2f);
	CHECK(std::fabs(height(-130.0f) - (kTop - depth)) < 1e-2f);
	CHECK(std::fabs(height(150.0f) - kTop) < 1e-3f && std::fabs(height(-150.0f) - kTop) < 1e-3f);
}

// However small its steps, the saw's last slice goes down to "through".
TEST(small_steps_saw_right_through) {
	auto saw = saw_stroke(Saw{}, {30, 0, kTop}, kUp, kAcross, 0.05f, 26.0f);
	for (int k = 0; k < 4000 && !saw->separation(); ++k) {
		saw->move_to({30, 0.3f * float(k % 2), kTop});
	}
	CHECK(saw->separation().has_value());
	CHECK(std::fabs(saw->pose().origin.z - (kTop - 26.0f)) < 1e-4f);
}
