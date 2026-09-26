#include "test.h"

#include "body/materials.h"
#include "demo/gallery.h"
#include "edit/session.h"
#include "eval/query.h"
#include "tools/tools.h"

#include <algorithm>

using namespace sdf;

namespace {

bool same_edit(const Edit &a, const Edit &b) {
	bool same = a.prim.type == b.prim.type && a.op == b.op && a.blend == b.blend && a.r == b.r && a.r2 == b.r2 &&
			a.material == b.material;
	for (int i = 0; i < 5; ++i) {
		same = same && a.prim.p[i].x == b.prim.p[i].x && a.prim.p[i].y == b.prim.p[i].y && a.prim.p[i].z == b.prim.p[i].z &&
				a.prim.p[i].w == b.prim.p[i].w;
	}
	return same;
}

bool same_edits(const Body &a, const std::vector<Edit> &b) {
	if (a.edits().size() != b.size()) {
		return false;
	}
	for (std::size_t i = 0; i < b.size(); ++i) {
		if (!same_edit(a.edits()[i], b[i])) {
			return false;
		}
	}
	return true;
}

// A random use of one of the tools on the board's top face.
std::vector<Edit> random_stroke(t::Rng &rng) {
	const float top = 12.5f;
	const vec3 up(0, 0, 1), start(rng.uniform(-60, 60), rng.uniform(-35, 35), top);
	switch (int(rng.uniform(0.0f, 3.0f))) {
		case 0: {
			const tools::Chisel chisel{rng.uniform(0.0f, 1.0f) < 0.5f ? 6.0f : 12.0f};
			const vec3 end = start + vec3(rng.uniform(-25, 25), rng.uniform(-25, 25), 0);
			return chisel.paring(start, end, up, rng.uniform(0.3f, 2.0f));
		}
		case 1:
			return {tools::Saw{}.kerf_cut(start, {rng.uniform(-1, 1), rng.uniform(-1, 1), 0}, up, rng.uniform(0.5f, 8.0f))};
		default: {
			const tools::Frame plane = tools::Frame::at(start, up, {1, 0, 0});
			const vec2 lo(rng.uniform(-30, -10), rng.uniform(-20, -5));
			return {tools::SandingBlock{}.pass(plane, lo, lo + vec2(rng.uniform(20, 50), rng.uniform(10, 30)),
					rng.uniform(0.05f, 0.6f))};
		}
	}
}

// The session's octree and ADF against ones built from scratch for its body, at points near
// the surface (found by rays down onto the board). ADFs are compared where both hold bricks:
// empty and solid leaves only keep their centre value, and where the session's update left
// a coarse exact leaf, the other may have decided a finer cell holds no surface.
void check_against_fresh(const EditSession &s, t::Rng &rng, float &worst_octree, float &worst_adf) {
	Octree fresh;
	fresh.build(s.body());
	Adf fresh_adf;
	fresh_adf.build(s.body(), fresh, s.adf().params());
	for (int i = 0; i < 300; ++i) {
		const auto hit = raycast(s.body(), fresh, {rng.uniform(-75, 75), rng.uniform(-45, 45), 60}, {0, 0, -1}, 200.0f, 1e-4f);
		if (!hit) {
			continue;
		}
		const vec3 p = hit->point + hit->normal * rng.uniform(-0.2f, 0.2f);
		worst_octree = std::max(worst_octree, std::fabs(s.octree().distance(s.body(), p) - fresh.distance(s.body(), p)));
		const int a = s.adf().leaf(p), b = fresh_adf.leaf(p);
		if (a < 0 || b < 0 || s.adf().nodes()[std::size_t(a)].brick < 0 || fresh_adf.nodes()[std::size_t(b)].brick < 0) {
			continue;
		}
		worst_adf = std::max(worst_adf, std::fabs(s.adf().distance(s.body(), s.octree(), p) - fresh_adf.distance(s.body(), fresh, p)));
	}
}

} // namespace

// Strokes replaced as tools move, committed, cancelled, undone and redone: the incremental
// octree and ADF must match fresh builds of the same edit list throughout.
TEST(edit_session_matches_fresh_builds) {
	EditSession s;
	s.reset(demo::board(mat::Oak));
	t::Rng rng(17);
	float worst_octree = 0.0f, worst_adf = 0.0f;
	for (int op = 0; op < 24; ++op) {
		const float r = rng.uniform(0.0f, 1.0f);
		if (r < 0.55f) {
			// A tool at work: a few updates of the same stroke, then commit or cancel.
			std::vector<Edit> stroke;
			for (int k = 0; k < 3; ++k) {
				stroke = random_stroke(rng);
				CHECK(s.set_stroke(stroke));
			}
			if (rng.uniform(0.0f, 1.0f) < 0.8f) {
				s.commit();
			} else {
				s.cancel();
			}
		} else if (r < 0.8f) {
			s.undo();
		} else {
			s.redo();
		}
		check_against_fresh(s, rng, worst_octree, worst_adf);
	}
	std::printf("    %zu edits in %zu steps; worst difference from fresh builds: octree %.2g mm, ADF %.2g mm\n",
			s.body().edits().size(), s.steps(), double(worst_octree), double(worst_adf));
	CHECK(worst_octree <= 1e-4f);
	CHECK(worst_adf <= 2.0f * s.adf().params().tolerance);
}

TEST(edit_session_undo_and_redo_restore_the_edit_list) {
	EditSession s;
	s.reset(demo::board(mat::Ash));
	t::Rng rng(3);
	std::vector<std::vector<Edit>> strokes;
	std::vector<Edit> all;
	for (int i = 0; i < 3; ++i) {
		strokes.push_back(random_stroke(rng));
		CHECK(s.set_stroke(strokes.back()));
		s.commit();
		all.insert(all.end(), strokes.back().begin(), strokes.back().end());
	}
	CHECK(same_edits(s.body(), all));
	CHECK(s.undo() && s.undo());
	CHECK(same_edits(s.body(), strokes[0]));
	CHECK(s.redo());
	std::vector<Edit> two = strokes[0];
	two.insert(two.end(), strokes[1].begin(), strokes[1].end());
	CHECK(same_edits(s.body(), two));
	// A new stroke in progress is dropped by undo; a committed one clears the redo list.
	CHECK(s.set_stroke(random_stroke(rng)));
	CHECK(s.undo());
	CHECK(same_edits(s.body(), strokes[0]));
	CHECK(s.set_stroke(strokes[2]));
	s.commit();
	CHECK(!s.can_redo());
	CHECK(s.undo() && s.undo() && !s.undo());
	CHECK(s.body().edits().empty());
}

TEST(edit_session_rejects_strokes_a_tool_cannot_make) {
	EditSession s;
	s.reset(demo::board(mat::Walnut));
	t::Rng rng(5);
	const std::vector<Edit> ok = random_stroke(rng);
	CHECK(s.set_stroke(ok));
	// A 25 mm chisel swept round a 3 mm bend.
	Edit tight;
	tight.prim = Primitive::sweep({0, 0, 11}, {3, 3, 11}, {0, 6, 11}, {0, 0, 1}, ToolProfile::flat(25, 3));
	CHECK(!s.set_stroke({tight}));
	CHECK(same_edits(s.body(), ok)); // unchanged
}
