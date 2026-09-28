#include "test.h"

#include "body/materials.h"
#include "plans/check.h"
#include "plans/part.h"

#include <cmath>
#include <cstdio>

using namespace sdf;
using namespace sdf::plans;

namespace {

// The mallet's parts (game/plans/mallet.json).
Part head() {
	Part p;
	p.id = "head";
	p.size = {110, 70, 55};
	Feature mortise;
	mortise.kind = Feature::Kind::Hole;
	mortise.name = "mortise";
	mortise.face = Face::Side;
	mortise.along = {40, 70};
	mortise.across = {29, 41};
	mortise.through = true;
	p.features.push_back(mortise);
	return p;
}

Part handle() {
	Part p;
	p.id = "handle";
	p.size = {300, 35, 28};
	Feature tenon;
	tenon.kind = Feature::Kind::Tenon;
	tenon.name = "tenon";
	tenon.length = 58;
	tenon.y = {2.5f, 32.5f};
	tenon.z = {8, 20};
	p.features.push_back(tenon);
	Feature kerf;
	kerf.kind = Feature::Kind::Kerf;
	kerf.name = "wedge kerf";
	kerf.depth = 38;
	kerf.axis = 1;
	kerf.at = 17.5f;
	kerf.width = 0.8f;
	p.features.push_back(kerf);
	return p;
}

Part wedge() {
	Part p;
	p.id = "wedge";
	p.size = {45, 12, 5};
	Feature taper;
	taper.kind = Feature::Kind::Taper;
	taper.name = "taper";
	taper.face = Face::Back;
	taper.from = 5;
	taper.to = 1;
	p.features.push_back(taper);
	return p;
}

// The volume of a body's material within its part's box (a millimetre round it), counted
// on a 0.5 mm grid (cell centres).
double grid_volume(const Body &b, vec3 size) {
	const float h = 0.5f;
	double v = 0.0;
	for (float z = -1.0f + 0.5f * h; z < size.z + 1.0f; z += h) {
		for (float y = -1.0f + 0.5f * h; y < size.y + 1.0f; y += h) {
			for (float x = -1.0f + 0.5f * h; x < size.x + 1.0f; x += h) {
				v += b.distance({x, y, z}) < 0.0f ? 1.0 : 0.0;
			}
		}
	}
	return v * double(h * h * h);
}

// What the features take out of the blank: the blank's volume less the part's.
double removed(const Part &p) {
	Part blank = p;
	blank.features.clear();
	return grid_volume(part_solid(blank, mat::Oak), p.size) - grid_volume(part_solid(p, mat::Oak), p.size);
}

int count(const PartLines &l, PlanLine::As as, const std::string &feature) {
	int n = 0;
	for (const PlanLine &line : l.lines) {
		n += line.as == as && line.feature == feature;
	}
	return n;
}

// Each line on its face's plane (of the stock's box `stock`, in part space) and within it,
// running in the face, `toward` in the face and square to it.
bool on_faces(const PartLines &l, vec3 stock) {
	bool ok = true;
	for (const PlanLine &line : l.lines) {
		const vec3 b = line.origin + line.dir * line.length;
		for (int a = 0; a < 3; ++a) {
			const float n = a == 0 ? line.face.x : a == 1 ? line.face.y : line.face.z;
			const float o = a == 0 ? line.origin.x : a == 1 ? line.origin.y : line.origin.z;
			const float e = a == 0 ? b.x : a == 1 ? b.y : b.z;
			const float hi = a == 0 ? stock.x : a == 1 ? stock.y : stock.z;
			if (n != 0.0f) {
				const float plane = n > 0.0f ? hi : 0.0f;
				ok = ok && std::fabs(o - plane) < 1e-4f && std::fabs(e - plane) < 1e-4f;
			}
			ok = ok && o > -1e-4f && o < hi + 1e-4f && e > -1e-4f && e < hi + 1e-4f;
		}
		ok = ok && std::fabs(gl::dot(line.dir, line.face)) < 1e-5f && std::fabs(gl::dot(line.toward, line.face)) < 1e-5f &&
				std::fabs(gl::dot(line.toward, line.dir)) < 1e-5f;
	}
	return ok;
}

// Points along the lines (1.5 mm in from their ends) lie on the solid's surface.
float off_surface(const Body &b, const PartLines &l, const std::string &feature) {
	float worst = 0.0f;
	for (const PlanLine &line : l.lines) {
		if (line.feature != feature) {
			continue;
		}
		for (float s = 1.5f; s <= line.length - 1.5f; s += 1.0f) {
			worst = std::max(worst, std::fabs(b.distance(line.origin + line.dir * s)));
		}
	}
	return worst;
}

} // namespace

