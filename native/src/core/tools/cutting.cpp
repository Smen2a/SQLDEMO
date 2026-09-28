#include "tools/cutting.h"

#include "tools/debris.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf::tools {

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
// kCuttingResistance (cutting.h): a 12 mm bench chisel pushed by hand (200 N) takes about
// 0.5 mm along the grain in ash, 0.25 mm across it.
constexpr float kCutting = kCuttingResistance;
constexpr float kSideStrip = 1.5f;   // mm of chip each attached side adds
constexpr float kClearance = 2.0f * kDeg;
constexpr float kDigIn = 8.0f * kDeg; // diving more steeply than this, the edge digs in
// The steepest the edge can be steered up, following a rising surface (the handle lowered
// onto the bevel): mm per mm. A surface rising faster stalls it.
constexpr float kRise = 0.14f;
// A chop's depth per blow of a bench chisel's mallet, 12 mm wide across the grain in oak
// (severing the fibres: grain factor 4.5), from the surface: 2.5 mm.
constexpr float kBlow = 11.25f;
constexpr float kPush = 0.25f; // the same by a hand's push (tools never struck)
constexpr float kSlit = 0.6f;  // a chop's slit, mm thick
constexpr std::size_t kMaxChips = 6;

Edit cut(const Primitive &prim) {
	Edit e;
	e.prim = prim;
	e.op = Op::Subtract;
	return e;
}

// Deterministic noise for chips: the same plan, the same chips.
struct Rng {
	std::uint32_t state;
	float next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return float(state & 0xffffff) / float(0x1000000);
	}
};

// Douglas-Peucker on the floor's (s, depth) points.
void simplify(const std::vector<vec2> &in, std::size_t a, std::size_t b, float tolerance, std::vector<vec2> &out) {
	float worst = 0.0f;
	std::size_t at = a;
	const vec2 d = in[b] - in[a];
	for (std::size_t i = a + 1; i < b; ++i) {
		const float t = d.x > 1e-6f ? (in[i].x - in[a].x) / d.x : 0.0f;
		const float off = std::fabs(in[i].y - (in[a].y + d.y * t));
		if (off > worst) {
			worst = off;
			at = i;
		}
	}
	if (worst > tolerance) {
		simplify(in, a, at, tolerance, out);
		simplify(in, at, b, tolerance, out);
	} else {
		out.push_back(in[b]);
	}
}

} // namespace

Wood Wood::of(const Material &m) {
	Wood w;
	if (m.hardness > 0.0f) {
		w.hardness = m.hardness;
		w.split = m.split;
		w.tearout = m.tearout;
	}
	return w;
}

Wood Work::wood(vec3 p) const {
	const Sample s = octree.sample(body, p);
	const float id = s.t < 0.5f ? s.m0 : s.m1;
	const std::size_t m = std::size_t(std::max(id, 0.0f) + 0.5f);
	return m < materials.size() ? Wood::of(materials[std::uint16_t(m)]) : Wood{};
}

vec3 Work::fibre() const {
	const vec3 a = body.grain_axis;
	return gl::dot(a, a) > 1e-12f ? gl::normalize(a) : vec3(1, 0, 0);
}

float grain_factor(vec3 fibre, vec3 travel, vec3 edge) {
	const float along = gl::dot(travel, fibre), lengthwise = gl::dot(edge, fibre);
	const float across = 1.0f - along * along;
	return along * along + across * (1.8f * lengthwise * lengthwise + 4.5f * (1.0f - lengthwise * lengthwise));
}

float resistance(const Wood &wood, vec3 fibre, vec3 travel, vec3 edge) {
	return kCutting * wood.hardness * grain_factor(fibre, travel, edge);
}

std::vector<std::string> warning_names(unsigned warnings) {
	static const char *names[] = {"skates", "shallow", "tears out", "corners buried", "breaks out", "not struck",
			"slit only", "pops off", "digs in", "splits", "blocked", "blade meets the work", "too wide for the gap",
			"stalls", "mouth", "at the line", "lifts out"};
	std::vector<std::string> out;
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (warnings & (1u << i)) {
			out.push_back(names[i]);
		}
	}
	return out;
}

float CutPlan::depth_at(float s) const {
	if (floor.empty()) {
		return 0.0f;
	}
	if (s <= floor.front().x) {
		return floor.front().y;
	}
	for (std::size_t i = 1; i < floor.size(); ++i) {
		if (s <= floor[i].x) {
			const float t = (s - floor[i - 1].x) / std::max(floor[i].x - floor[i - 1].x, 1e-6f);
			return floor[i - 1].y + (floor[i].y - floor[i - 1].y) * t;
		}
	}
	return floor.back().y;
}

bool CutPlan::lift_out(float upto, Edit &out) const {
	if (chop || floor.size() < 2 || (square_end && upto >= length - 1e-3f)) {
		return false;
	}
	const float s = std::min(upto, length), floor = depth_at(s);
	const float d = lift_depth >= 0.0f ? lift_depth : floor;
	if (d <= 0.0f) {
		return false;
	}
	Chisel c = chisel;
	if (c.kind == gl::SDF_TOOL_FLAT) {
		c.width = width;
	}
	out = c.lift_out(point(s, floor), path, normal, d);
	return true;
}

float CutPlan::shaving_piece() const {
	return (1.5f + 3.0f * (1.0f - split)) / std::max(grain, 0.03f);
}

std::vector<Edit> CutPlan::edits(float upto, bool finished) const {
	std::vector<Edit> out = floor_edits(upto);
	if (chop) {
		out.insert(out.end(), chips.begin(), chips.end());
		return out;
	}
	if (out.empty()) {
		return out;
	}
	const float end = std::min(upto, length);
	for (std::size_t i = 0; i < chips.size(); ++i) {
		if (chips_at[i] <= end) {
			out.push_back(chips[i]);
		}
	}
	Edit lift;
	if (finished && lift_out(end, lift)) {
		out.push_back(lift);
	}
	return out;
}

