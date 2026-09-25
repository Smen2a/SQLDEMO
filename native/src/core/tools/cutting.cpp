#include "tools/cutting.h"

#include "tools/debris.h"

#include <algorithm>
#include <cmath>

namespace sdf::tools {

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
// kCuttingResistance (cutting.h): a 12 mm bench chisel pushed by hand (200 N) takes about
// 0.5 mm along the grain in ash, 0.25 mm across it.
constexpr float kCutting = kCuttingResistance;
constexpr float kSideStrip = 1.5f;   // mm of chip each attached side adds
constexpr float kClearance = 2.0f * kDeg;
constexpr float kDigIn = 8.0f * kDeg; // diving more steeply than this, the edge digs in
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
			"slit only", "pops off", "digs in", "splits"};
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
	if (chop || floor.size() < 2) {
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

std::vector<Edit> CutPlan::floor_edits(float upto) const {
	std::vector<Edit> out;
	if (chop) {
		out.push_back(slit);
		return out;
	}
	if (floor.size() < 2) {
		return out;
	}
	const float end = std::min(upto, length);
	if (end <= floor.front().x) {
		return out;
	}
	std::vector<vec2> points;
	for (const vec2 &f : floor) {
		if (f.x >= end) {
			break;
		}
		points.push_back(f);
	}
	points.push_back({end, depth_at(end)});
	std::vector<vec2> kept{points.front()};
	if (points.size() > 1) {
		simplify(points, 0, points.size() - 1, 0.015f, kept);
	}
	float deepest = 0.0f;
	for (const vec2 &k : kept) {
		deepest = std::max(deepest, k.y);
	}
	Chisel c = chisel;
	if (c.kind == gl::SDF_TOOL_FLAT) {
		c.width = width;
	}
	const ToolProfile profile = c.profile(height > 0.0f ? height : deepest + 2.0f);
	for (std::size_t i = 0; i + 1 < kept.size(); ++i) {
		// A cut that starts at depth (from an open face) starts just outside it; one that
		// ramps in, just above the surface. Pieces overlap half a millimetre: flush ends
		// leave a seam the raymarcher would see as a wall.
		vec2 a = kept[i], b = kept[i + 1];
		if (i == 0) {
			a = open || a.y > 0.0f ? vec2(a.x - 0.5f, a.y) : vec2(a.x, -0.2f);
		}
		vec3 from = point(a.x, a.y), to = point(b.x, b.y);
		if (i + 2 < kept.size()) {
			to = to + gl::normalize(to - from) * 0.5f;
		}
		out.push_back(cut(Primitive::sweep(from, to, normal, profile)));
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

CutPlan plan_chop(CutPlan p, const Work &work, const Wood &wood, vec3 b, vec3 edge) {
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
	const float driven = c.mallet > 0.0f ? kBlow * c.mallet : kPush * c.hand_force / 200.0f;
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
		const float floor = p.depth_at(s);
		if (roll >= risk) {
			continue;
		}
		const float dip = std::min(p.depth * (0.4f + 1.2f * risk * rng.next()), 2.5f);
		const float reach = (2.0f + 4.0f * rng.next()) * (0.5f + wood.split);
		const float end = std::min(s + reach, p.length);
		const vec3 side = b * (off * 0.5f * (p.width - w));
		p.chips.push_back(cut(Primitive::sweep(p.point(s, floor) + side, p.point(end, p.depth_at(end) + dip) + side,
				p.normal, ToolProfile::flat(w, dip + (p.height > 0.0f ? p.height : p.depth_at(end) + 1.0f)))));
		p.chips_at.push_back(end);
		p.warnings |= kTearOut;
	}
}

CutPlan plan_cut(const Chisel &chisel, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, float skew_deg, std::uint32_t seed) {
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
		return plan_chop(p, work, wood, b, edge);
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
	auto force = [&](float d, int sides) {
		const float area = chisel.kind == gl::SDF_TOOL_FLAT ? p.width * d : chisel.chip_area(d);
		return k * (area + float(sides) * side_strip * std::max(d - corners, 0.0f));
	};
	// The deepest the hand can take with `sides` attached: F(d) = available, by bisection.
	auto reachable = [&](int sides) {
		if (force(depth, sides) <= p.available) {
			return depth;
		}
		float lo = 0.0f, hi = depth;
		for (int i = 0; i < 30; ++i) {
			const float mid = 0.5f * (lo + hi);
			(force(mid, sides) <= p.available ? lo : hi) = mid;
		}
		return lo;
	};

	// In: from an open face at its depth at once; mid-face, only past its bevel's clearance,
	// diving at the difference.
	const bool open = open_behind(work, start, t, n, b, p.width, depth);
	p.open = open;
	const float tilt = chisel.approach_deg * kDeg, bevel = chisel.bevel_deg * kDeg;
	float dive = 1e9f;
	if (!open) {
		if (tilt < bevel + kClearance) {
			p.warnings |= kSkates;
			return p;
		}
		dive = std::tan(tilt - bevel);
		if (tilt - bevel > kDigIn + 1e-4f) {
			p.warnings |= kDigsIn;
		}
	}

	// Along the path a millimetre at a time: the floor dives until it levels at the depth,
	// or at what the hand can push where that is less.
	const int steps = std::max(1, int(std::ceil(length)));
	const float ds = length / float(steps);
	float d = open ? std::min(depth, reachable(attached_sides(work, start, n, b, p.width, depth))) : 0.0f;
	p.floor.push_back({0.0f, d});
	float exit = -1.0f;
	bool limited = false;
	for (int i = 1; i <= steps; ++i) {
		const float s = float(i) * ds;
		const vec3 at = start + t * s;
		const float probe = std::max(std::max(d, 0.1f), std::min(depth, d + dive * ds));
		const bool in_wood = work.field(at - n * (0.5f * probe)) < 0.0f;
		float target = d; // in the air (off the work, over a cut) it holds its depth
		if (in_wood) {
			int sides = attached_sides(work, at, n, b, p.width, probe);
			if (chisel.kind != gl::SDF_TOOL_FLAT && probe <= corners) {
				sides = 0; // corners out: the chip's sides are free
			}
			target = reachable(sides);
			limited = limited || target < depth - 1e-3f;
			p.force = std::max(p.force, force(std::min(target, std::min(depth, d + dive * ds)), sides));
		} else if (exit < 0.0f && d > 0.0f) {
			exit = s; // the edge has left the work
		}
		if (d < target && d + dive * ds > target) {
			// It levels between samples: exactly where, so the ramp stays one straight piece.
			p.floor.push_back({s - ds + (target - d) / dive, target});
		}
		d = std::min(target, d + dive * ds);
		p.floor.push_back({s, d});
		p.depth = std::max(p.depth, d);
	}
	p.length = length;
	if (limited && p.depth < depth - 1e-3f) {
		p.warnings |= kShallow;
	}
	if (chisel.kind != gl::SDF_TOOL_FLAT && p.depth > corners + 1e-3f) {
		p.warnings |= kCornersBuried;
	}

	// Chips, seeded: tear-out against the grain, the corners' tearing, breakout at the exit.
	Rng rng{seed * 2654435761u + 0x9e3779b9u};
	rng.next();
	add_tear_out(p, work, 1.0f, seed);
	if (p.warnings & kCornersBuried) {
		// The buried corners tear the fibres beside the cut.
		for (float s = 3.0f + 4.0f * rng.next(); s < p.length - 1.0f && p.chips.size() < kMaxChips; s += 6.0f + 6.0f * rng.next()) {
			const float side = rng.next() < 0.5f ? -1.0f : 1.0f;
			const float torn = (p.depth_at(s) - corners) * (0.5f + 0.5f * rng.next());
			if (torn > 0.02f) {
				const vec3 at = p.point(s, 0.0f) + b * (side * (0.5f * p.width + 0.3f));
				p.chips.push_back(cut(Primitive::sweep(at + n * 0.3f, at + t * (2.0f + 2.0f * rng.next()) - n * torn, n,
						ToolProfile::flat(1.2f, torn + 0.6f))));
				p.chips_at.push_back(s);
			}
		}
	}
	if (chisel.kind == gl::SDF_TOOL_V && along < 0.5f && p.depth > 0.1f) {
		// Across the grain one wing cuts against it and tears.
		const float side = gl::dot(f * (gl::dot(f, n) >= 0.0f ? 1.0f : -1.0f), b) >= 0.0f ? -1.0f : 1.0f;
		const float half = p.depth * std::tan(0.5f * chisel.v_angle_deg * kDeg);
		for (float s = 2.0f + 3.0f * rng.next(); s < p.length - 1.0f && p.chips.size() < kMaxChips; s += 4.0f + 5.0f * rng.next()) {
			const float torn = p.depth_at(s) * (0.3f + 0.4f * rng.next() * wood.tearout);
			const vec3 at = p.point(s, 0.0f) + b * (side * (half + 0.2f));
			p.chips.push_back(cut(Primitive::sweep(at + n * 0.3f, at + t * (1.5f + 2.0f * rng.next()) - n * torn, n,
					ToolProfile::flat(0.8f + rng.next(), torn + 0.6f))));
			p.chips_at.push_back(s);
			p.warnings |= kTearOut;
		}
	}
	if (exit > 0.0f && g > 1.4f) {
		// Out of an edge across the grain: the unsupported fibres there break away.
		const float r = std::min(p.depth_at(exit) * (1.0f + 2.0f * wood.split) * (0.7f + 0.6f * rng.next()), 4.0f);
		const float s0 = std::max(exit - 2.0f * r, 0.0f);
		if (p.chips.size() >= kMaxChips) {
			p.chips.pop_back();
			p.chips_at.pop_back();
		}
		p.chips.push_back(cut(Primitive::sweep(p.point(s0, p.depth_at(s0)), p.point(exit + 1.0f, p.depth_at(exit) + r), n,
				ToolProfile::flat(p.width * (0.6f + 0.4f * rng.next()), p.depth_at(exit) + r + 1.0f))));
		p.chips_at.push_back(exit);
		p.warnings |= kBreaksOut;
	}
	if (p.warnings & kDigsIn) {
		// Where the steep dive levels, the edge has already gone on in.
		for (std::size_t i = 1; i < p.floor.size(); ++i) {
			if (p.floor[i].y >= p.depth - 1e-4f) {
				const float s = p.floor[i].x;
				const float dig = std::min(p.depth * std::tan(tilt - bevel) * 2.0f, 1.5f);
				if (p.chips.size() < kMaxChips) {
					p.chips.push_back(cut(Primitive::sweep(p.point(std::max(s - 1.0f, 0.0f), p.depth), p.point(s + 3.0f, p.depth + dig), n,
							chisel.kind == gl::SDF_TOOL_FLAT ? ToolProfile::flat(p.width * 0.8f, p.depth + dig + 1.0f)
															 : chisel.profile(p.depth + dig + 1.0f))));
					p.chips_at.push_back(s);
				}
				break;
			}
		}
	}
	return p;
}

namespace {

// The shaving is sampled every half millimetre; thinner than this, nothing came off.
constexpr float kShavingStep = 0.5f;
constexpr float kShavingLeast = 0.01f;

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
		// The shaving, from the floor up to the surface the edge came in under.
		out.step = kShavingStep;
		const vec3 n = plan_.normal;
		const float most = (plan_.height > 0.0f ? plan_.height : plan_.depth + 2.0f);
		const bool flat = plan_.chisel.kind == gl::SDF_TOOL_FLAT;
		for (; shaved_ <= reached_ + 1e-4f; shaved_ += kShavingStep) {
			const float s = shaved_;
			const vec3 floor = plan_.point(s, plan_.depth_at(s));
			const float t = depth_below_surface(body, octree, floor, n, most);
			if (t < kShavingLeast) {
				gap_ = true; // over air (off the work, over an earlier cut): it breaks here
				continue;
			}
			ShavingSample sample;
			sample.s = s;
			sample.thickness = t;
			sample.width = flat ? plan_.width : plan_.chisel.chip_area(t) / t;
			sample.point = floor + n * (0.5f * t);
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

} // namespace

std::unique_ptr<Stroke> planned_stroke(const CutPlan &plan) {
	return std::make_unique<PlannedStroke>(plan);
}

} // namespace sdf::tools