// The head's mortise: knifed round on the face side and, as it goes through, on the back;
// cut out of the solid, it takes 30 x 12 x 55 mm. On longer stock the head is knifed to
// length round it; on thicker stock the back's lines wait (that face is not there yet) and
// the thickness is gauged instead.
TEST(a_mortise_is_laid_out_and_cut_through) {
	const Part p = head();
	const PartLines exact = part_lines(p, p.size);
	CHECK(exact.lines.size() == 8 && exact.later == 0);
	CHECK(count(exact, PlanLine::As::Knife, "mortise") == 8);
	CHECK(on_faces(exact, p.size));
	const Body solid = part_solid(p, mat::Oak);
	const float off = off_surface(solid, exact, "mortise");
	const double taken = removed(p);
	std::printf("    mortise lines %.3f mm off the surface at worst; %.0f mm^3 taken (19800)\n", double(off), taken);
	CHECK(off < 0.05f);
	CHECK(std::fabs(taken - 19800.0) < 0.005 * 19800.0);

	const PartLines longer = part_lines(p, {120, 70, 55});
	CHECK(longer.lines.size() == 12 && longer.later == 0);
	CHECK(count(longer, PlanLine::As::Knife, "") == 4);
	CHECK(on_faces(longer, {120, 70, 55}));

	const PartLines thicker = part_lines(p, {110, 70, 60});
	CHECK(thicker.later == 4); // the back's
	CHECK(count(thicker, PlanLine::As::Gauge, "") == 4 && count(thicker, PlanLine::As::Knife, "mortise") == 4);
	CHECK(on_faces(thicker, {110, 70, 60}));
	for (const PlanLine &line : thicker.lines) {
		if (line.feature.empty()) { // gauged 5 mm from the stock's back, the waste beyond
			CHECK(std::fabs(line.origin.z - 55.0f) < 1e-4f && line.toward.z > 0.99f && std::fabs(line.distance - 5.0f) < 1e-4f);
		}
	}
}

// The handle's tenon: four rebates round its end, each a shoulder gauged across its face
// from the end and a depth gauged on the faces beside it and on the end; the wedge's kerf
// knifed across the end. The solid lacks what the four cheeks and the kerf take.
TEST(a_tenon_is_four_rebates_round_its_end) {
	const Part p = handle();
	const PartLines l = part_lines(p, p.size);
	CHECK(count(l, PlanLine::As::Gauge, "tenon") == 16 && count(l, PlanLine::As::Knife, "wedge kerf") == 1);
	CHECK(l.lines.size() == 17 && l.later == 0);
	CHECK(on_faces(l, p.size));
	int shoulders = 0;
	for (const PlanLine &line : l.lines) {
		if (line.feature == "tenon" && std::fabs(line.origin.x - 58.0f) < 1e-4f && std::fabs(line.dir.x) < 1e-4f) {
			++shoulders; // across its face at 58 mm, gauged from the end: the waste towards it
			CHECK(line.toward.x < -0.99f && std::fabs(line.distance - 58.0f) < 1e-4f);
		}
	}
	CHECK(shoulders == 4);
	const Body solid = part_solid(p, mat::Ash);
	// The shoulders lie on its surface (where each cheek meets the rest of the handle).
	float worst = 0.0f;
	for (const PlanLine &line : l.lines) {
		if (line.feature == "tenon" && std::fabs(line.origin.x - 58.0f) < 1e-4f && std::fabs(line.dir.x) < 1e-4f) {
			for (float s = 1.5f; s <= line.length - 1.5f; s += 1.0f) {
				worst = std::max(worst, std::fabs(solid.distance(line.origin + line.dir * s)));
			}
		}
	}
	const double taken = removed(p), expected = (35.0 * 28.0 - 30.0 * 12.0) * 58.0 + 0.8 * 12.0 * 38.0;
	std::printf("    shoulders %.3f mm off the surface at worst; %.0f mm^3 taken (%.0f)\n", double(worst), taken, expected);
	CHECK(worst < 0.05f);
	CHECK(std::fabs(taken - expected) < 0.01 * expected);
	// A 320 mm handle blank: knifed to length round it too.
	const PartLines longer = part_lines(p, {320, 35, 28});
	CHECK(longer.lines.size() == 21 && count(longer, PlanLine::As::Knife, "") == 4);
}