std::vector<vec2> CutPlan::floor_points(float upto) const {
	std::vector<vec2> kept;
	if (chop || floor.size() < 2) {
		return kept;
	}
	const float end = std::min(upto, length);
	if (end <= floor.front().x) {
		return kept;
	}
	std::vector<vec2> points;
	for (const vec2 &f : floor) {
		if (f.x >= end) {
			break;
		}
		points.push_back(f);
	}
	points.push_back({end, depth_at(end)});
	kept.push_back(points.front());
	if (points.size() > 1) {
		simplify(points, 0, points.size() - 1, 0.015f, kept);
	}
	return kept;
}

ToolProfile CutPlan::profile(float deepest) const {
	Chisel c = chisel;
	if (c.kind == gl::SDF_TOOL_FLAT) {
		c.width = width;
	}
	return c.profile(height > 0.0f ? height : deepest + 2.0f);
}

std::vector<Edit> CutPlan::floor_edits(float upto) const {
	std::vector<Edit> out;
	if (chop) {
		out.push_back(slit);
		return out;
	}
	const std::vector<vec2> kept = floor_points(upto);
	if (kept.size() < 2) {
		return out;
	}
	float deepest = 0.0f;
	for (const vec2 &k : kept) {
		deepest = std::max(deepest, k.y);
	}
	const ToolProfile section = profile(deepest);
	for (std::size_t i = 0; i + 1 < kept.size(); ++i) {
		// A cut that starts at depth (from an open face) starts just outside it; one that
		// ramps in, just above the surface. Pieces overlap half a millimetre: flush ends
		// leave a seam the raymarcher would see as a wall.
		vec2 a = kept[i], b = kept[i + 1];
		if (i == 0) {
			a = open || a.y > 0.0f ? vec2(a.x - lead_in, a.y) : vec2(a.x, -0.2f);
		}
		vec3 from = point(a.x, a.y), to = point(b.x, b.y);
		if (i + 2 < kept.size()) {
			to = to + gl::normalize(to - from) * 0.5f;
		}
		out.push_back(cut(Primitive::sweep(from, to, normal, section)));
	}
	return out;
}

namespace {

// Whether the face just behind `start` is open (air) where a cut `depth` deep would begin:
// an edge of the work, or an existing cut.
bool open_behind(const Work &work, vec3 start, vec3 t, vec3 n, vec3 b, float width, float depth) {
	int air = 0, probes = 0;
	for (const float z : {0.5f, 0.75f, 1.0f}) {
		for (const float x : {-0.33f, 0.0f, 0.33f}) {
			++probes;
			air += work.field(start - t * 0.6f - n * (depth * z) + b * (width * x)) > 0.02f;
		}
	}
	return air * 3 >= probes * 2;
}

// How many sides of a chip `depth` deep at `at` (a point on the path, at the surface) are
// still attached to the work: wood just beyond each side of the edge.
int attached_sides(const Work &work, vec3 at, vec3 n, vec3 b, float width, float depth) {
	const vec3 mid = at - n * (0.5f * depth);
	return int(work.field(mid + b * (0.5f * width + 0.4f)) < 0.0f) + int(work.field(mid - b * (0.5f * width + 0.4f)) < 0.0f);
}

CutPlan plan_chop(CutPlan p, const Work &work, const Wood &wood, vec3 b, vec3 edge, float blow) {
	const Chisel &c = p.chisel;
	const vec3 n = p.normal, t = p.path;
	const vec3 f = work.fibre();
	p.chop = true;
	const float g = grain_factor(f, -n, edge);
	p.grain = (g - 1.0f) / 3.5f;
	if (std::fabs(gl::dot(edge, f)) > 0.7f) {
		p.warnings |= kSplits;
	}
	// The slit so far: air straight down its middle.
	float d0 = 0.0f;
	for (float z = 0.05f; z < 80.0f; z += 0.05f) {
		if (work.field(p.start + t * (0.5f * kSlit) - n * z) <= 0.02f) {
			break;
		}
		d0 = z;
	}
	const float hard = wood.hardness / 5740.0f, wide = p.width / 12.0f;
	const float driven = c.mallet > 0.0f ? kBlow * c.mallet * blow : kPush * c.hand_force / 200.0f;
	if (c.mallet <= 0.0f) {
		p.warnings |= kNotStruck;
	}
	p.blow = driven / (hard * g * wide) / (1.0f + d0 / 6.0f);
	p.depth = d0 + p.blow;
	p.slit = cut(Primitive::sweep(p.start + n * 0.5f, p.start - n * p.depth, t, ToolProfile::flat(p.width, kSlit)));
	p.floor = {{0.0f, p.depth}};
	// Within reach of an open face on the bevel's side (along the path), the chip between
	// pops off, split along the grain.
	const float reach = p.depth * (0.8f + 1.5f * wood.split);
	for (float s = kSlit + 0.2f; s <= reach; s += 0.2f) {
		if (work.field(p.start + t * s - n * (0.5f * p.depth)) > 0.05f) {
			const ToolProfile block = ToolProfile::flat(p.width, p.depth + 1.0f);
			p.chips.push_back(cut(Primitive::sweep(p.point(kSlit * 0.5f, p.depth), p.point(s + 0.3f, p.depth), n, block)));
			p.chips_at.push_back(0.0f);
			p.warnings |= kPopsOff;
			break;
		}
	}
	if (!(p.warnings & kPopsOff)) {
		p.warnings |= kSlitOnly;
	}
	(void)b;
	return p;
}

} // namespace