// The wedge: its slope pencilled on both edges, its thin end gauged; the solid is its taper.
TEST(a_wedge_tapers) {
	const Part p = wedge();
	const PartLines l = part_lines(p, p.size);
	CHECK(count(l, PlanLine::As::Guide, "taper") == 2 && count(l, PlanLine::As::Gauge, "taper") == 1);
	CHECK(l.lines.size() == 3 && on_faces(l, p.size));
	// (Less the eased arrises it takes off, which the blank had lost already: round the back
	// and across its thin end, (1 - pi/4) mm^2 a millimetre.)
	const double taken = removed(p), expected = 45.0 * 12.0 * (5.0 - 1.0) * 0.5 - (1.0 - 0.785398) * (2 * 45.0 + 12.0);
	std::printf("    %.0f mm^3 taken (%.0f)\n", taken, expected);
	CHECK(std::fabs(taken - expected) < 0.01 * expected);
	const Body solid = part_solid(p, mat::Oak);
	CHECK(solid.distance({2.0f, 6.0f, 4.5f}) < 0.0f);  // thick at its head
	CHECK(solid.distance({43.0f, 6.0f, 2.0f}) > 0.0f); // thin at its tip
}

// A rebate along the whole face edge (gauged on both faces) and a stopped chamfer on the
// other (gauged on both, knifed across where it stops): each takes what it should.
TEST(a_rebate_and_a_stopped_chamfer) {
	Part p;
	p.size = {100, 40, 20};
	Feature rebate;
	rebate.kind = Feature::Kind::Rebate;
	rebate.name = "rebate";
	rebate.face = Face::Side;
	rebate.other = Face::Edge;
	rebate.width = 10;
	rebate.depth = 6;
	p.features.push_back(rebate);
	Feature chamfer;
	chamfer.kind = Feature::Kind::Chamfer;
	chamfer.name = "chamfer";
	chamfer.face = Face::Side;
	chamfer.other = Face::OtherEdge;
	chamfer.width = 4;
	chamfer.along = {20, 80};
	p.features.push_back(chamfer);
	const PartLines l = part_lines(p, p.size);
	CHECK(count(l, PlanLine::As::Gauge, "rebate") == 2 && count(l, PlanLine::As::Knife, "rebate") == 0);
	CHECK(count(l, PlanLine::As::Gauge, "chamfer") == 2 && count(l, PlanLine::As::Knife, "chamfer") == 4);
	CHECK(on_faces(l, p.size));
	for (const PlanLine &line : l.lines) {
		if (line.feature == "rebate" && line.face.z < -0.99f) { // on the face side, 10 mm in from the face edge
			CHECK(std::fabs(line.origin.y - 10.0f) < 1e-4f && line.toward.y < -0.99f);
		}
	}
	Part only_rebate = p, only_chamfer = p;
	only_rebate.features.pop_back();
	only_chamfer.features.erase(only_chamfer.features.begin());
	const double r = removed(only_rebate), c = removed(only_chamfer);
	std::printf("    rebate %.0f mm^3 (6000), chamfer %.0f mm^3 (480, less its eased arris)\n", r, c);
	CHECK(std::fabs(r - 6000.0) < 0.01 * 6000.0);
	CHECK(std::fabs(c - 480.0) < 0.05 * 480.0);
}

namespace {

// A blank of `size` mm at the part's corner, eased as stock is or square, less boxes.
Body wood(vec3 size, bool eased, const std::vector<std::pair<vec3, vec3>> &cuts = {}) {
	Body b;
	b.base = Primitive::box(size * 0.5f, size * 0.5f, eased ? 1.0f : 0.0f);
	b.base_material = mat::Oak;
	for (const auto &[lo, hi] : cuts) {
		Edit e;
		e.prim = Primitive::box((lo + hi) * 0.5f, (hi - lo) * 0.5f);
		e.op = Op::Subtract;
		b.add(e);
	}
	return b;
}

Check check(const Part &p, const Body &b) {
	const Check c = check_part(p, [&](vec3 q) { return b.distance(q); }, b.bounds());
	std::printf("    %zu spot(s), %d samples, %.0f ms:", c.spots.size(), c.samples, c.ms);
	for (const Spot &s : c.spots) {
		std::printf(" [%s %.2f mm, %.0f mm^2, %.0f mm^3, feature %d, normal (%.2f %.2f %.2f) at (%.1f %.1f %.1f)]",
				s.kind == Spot::Kind::Proud ? "proud" : "short", s.most, s.area, s.volume, s.feature, s.normal.x, s.normal.y,
				s.normal.z, s.at.x, s.at.y, s.at.z);
	}
	std::printf("\n");
	return c;
}

} // namespace