namespace {

// A splinter torn out along the grain: a rounded scoop `radius` wide either side of its
// line, `side` off the cut's middle, whose floor runs from `bottom0` deep at `s0` along the
// path down to `bottom1` at `s1` and curls back up to the surface beyond (depths below the
// plane the plan starts on).
Edit splinter(const CutPlan &p, float s0, float s1, float bottom0, float bottom1, float radius, vec3 side) {
	return cut(Primitive::capsule(p.point(s0, bottom0 - radius) + side, p.point(s1, bottom1 - radius) + side, radius));
}

// How deep a splinter may go below the cut's floor there: half as deep again as the cut, and
// a millimetre at most.
float splinter_limit(const CutPlan &p, float s) {
	return std::min(1.5f * std::max(p.depth_at(s), 0.05f), 1.0f);
}

constexpr int kColumns = kChipColumns;

// Column j's offset across the edge (mm, towards b = normal x path).
float column_across(const CutPlan &p, int j) {
	return ((float(j) + 0.5f) / float(kColumns) - 0.5f) * p.width;
}

} // namespace

void chip_columns(const CutPlan &p, const Body &body, const Octree &octree, float s, float floor,
		float over[kChipColumns]) {
	const vec3 n = p.normal, b = gl::cross(p.normal, p.path);
	for (int j = 0; j < kColumns; ++j) {
		const float across = column_across(p, j);
		const float lift = p.chisel.edge_height(across);
		const vec3 at = p.point(s, floor - lift) + b * across;
		over[j] = std::max(depth_below_surface(body, octree, at, n, floor + 30.0f), 0.0f);
	}
}

void add_tear_out(CutPlan &p, const Work &work, float scale, std::uint32_t seed) {
	if (p.slope >= 0 || p.depth <= 0.0f || p.chips.size() >= kMaxChips) {
		return;
	}
	const Wood wood = work.wood(p.start - p.normal * 0.5f);
	const vec3 f = work.fibre(), ahead = gl::dot(f, p.path) >= 0.0f ? f : -f;
	const float rise = gl::dot(ahead, p.normal);
	const vec3 b = gl::cross(p.normal, p.path);
	Rng rng{seed * 2654435761u + 0x7f4a7c15u};
	rng.next();
	const float risk = std::min(1.0f, scale * wood.tearout * std::fabs(rise) / std::sin(3.0f * kDeg));
	for (float s = 2.0f + 3.0f * rng.next(); s < p.length - 1.0f && p.chips.size() < kMaxChips;
			s += 3.0f + 5.0f * rng.next()) {
		const float roll = rng.next(), w = p.width * (0.4f + 0.5f * rng.next()), off = rng.next() * 2.0f - 1.0f;
		const float lean = rng.next();
		if (roll >= risk) {
			continue;
		}
		// The split runs down along the fibres ahead of the edge; the chip over it tears away.
		const float reach = (2.0f + 4.0f * rng.next()) * (0.5f + wood.split);
		const float end = std::min(s + reach, p.length);
		const float dip = std::min(p.depth_at(s) * (0.3f + 1.2f * risk * lean), splinter_limit(p, s));
		const vec3 side = b * (off * 0.5f * (p.width - w));
		p.chips.push_back(splinter(p, s, end, p.depth_at(s), p.depth_at(end) + dip, 0.5f * w, side));
		p.chips_at.push_back(end);
		p.warnings |= kTearOut;
	}
}

CutPlan plan_cut(const Chisel &chisel, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, float skew_deg, std::uint32_t seed, float blow, float entry) {
	CutPlan p;
	p.chisel = chisel;
	p.start = start;
	const Frame frame = Frame::at(start, normal, path);
	p.normal = frame.z;
	p.path = frame.x;
	const vec3 n = frame.z, t = frame.x, b = gl::cross(n, t);
	// Only a flat edge skews (and slices).
	const float skew = chisel.kind == gl::SDF_TOOL_FLAT ? (chisel.skew_deg + skew_deg) * kDeg : 0.0f;
	const vec3 edge = gl::normalize(b * std::cos(skew) + t * std::sin(skew));
	p.width = chisel.kind == gl::SDF_TOOL_FLAT ? chisel.width * std::cos(skew) : chisel.width;
	const Wood wood = work.wood(start - n * 0.5f);
	p.split = wood.split;
	if (chisel.approach_deg >= 60.0f) {
		return plan_chop(p, work, wood, b, edge, blow);
	}

	const vec3 f = work.fibre();
	const float g = grain_factor(f, t, edge);
	p.grain = (g - 1.0f) / 3.5f;
	const vec3 ahead = gl::dot(f, t) >= 0.0f ? f : -f; // the fibres, followed the way the edge goes
	const float along = std::fabs(gl::dot(f, t));
	const float rise = gl::dot(ahead, n); // > 0: they run out of the surface ahead (downhill)
	p.slope = along < 0.5f || std::fabs(rise) < 0.005f ? 0 : (rise > 0.0f ? 1 : -1);
	const float k = kCutting * wood.hardness * g * (1.0f - 0.4f * std::fabs(std::sin(skew)));
	const float side_strip = kSideStrip * (1.0f - 0.6f * wood.split * along * along);
	const float corners = chisel.corner_depth();
	p.available = chisel.hand_force;
	auto stop = [&](float s, unsigned why) {
		p.stop_at = std::max(s, 0.0f);
		p.stop = why;
		p.warnings |= why;
	};

	// In: from an open face at its depth at once; mid-face, only past its bevel's clearance,
	// diving at the difference. Going on from a cut, the edge is in it already: the bevel
	// steers it from its depth there, towards `target` at `rate` (mm a mm along).
	const bool going_on = entry >= 0.0f;
	const bool open = going_on || open_behind(work, start, t, n, b, p.width, depth);
	p.open = open;
	const float tilt = chisel.approach_deg * kDeg, bevel = chisel.bevel_deg * kDeg;
	float dive = 1e9f, target = depth;
	bool lifting = false;
	if (going_on) {
		if (tilt > bevel + kClearance) {
			dive = std::tan(tilt - bevel); // tipped past its bevel: it dives, to the depth asked
			target = std::max(depth, entry);
			if (tilt - bevel > kDigIn + 1e-4f) {
				p.warnings |= kDigsIn;
			}
		} else if (tilt >= bevel - kClearance) {
			dive = 0.0f; // riding its bevel on the floor it has cut: level
			target = entry;
		} else {
			dive = std::tan(bevel - tilt); // lowered under it: it rises, and lifts out
			target = 0.0f;
			lifting = true;
		}
	} else if (!open) {
		if (tilt < bevel + kClearance) {
			p.warnings |= kSkates;
			stop(0.0f, kSkates);
			p.length = 0.0f;
			p.floor.push_back({0.0f, 0.0f});
			return p;
		}
		dive = std::tan(tilt - bevel);
		if (tilt - bevel > kDigIn + 1e-4f) {
			p.warnings |= kDigsIn; // (it dives steeply, but never below the depth asked)
		}
	}

	// What stands over the edge `floor` deep at s (its section rises to its corners), in
	// columns across it: the wood over each (0 where it is in the air), and the highest the
	// surface stands above the floor (0: the edge is all in the air).
	struct Column {
		float over[kColumns] = {};
		float thickest = 0.0f, middle = 0.0f, side = 0.0f; // side: the outer columns, the more
		float floor = 0.0f;
	};
	auto column = [&](float s, float floor) {
		Column c;
		c.floor = floor;
		chip_columns(p, work.body, work.octree, s, floor, c.over);
		for (int j = 0; j < kColumns; ++j) {
			const float over = c.over[j];
			if (over > 0.0f) {
				c.thickest = std::max(c.thickest, over + chisel.edge_height(column_across(p, j)));
				if (j == kColumns / 2) {
					c.middle = over;
				} else if (j == 0 || j == kColumns - 1) {
					c.side = std::max(c.side, over);
				}
			}
		}
		return c;
	};
	// The force the chip over the edge takes with its floor raised from the column's to `at`
	// (the surface stays where it is): its section, summed across the columns, and the strips
	// its attached sides tear.
	auto force_over = [&](const Column &c, float at, int sides) {
		const float raised = c.floor - at;
		float area = 0.0f, thick = 0.0f;
		for (const float over : c.over) {
			area += std::max(over - raised, 0.0f);
			thick = std::max(thick, over - raised);
		}
		area *= p.width / float(kColumns);
		return k * (area + float(sides) * side_strip * std::max(thick - corners, 0.0f));
	};
	// The deepest floor in [lo, hi] the hand can push the column's chip at (lo if not even that).
	auto deepest_pushed = [&](const Column &c, float lo, float hi, int sides) {
		if (force_over(c, hi, sides) <= p.available) {
			return hi;
		}
		for (int i = 0; i < 24; ++i) {
			const float mid = 0.5f * (lo + hi);
			(force_over(c, mid, sides) <= p.available ? lo : hi) = mid;
		}
		return lo;
	};
	// Whether wood lies where the blade's body goes behind an edge `floor` deep at s: its
	// middle 10 to 25 mm back at its approach (kBladeMeets), or only its sides (kTooWide).
	const vec3 back = gl::normalize(t * -std::cos(tilt) + n * std::sin(tilt));
	const vec3 over = gl::normalize(t * std::sin(tilt) + n * std::cos(tilt)); // out of its flat back
	auto blade = [&](float s, float floor, float chip) -> unsigned {
		unsigned hit = 0;
		for (const float u : {10.0f, 17.0f, 25.0f}) {
			if (u * std::sin(tilt) - 0.5f * chisel.thickness * std::cos(tilt) < chip + 0.3f) {
				continue; // still down in the stroke's own channel, the chip it has cut away
			}
			for (const float x : {-0.4f, 0.0f, 0.4f}) {
				const float across = x * p.width;
				const vec3 q = p.point(s, floor) + back * u - over * (0.5f * chisel.thickness) + b * across +
						n * chisel.edge_height(across);
				if (work.field(q) < 0.0f) {
					hit |= x == 0.0f ? kBladeMeets : kTooWide;
				}
			}
		}
		return hit & kBladeMeets ? kBladeMeets : hit;
	};

	// Along the path a millimetre at a time: the floor dives until it levels at the depth,
	// never deeper than the hand can push the chip there, and rising only gently.
	const int steps = std::max(1, int(std::ceil(length)));
	const float ds = length / float(steps);
	float d = going_on ? entry
			: open	   ? deepest_pushed(column(0.0f, depth), 0.0f, depth, attached_sides(work, start, n, b, p.width, depth))
					   : 0.0f;
	p.floor.push_back({0.0f, d});
	if (const unsigned why = blade(0.0f, d, d)) {
		stop(0.0f, why); // the blade cannot get there
		p.length = 0.0f;
		return p;
	}
	constexpr float kNone = std::numeric_limits<float>::quiet_NaN();
	float exit = -1.0f, top = 0.0f, last_rise = kNone, before = kNone; // the surface a mm and 2 mm back
	bool limited = false;
	// How high the surface stands above the start's plane at its highest over the next 6 mm
	// from s (along the edge's middle, the floor `floor` deep): the top of a wall ahead.
	auto top_ahead = [&](float s, float floor) {
		float highest = -1e9f;
		for (float u = 0.0f; u <= 6.0f; u += 0.5f) {
			const float over = depth_below_surface(work.body, work.octree, p.point(s + u, floor), n, floor + 30.0f);
			if (over > 0.0f) {
				highest = std::max(highest, over - floor);
			}
		}
		return highest;
	};
	for (int i = 1; i <= steps; ++i) {
		const float s = float(i) * ds;
		const vec3 at = start + t * s;
		float next = d <= target ? std::min(target, d + dive * ds) : std::max(target, d - dive * ds);
		if (lifting && next <= 0.0f) {
			// Lifted out: the edge leaves the wood here, where it rises to the surface.
			const float out = s - ds + d / std::max(dive, 1e-6f);
			p.floor.push_back({out, 0.0f});
			p.warnings |= kLifts;
			stop(out, kLifts);
			break;
		}
		const Column col = column(s, next);
		const float chip = col.thickest;
		if (chip > 0.02f) {
			// The surface's height above the plane the stroke started on, here.
			const float surface = chip - next;
			// A step up ahead: over a millimetre within the last two (a wall steeper than about
			// 27 degrees, as a cut's own end is). The edge stops at its face.
			const float lowest = std::isnan(before) ? last_rise : std::min(last_rise, before);
			if (!std::isnan(lowest) && surface > lowest + 1.0f) {
				stop(s - ds, kBlocked);
				p.wall = std::max(surface, top_ahead(s, next)) - lowest;
				break;
			}
			if (col.side > col.middle + 1.0f) {
				stop(s - ds, kTooWide); // walls either side, closer than the edge is wide
				p.wall = col.side - col.middle;
				break;
			}
			before = last_rise;
			last_rise = surface;
			int sides = attached_sides(work, at, n, b, p.width, std::max(next, 0.1f));
			if (chisel.kind != gl::SDF_TOOL_FLAT && next <= corners) {
				sides = 0; // corners out: the chip's sides are free
			}
			// As deep as the hand can push the chip over the edge. Where the surface rises into a
			// chip thicker than that, the edge follows it up, but only so steeply: faster, it stalls;
			// where it rises steeper than about 27 degrees here, that is a wall, which blocks it
			// (however the millimetres fall on it).
			const float highest = d - kRise * ds;
			if (force_over(col, highest, sides) > p.available) {
				const float rise = std::isnan(before) ? 0.0f : surface - before;
				if (rise >= 0.5f * ds) {
					stop(s - ds, kBlocked);
					p.wall = std::max(surface, top_ahead(s, next)) - (std::isnan(lowest) ? surface : lowest);
				} else {
					stop(s - ds, kStalls); // the floor would have to rise too fast: the edge stalls
				}
				break;
			}
			const float pushed = deepest_pushed(col, highest, next, sides);
			if (pushed < next) {
				next = pushed;
				limited = limited || next < depth - 1e-3f;
			}
			p.force = std::max(p.force, force_over(col, next, sides));
			top = std::max(top, next + surface);
		} else {
			next = d; // in the air (off the work, over a cut) it holds its depth
			if (exit < 0.0f && d > 0.0f) {
				exit = s; // the edge has left the work
			}
		}
		if (const unsigned why = blade(s, next, std::max(next + (std::isnan(last_rise) ? 0.0f : last_rise), next))) {
			stop(s - ds, why);
			break;
		}
		if (d < next && d + dive * ds > next) {
			// It levels between samples: exactly where, so the ramp stays one straight piece.
			p.floor.push_back({s - ds + (next - d) / dive, next});
		}
		if (d > next && d - dive * ds < next && next > 0.0f) {
			p.floor.push_back({s - ds + (d - next) / dive, next}); // (it levels, risen)
		}
		d = next;
		p.floor.push_back({s, d});
		p.depth = std::max(p.depth, d);
	}
	p.length = p.stop_at >= 0.0f ? p.stop_at : length;
	// The section reaches through the thickest chip (nothing thicker stands over the floor).
	p.height = top + 1.0f;
	if (limited && p.depth < depth - 1e-3f) {
		p.warnings |= kShallow;
	}
	if (chisel.kind != gl::SDF_TOOL_FLAT && p.depth > corners + 1e-3f) {
		p.warnings |= kCornersBuried;
	}
	if (p.length <= 0.0f) {
		return p;
	}

	// Chips, seeded: tear-out against the grain, the corners' tearing, breakout at the exit,
	// each a splinter along the grain.
	Rng rng{seed * 2654435761u + 0x9e3779b9u};
	rng.next();
	add_tear_out(p, work, 1.0f, seed);
	if (p.warnings & kCornersBuried) {
		// The buried corners tear the fibres beside the cut.
		for (float s = 3.0f + 4.0f * rng.next(); s < p.length - 1.0f && p.chips.size() < kMaxChips; s += 6.0f + 6.0f * rng.next()) {
			const float side = rng.next() < 0.5f ? -1.0f : 1.0f;
			const float torn = std::min((p.depth_at(s) - corners) * (0.5f + 0.5f * rng.next()), splinter_limit(p, s));
			if (torn > 0.02f) {
				const float end = std::min(s + 2.0f + 2.0f * rng.next(), p.length);
				p.chips.push_back(splinter(p, s, end, 0.0f, torn, 0.6f, b * (side * (0.5f * p.width + 0.3f))));
				p.chips_at.push_back(s);
			}
		}
	}
	if (chisel.kind == gl::SDF_TOOL_V && along < 0.5f && p.depth > 0.1f) {
		// Across the grain one wing cuts against it and tears.
		const float side = gl::dot(f * (gl::dot(f, n) >= 0.0f ? 1.0f : -1.0f), b) >= 0.0f ? -1.0f : 1.0f;
		const float half = p.depth * std::tan(0.5f * chisel.v_angle_deg * kDeg);
		for (float s = 2.0f + 3.0f * rng.next(); s < p.length - 1.0f && p.chips.size() < kMaxChips; s += 4.0f + 5.0f * rng.next()) {
			const float torn = std::min(p.depth_at(s) * (0.3f + 0.4f * rng.next() * wood.tearout), splinter_limit(p, s));
			const float end = std::min(s + 1.5f + 2.0f * rng.next(), p.length);
			p.chips.push_back(splinter(p, s, end, 0.0f, torn, 0.4f + 0.4f * rng.next(), b * (side * (half + 0.2f))));
			p.chips_at.push_back(s);
			p.warnings |= kTearOut;
		}
	}
	if (exit > 0.0f && g > 1.4f) {
		// Out of an edge across the grain: the unsupported fibres there break away, a splinter
		// running out to the edge.
		const float dip = std::min(p.depth_at(exit) * (1.0f + 2.0f * wood.split) * (0.4f + 0.3f * rng.next()), 2.0f);
		const float s0 = std::max(exit - 4.0f * (0.5f + wood.split), 0.0f);
		if (p.chips.size() >= kMaxChips) {
			p.chips.pop_back();
			p.chips_at.pop_back();
		}
		p.chips.push_back(splinter(p, s0, exit + 1.0f, p.depth_at(s0), p.depth_at(exit) + dip,
				0.5f * p.width * (0.6f + 0.4f * rng.next()), vec3(0.0f)));
		p.chips_at.push_back(exit);
		p.warnings |= kBreaksOut;
	}
	return p;
}