// The part as drawn is as drawn; a square blank's arrises are within the allowance for the
// drawing's eased ones.
TEST(a_part_as_drawn_checks_clean) {
	CHECK(check(head(), part_solid(head(), mat::Oak)).spots.empty());
	CHECK(check(handle(), part_solid(handle(), mat::Oak)).spots.empty());
	CHECK(check(wedge(), part_solid(wedge(), mat::Oak)).spots.empty());
	const Body square = wood({110, 70, 55}, false, {{{40, 29, -1}, {70, 41, 56}}});
	CHECK(check(head(), square).spots.empty());
}

// The head on its 120 mm blank, mortised but not yet cut to length: the far end is 10 mm
// proud, the blank's end beyond it, and nothing else. (Volumes are of the wood beyond the
// tolerance.)
TEST(a_blank_not_cut_to_length_is_proud_at_its_far_end) {
	const Body b = wood({120, 70, 55}, true, {{{40, 29, -1}, {70, 41, 56}}});
	const Check c = check(head(), b);
	CHECK(c.spots.size() == 1);
	if (!c.spots.empty()) {
		const Spot &s = c.spots[0];
		CHECK(s.kind == Spot::Kind::Proud && std::fabs(s.most - 10.0f) < 0.15f);
		CHECK(s.feature == -1 && s.normal.x > 0.95f && std::fabs(s.at.x - 110.0f) < 0.5f);
		// (The wood beyond the tolerance: 9.5 mm of it, less the eased arrises.)
		CHECK(std::fabs(s.volume - 9.5f * 70.0f * 55.0f) < 0.03f * 9.5f * 70.0f * 55.0f);
		CHECK(!s.dots.empty() && s.dots.size() == s.by.size() && int(s.dots.size()) <= Spot::kDots);
	}
}

// A mortise chopped a millimetre short of its line (proud), or past it (short): the spot on
// the mortise's side 29 mm from the face edge, a millimetre off.
TEST(a_mortise_a_millimetre_off_its_line) {
	const Check narrow = check(head(), wood({110, 70, 55}, true, {{{40, 30, -1}, {70, 41, 56}}}));
	CHECK(narrow.spots.size() == 1);
	if (!narrow.spots.empty()) {
		const Spot &s = narrow.spots[0];
		CHECK(s.kind == Spot::Kind::Proud && std::fabs(s.most - 1.0f) < 0.1f && s.feature == 0);
		CHECK(s.normal.y > 0.95f && std::fabs(s.at.y - 29.0f) < 0.2f);
		CHECK(std::fabs(s.volume - 0.5f * 30.0f * 55.0f) < 0.2f * 0.5f * 30.0f * 55.0f);
		CHECK(std::fabs(s.area - 30.0f * 55.0f) < 0.1f * 30.0f * 55.0f); // the mortise's side
	}
	const Check wide = check(head(), wood({110, 70, 55}, true, {{{40, 28, -1}, {70, 41, 56}}}));
	CHECK(wide.spots.size() == 1);
	if (!wide.spots.empty()) {
		const Spot &s = wide.spots[0];
		CHECK(s.kind == Spot::Kind::Short && std::fabs(s.most - 1.0f) < 0.1f && s.feature == 0);
		CHECK(s.normal.y > 0.95f && std::fabs(s.at.y - 29.0f) < 0.2f);
	}
}

// A tenon's cheek pared 0.8 mm past its line: short, on the tenon.
TEST(a_tenon_pared_too_thin_is_short) {
	Part thin = handle();
	thin.features[0].z = {8.8f, 20.0f};
	const Check c = check(handle(), part_solid(thin, mat::Ash));
	CHECK(c.spots.size() == 1);
	if (!c.spots.empty()) {
		const Spot &s = c.spots[0];
		CHECK(s.kind == Spot::Kind::Short && std::fabs(s.most - 0.8f) < 0.1f && s.feature == 0);
		CHECK(s.normal.z < -0.95f && std::fabs(s.at.z - 8.0f) < 0.2f);
	}
}

// The head's blank with nothing cut: its far end, and the mortise (wood all through it, up
// to 6 mm from its sides: half its width), proud.
TEST(an_uncut_mortise_is_proud_by_half_its_width) {
	const Check c = check(head(), wood({120, 70, 55}, true));
	CHECK(c.spots.size() == 2);
	if (c.spots.size() == 2) {
		CHECK(c.spots[0].feature == -1 && std::fabs(c.spots[0].most - 10.0f) < 0.15f);
		CHECK(c.spots[1].feature == 0 && std::fabs(c.spots[1].most - 6.0f) < 0.15f);
		CHECK(std::fabs(c.spots[1].volume - 29.0f * 11.0f * 55.0f) < 0.05f * 29.0f * 11.0f * 55.0f);
	}
}