namespace {

// The shaving is sampled every half millimetre; thinner than this, nothing came off.
constexpr float kShavingStep = 0.5f;
constexpr float kShavingLeast = 0.01f;

} // namespace

namespace detail {

class PlannedStroke : public Stroke {
public:
	explicit PlannedStroke(const CutPlan &plan)
		: plan_(plan), piece_length_(plan.shaving_piece()), chip_sent_(plan.chips.size(), false) {}

	StrokeUpdate move_to(vec3 point) override {
		StrokeUpdate u;
		if (plan_.chop) {
			if (!struck_) { // the blow, as soon as the tool moves
				struck_ = true;
				u.edits = plan_.edits();
			}
			return u;
		}
		const float s = std::clamp(gl::dot(point - plan_.start, plan_.path), reached_, plan_.length);
		if (s < reached_ + 0.25f && !(s >= plan_.length && reached_ < plan_.length)) {
			return u;
		}
		reached_ = s;
		u.drop = emitted_;
		u.edits = plan_.edits(reached_, false);
		emitted_ = u.edits.size();
		return u;
	}

	std::vector<Edit> edits() const override {
		if (plan_.chop) {
			return plan_.edits();
		}
		return plan_.edits(reached_, false);
	}

	std::vector<Edit> finish() override {
		Edit lift;
		if (!plan_.chop && plan_.lift_out(reached_, lift)) {
			return {lift};
		}
		return {};
	}

	Frame pose() const override {
		if (plan_.chop) {
			return Frame::at(plan_.point(0.0f, plan_.depth), plan_.normal, plan_.path);
		}
		return Frame::at(plan_.point(reached_, plan_.depth_at(reached_)), plan_.normal, plan_.path);
	}

	void debris(const Body &body, const Octree &octree, Debris &out, bool ended) override {
		out.ended = out.ended || ended;
		if (plan_.chop) {
			if (struck_) {
				chips(body, octree, out, 1e9f); // a chop's pop-off comes with the blow
			}
			return;
		}
		// The shaving: the chip over the edge, column by column across it (from the edge's
		// section up to the surface it came in under), as wide as the columns holding wood.
		out.step = kShavingStep;
		const vec3 n = plan_.normal, b = gl::cross(plan_.normal, plan_.path);
		const float column = plan_.width / float(kColumns);
		for (; shaved_ <= reached_ + 1e-4f; shaved_ += kShavingStep) {
			const float s = shaved_;
			const float depth = plan_.depth_at(s);
			float over[kColumns];
			chip_columns(plan_, body, octree, s, depth, over);
			float area = 0.0f, moment = 0.0f;
			int first = -1, last = -1;
			for (int j = 0; j < kColumns; ++j) {
				if (over[j] >= kShavingLeast) {
					area += over[j] * column;
					moment += over[j] * column * column_across(plan_, j);
					first = first < 0 ? j : first;
					last = j;
				}
			}
			if (first < 0) {
				gap_ = true; // over air (off the work, over an earlier cut): it breaks here
				continue;
			}
			ShavingSample sample;
			sample.s = s;
			sample.width = float(last - first + 1) * column;
			sample.thickness = area / sample.width;
			sample.offset = moment / area;
			sample.point = plan_.point(s, depth) + b * sample.offset +
					n * (plan_.chisel.edge_height(sample.offset) + 0.5f * sample.thickness);
			sample.starts = gap_ || piece_ >= piece_length_;
			if (sample.starts) {
				piece_ = 0.0f;
			}
			gap_ = false;
			piece_ += kShavingStep;
			out.shaving.push_back(sample);
		}
		chips(body, octree, out, reached_);
	}

private:
	// The chips reached by `upto` and not yet sent: the material each takes beyond the
	// floor's cut (and the chips before it).
	void chips(const Body &body, const Octree &octree, Debris &out, float upto) {
		std::vector<Edit> taken;
		for (std::size_t i = 0; i < plan_.chips.size(); ++i) {
			if (chip_sent_[i] || plan_.chips_at[i] > upto) {
				continue;
			}
			if (taken.empty()) {
				taken = plan_.floor_edits(plan_.chop ? 1e9f : reached_);
			}
			chip_sent_[i] = true;
			Chip chip;
			if (measure_chip(body, octree, plan_.chips[i], taken, plan_.path, plan_.normal, chip)) {
				out.chips.push_back(chip);
			}
			taken.push_back(plan_.chips[i]);
		}
	}

public:
	const CutPlan &plan() const { return plan_; }
	float reached() const { return reached_; }
	// Its shaving goes on from `from`'s (steered on from it).
	void go_on_from(const PlannedStroke &from) {
		piece_ = from.piece_;
		gap_ = from.gap_;
	}

private:
	CutPlan plan_;
	float reached_ = 0.0f;
	std::size_t emitted_ = 0;
	bool struck_ = false;
	// The debris sent so far: the shaving up to `shaved_` (the length of its current piece,
	// whether it broke over a gap), the chips.
	float piece_length_;
	float shaved_ = 0.0f, piece_ = 0.0f;
	bool gap_ = true;
	std::vector<bool> chip_sent_;
};

} // namespace detail

std::unique_ptr<Stroke> planned_stroke(const CutPlan &plan) {
	return std::make_unique<detail::PlannedStroke>(plan);
}

// --- steered strokes -------------------------------------------------------------------

namespace {

// A point of a steered stroke's floor, the way its segment sweeps it.
struct FloorPoint {
	vec3 at;
	std::size_t segment;
	bool joint = false; // where a segment steered on from the last begins
};

// Whether two segments sweep the same section (height aside) the same way up.
bool same_sweep(const CutPlan &a, const CutPlan &b) {
	return a.chisel.kind == b.chisel.kind && std::fabs(a.width - b.width) < 1e-4f &&
			std::fabs(a.chisel.sweep_radius - b.chisel.sweep_radius) < 1e-4f &&
			std::fabs(a.chisel.v_angle_deg - b.chisel.v_angle_deg) < 1e-4f && gl::dot(a.normal, b.normal) > 0.99995f;
}

float distance_to_segment(vec3 p, vec3 a, vec3 b) {
	const vec3 d = b - a;
	const float t = std::clamp(gl::dot(p - a, d) / std::max(gl::dot(d, d), 1e-12f), 0.0f, 1.0f);
	return gl::length(p - (a + d * t));
}

float distance_to_bezier(vec3 p, vec3 a, vec3 c, vec3 b) {
	auto at = [&](float t) {
		const float u = 1.0f - t;
		return gl::length(p - (a * (u * u) + c * (2.0f * u * t) + b * (t * t)));
	};
	// The nearest of 64 samples along it, then narrowed down between its neighbours.
	int nearest = 0;
	float best = 1e9f;
	for (int i = 0; i <= 64; ++i) {
		const float d = at(float(i) / 64.0f);
		if (d < best) {
			best = d;
			nearest = i;
		}
	}
	float lo = float(std::max(nearest - 1, 0)) / 64.0f, hi = float(std::min(nearest + 1, 64)) / 64.0f;
	for (int k = 0; k < 30; ++k) {
		const float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
		(at(m1) < at(m2) ? hi : lo) = at(m1) < at(m2) ? m2 : m1;
	}
	return std::min(best, at(0.5f * (lo + hi)));
}

// Where the lines through a (along da) and b (along db) come closest: the control point of
// the quadratic leaving a along da and arriving at b along db; false where they are parallel
// or it lies behind either end.
bool control_point(vec3 a, vec3 da, vec3 b, vec3 db, vec3 &c) {
	const vec3 w = a - b;
	const float d1 = gl::dot(da, da), d2 = gl::dot(da, db), d3 = gl::dot(db, db);
	const float e1 = gl::dot(da, w), e2 = gl::dot(db, w);
	const float den = d1 * d3 - d2 * d2;
	if (den < 1e-8f * d1 * d3) {
		return false;
	}
	const float s = (d2 * e2 - d3 * e1) / den, t = (d1 * e2 - d2 * e1) / den;
	if (s <= 0.0f || t >= 0.0f) {
		return false;
	}
	c = 0.5f * ((a + da * s) + (b + db * t));
	return true;
}

} // namespace

SteeredStroke::SteeredStroke(const CutPlan &first) {
	segments_.push_back(std::make_unique<detail::PlannedStroke>(first));
}

SteeredStroke::~SteeredStroke() = default;

StrokeUpdate SteeredStroke::move_to(vec3 point) {
	return segments_.back()->move_to(point);
}

const CutPlan &SteeredStroke::plan() const {
	return segments_.back()->plan();
}

float SteeredStroke::reached() const {
	return segments_.back()->reached();
}

vec3 SteeredStroke::edge() const {
	const CutPlan &p = plan();
	return p.point(reached(), p.depth_at(reached()));
}

SteeredStroke::Going SteeredStroke::going_on(const Work &work, vec3 normal) const {
	const vec3 at = edge(), up = gl::normalize(normal);
	const float entry = depth_below_surface(work.body, work.octree, at, up, 30.0f);
	return {at + up * entry, entry};
}

void SteeredStroke::steer(CutPlan next) {
	const detail::PlannedStroke &last = *segments_.back();
	// Its sweep reaches back over the joint far enough to cover the outer corner there.
	const float turn = std::acos(std::clamp(gl::dot(last.plan().path, next.path), -1.0f, 1.0f));
	next.lead_in = std::min(0.5f + 0.5f * next.width * std::tan(std::min(turn, 1.0f)), 4.0f);
	auto segment = std::make_unique<detail::PlannedStroke>(next);
	segment->go_on_from(last);
	segments_.push_back(std::move(segment));
}

Frame SteeredStroke::pose() const {
	return segments_.back()->pose();
}

std::vector<Edit> SteeredStroke::finish() {
	return segments_.back()->finish();
}

void SteeredStroke::debris(const Body &body, const Octree &octree, Debris &out, bool ended) {
	segments_.back()->debris(body, octree, out, ended);
}

std::vector<Edit> SteeredStroke::edits() const {
	if (segments_.size() == 1) {
		return segments_.front()->edits();
	}
	// The floor's points, segment after segment (each segment's first where the last's edge
	// had got to: the same point twice).
	std::vector<FloorPoint> points;
	std::vector<Edit> chips;
	std::vector<float> deepest(segments_.size(), 0.0f);
	for (std::size_t k = 0; k < segments_.size(); ++k) {
		const CutPlan &p = segments_[k]->plan();
		const float upto = segments_[k]->reached();
		const std::vector<vec2> kept = p.floor_points(upto);
		for (std::size_t i = 0; i < kept.size(); ++i) {
			deepest[k] = std::max(deepest[k], kept[i].y);
			points.push_back({p.point(kept[i].x, kept[i].y), k, k > 0 && i == 0});
		}
		for (std::size_t i = 0; i < p.chips.size(); ++i) {
			if (p.chips_at[i] <= std::min(upto, p.length)) {
				chips.push_back(p.chips[i]);
			}
		}
	}
	auto same_point = [&](std::size_t a, std::size_t b) { return gl::length(points[a].at - points[b].at) < 1e-3f; };
	// The way the floor goes at a point: from the point before it to the one after (past
	// repeated points), which a polyline round a curve has as its tangent; at its ends, the
	// way of the piece there.
	auto tangent = [&](std::size_t i) {
		std::size_t before = i, after = i;
		while (before > 0 && same_point(before, i)) {
			--before;
		}
		while (after + 1 < points.size() && same_point(after, i)) {
			++after;
		}
		const vec3 a = same_point(before, i) ? points[i].at : points[before].at;
		const vec3 b = same_point(after, i) ? points[i].at : points[after].at;
		return b - a;
	};
	// Runs of points one segment's sweep can take (the same section, the same way up), each
	// swept as few pieces as fit within kSteeredFit: straight where they lie in line, else a
	// quadratic leaving and arriving along the floor there.
	std::vector<Edit> out;
	std::size_t i = 0;
	while (i + 1 < points.size()) {
		const CutPlan &first = segments_[points[i].segment]->plan();
		std::size_t end = i + 1; // the furthest point a piece from i reaches
		vec3 control{0.0f};
		bool curved = false;
		for (std::size_t j = i + 1; j < points.size(); ++j) {
			if (!same_sweep(first, segments_[points[j].segment]->plan())) {
				break;
			}
			if (j == i + 1 || same_point(j, i)) {
				end = j;
				continue;
			}
			// In line?
			bool fits = true;
			for (std::size_t m = i + 1; m < j && fits; ++m) {
				fits = distance_to_segment(points[m].at, points[i].at, points[j].at) <= kSteeredFit;
			}
			if (fits) {
				end = j;
				curved = false;
				continue;
			}
			// A quadratic: leaving along the floor at i, arriving along it at j; one the field
			// stays near-exact round (bending no tighter than its section follows: the body
			// takes none bending tighter still).
			vec3 c;
			if (control_point(points[i].at, tangent(i), points[j].at, tangent(j), c)) {
				float deep = 0.0f;
				for (std::size_t m = i; m <= j; ++m) {
					deep = std::max(deep, deepest[points[m].segment]);
				}
				fits = Primitive::sweep(points[i].at, c, points[j].at, first.normal, first.profile(deep)).lipschitz() <=
						kSteeredBend;
				for (std::size_t m = i + 1; m < j && fits; ++m) {
					fits = distance_to_bezier(points[m].at, points[i].at, c, points[j].at) <= kSteeredFit;
				}
				if (fits) {
					end = j;
					control = c;
					curved = true;
					continue;
				}
			}
			break;
		}
		if (same_point(end, i)) {
			i = end; // (nothing to sweep: a joint's repeated point)
			continue;
		}
		// Its section: the deepest of its segments' (height aside, they are the same).
		float deep = 0.0f;
		for (std::size_t m = i; m <= end; ++m) {
			deep = std::max(deep, deepest[points[m].segment]);
		}
		const ToolProfile section = first.profile(deep);
		// Its ends as a segment's sweep has them: the stroke's start (just above the surface,
		// or just outside an open face) or a joint's reach back over the corner there; half a
		// millimetre on into the next piece.
		vec3 from = points[i].at;
		const vec3 out_way = gl::normalize(curved ? control - points[i].at : points[end].at - points[i].at);
		if (i == 0) {
			const vec2 f = first.floor_points(segments_[0]->reached()).front();
			from = first.open || f.y > 0.0f ? from - out_way * first.lead_in : first.point(f.x, -0.2f);
		} else if (points[i].joint) {
			from = from - out_way * first.lead_in;
		}
		vec3 to = points[end].at;
		const vec3 in_way = gl::normalize(curved ? points[end].at - control : points[end].at - points[i].at);
		if (end + 1 < points.size()) {
			to = to + in_way * 0.5f;
		}
		out.push_back(cut(curved ? Primitive::sweep(from, control, to, first.normal, section)
								 : Primitive::sweep(from, to, first.normal, section)));
		// The next piece begins here (a joint's second copy: with its reach back).
		i = end + 1 < points.size() && points[end + 1].joint && same_point(end + 1, end) ? end + 1 : end;
	}
	out.insert(out.end(), chips.begin(), chips.end());
	return out;
}

} // namespace sdf::tools
