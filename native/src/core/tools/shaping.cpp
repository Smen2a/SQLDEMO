#include "tools/shaping.h"

#include "body/materials.h"
#include "tools/debris.h"
#include "tools/rubbing.h"
#include "eval/query.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace sdf::tools {

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

Edit cut(const Primitive &prim) {
	Edit e;
	e.prim = prim;
	e.op = Op::Subtract;
	return e;
}

Edit join(const Primitive &prim, std::uint16_t material, Blend blend = Blend::Hard, float r = 0.0f) {
	Edit e;
	e.prim = prim;
	e.op = Op::Union;
	e.blend = blend;
	e.r = e.r2 = r;
	e.material = material;
	return e;
}

// Rotation taking a cylinder's axis (local y) along x.
vec4 along_x() {
	return quat_axis_angle({0, 0, 1}, -90.0f * kDeg);
}

} // namespace

// --- rasp ------------------------------------------------------------------------------

Body Rasp::model() const {
	// Its working face at z = 0, its length along x: flat, or the lower part of a cylinder as
	// wide as the rasp (a half-round rasp worked on its round face). A tang and an ash handle
	// behind it (-x).
	Body b;
	const float half = 0.5f * length;
	if (round) {
		b.base = Primitive::cylinder({0.0f, 0.0f, round_radius}, round_radius, half, 0.0f, along_x());
		b.add(cut(Primitive::box({0.0f, 0.0f, thickness + 30.0f}, {half + 1.0f, round_radius + 1.0f, 30.0f})));
		for (const float side : {-1.0f, 1.0f}) {
			b.add(cut(Primitive::box({0.0f, side * (0.5f * width + 30.0f), 0.0f}, {half + 1.0f, 30.0f, 60.0f})));
		}
	} else {
		b.base = Primitive::box({0.0f, 0.0f, 0.5f * thickness}, {half, 0.5f * width, 0.5f * thickness}, 0.5f);
	}
	b.base_material = mat::Steel;
	b.add(join(Primitive::box({-half - 12.0f, 0.0f, 0.5f * thickness}, {12.0f, 3.0f, 2.0f}), mat::Steel));
	b.add(join(Primitive::cylinder({-half - 70.0f, 0.0f, 0.5f * thickness}, 13.0f, 50.0f, 5.0f, along_x()), mat::Ash,
			Blend::Round, 1.0f));
	b.grain_origin = {-half - 70.0f, 30.0f, 0.0f};
	b.grain_axis = {1, 0, 0};
	return b;
}

float Rasp::removal_per_mm(const Wood &wood) const {
	return 6.7e-4f * coarseness * pressure * 5740.0f / std::max(wood.hardness, 1.0f);
}

ToolProfile Rasp::profile(float height) const {
	return round ? ToolProfile::gouge(round_radius, width, height) : ToolProfile::flat(width, height);
}

std::unique_ptr<Stroke> rasp_stroke(const Rasp &rasp, const Work &work, vec3 contact, vec3 normal, vec3 path,
		float length, float pace, const Limits &limits) {
	// Set flat on the work (then tilted as the hand holds it).
	const Frame f = settle(work, Frame::at(contact, normal, path), {0.5f * rasp.length, 0.5f * rasp.width});
	// Tilted about its line, its face's normal turns across it.
	const float tilt = rasp.tilt_deg * kDeg;
	const vec3 up = f.z * std::cos(tilt) + f.y * std::sin(tilt);
	Frame tilted = f;
	tilted.z = up;
	tilted.y = gl::cross(up, f.x);
	// The round face's chord at a depth: narrow at first, the rasp's width once in far enough.
	const auto width_at = [=](float depth) {
		if (!rasp.round) {
			return rasp.width;
		}
		const float r = rasp.round_radius, d = std::min(depth, r);
		return std::min(rasp.width, 2.0f * std::sqrt(std::max(2.0f * r * d - d * d, 0.0f)));
	};
	RubFace face;
	face.length = rasp.length;
	face.width = rasp.width;
	face.rate = rasp.removal_per_mm(work.wood(contact - f.z * 0.5f));
	face.cuts = 1; // on the push, away from its handle
	face.line = true;
	face.from = 0.0f;
	face.to = length;
	face.grain = 0.4f + 0.6f * rasp.coarseness;
	face.thrown = true;
	face.limits = limits;
	face.pass = [rasp](const Frame &frame, vec2 lo, vec2 hi, float floor, float depth, float) {
		// Its width across the line, less where a marked line holds it (tools/layout.h).
		Rasp held = rasp;
		const float middle = 0.5f * (lo.y + hi.y);
		held.width = std::min(rasp.width, std::max(hi.y - lo.y, 0.01f));
		const vec3 a = frame.point({lo.x, middle, floor}), b = frame.point({hi.x, middle, floor});
		return cut(Primitive::sweep(a, b, frame.z, held.profile(depth + 2.0f)));
	};
	face.section = [width_at](float depth) {
		float area = 0.0f;
		for (int i = 0; i < 16; ++i) {
			area += width_at(depth * (float(i) + 0.5f) / 16.0f) * depth / 16.0f;
		}
		return area;
	};
	return rub_stroke(face, work, tilted, pace);
}

// --- card scraper ----------------------------------------------------------------------

Body CardScraper::model() const {
	// Stood on its long edge (the burr) at z = 0, along y; a little bowed, as flexed.
	Body b;
	b.base = Primitive::box({0.0f, 0.0f, 0.5f * height}, {0.5f * thickness, 0.5f * width, 0.5f * height}, 0.3f);
	b.base_material = mat::Steel;
	return b;
}

float CardScraper::per_pass(const Wood &wood) const {
	return 0.01f * pressure * 5740.0f / std::max(wood.hardness, 1.0f);
}

std::unique_ptr<Stroke> scraper_stroke(const CardScraper &scraper, const Work &work, vec3 contact, vec3 normal,
		vec3 path, float length, float pace, const Limits &limits) {
	// Held to the work (its burr across the stroke); a line 1 mm along it, so a point it
	// passes over loses per_pass() each push, however long the stroke.
	const Frame f = settle(work, Frame::at(contact, normal, path), {10.0f, 0.5f * scraper.width});
	RubFace face;
	face.length = 1.0f;
	face.width = scraper.width;
	face.rate = scraper.per_pass(work.wood(contact - f.z * 0.5f)) / face.length;
	face.cuts = 1; // pushed
	face.line = true;
	face.from = 0.0f;
	face.to = length;
	face.grain = 0.3f;
	face.thrown = true;
	face.limits = limits;
	// Flexed, its cut fades out over its feather at each side.
	SandingBlock pass_shape;
	pass_shape.feather = scraper.feather;
	face.pass = [pass_shape](const Frame &frame, vec2 lo, vec2 hi, float floor, float depth, float typical) {
		const float d = typical > 0.0f ? typical : depth;
		Frame rest = frame;
		rest.origin = frame.point({0.0f, 0.0f, floor + d});
		return pass_shape.pass(rest, lo, hi, d);
	};
	return rub_stroke(face, work, f, pace);
}

// --- spokeshave ------------------------------------------------------------------------

Body Spokeshave::model() const {
	const float bed = bed_deg * kDeg;
	Body b;
	if (kind == Kind::Spokeshave) {
		// Its sole on the work (z = 0) around the blade's edge at the origin; the body across
		// the stroke (y), a handle out to each side.
		b.base = Primitive::box({0.0f, 0.0f, 6.0f}, {0.5f * sole, 0.5f * blade_width + 6.0f, 6.0f}, 2.0f);
		b.base_material = mat::Steel;
		// The blade through its mouth, bedded at its angle.
		const vec4 bedded = quat_axis_angle({0, 1, 0}, -bed);
		b.add(join(Primitive::box({4.0f, 0.0f, 14.0f}, {1.5f, 0.5f * blade_width, 16.0f}, 0.0f, bedded), mat::Steel));
		for (const float side : {-1.0f, 1.0f}) {
			const vec4 turned = quat_axis_angle({1, 0, 0}, side * 20.0f * kDeg);
			b.add(join(Primitive::cylinder({0.0f, side * (0.5f * blade_width + 45.0f), 16.0f}, 11.0f, 40.0f, 5.0f, turned),
					mat::Walnut, Blend::Round, 3.0f));
		}
		b.grain_origin = {0.0f, 0.0f, 40.0f};
		b.grain_axis = {0, 1, 0};
		return b;
	}
	// A plane: its sole on the work (z = 0), the edge at the origin in its mouth, the iron
	// bedded low behind it (bevel up), rising towards the heel.
	const vec3 up_back{-std::cos(bed), 0.0f, std::sin(bed)};
	const vec4 bedded = quat_axis_angle({0, 1, 0}, bed);
	auto iron = [&](float length, float width) {
		return Primitive::box(up_back * (0.5f * length), {0.5f * length, 0.5f * width, 1.5f}, 0.0f, bedded);
	};
	if (kind == Kind::BlockPlane) {
		// A cast body: the sole and two cheeks, low at the ends; the iron under a brass
		// lever cap; a finger rest at the toe.
		b.base = Primitive::box({0.0f, 0.0f, 3.0f}, {0.5f * sole, 0.5f * sole_width, 3.0f}, 1.0f);
		b.base_material = mat::Steel;
		for (const float side : {-1.0f, 1.0f}) {
			b.add(join(Primitive::box({-4.0f, side * (0.5f * sole_width - 2.0f), 14.0f}, {0.5f * sole - 16.0f, 2.0f, 11.0f},
					2.0f), mat::Steel, Blend::Round, 2.0f));
		}
		b.add(join(iron(62.0f, blade_width), mat::Steel));
		b.add(join(Primitive::box(up_back * 34.0f + vec3(0.0f, 0.0f, 3.0f), {22.0f, 0.5f * blade_width - 1.0f, 3.0f}, 1.5f,
				bedded), mat::Brass));
		b.add(join(Primitive::sphere({0.5f * sole - 16.0f, 0.0f, 22.0f}, 9.0f), mat::Brass, Blend::Round, 3.0f));
		if (fence) {
			// A chamfer fence under each side of the sole: a plate lying on each face of the
			// arris (at 45 degrees to the sole), on a bracket down from the sole's side.
			for (const float side : {-1.0f, 1.0f}) {
				const vec4 laid = quat_axis_angle({1, 0, 0}, -side * 45.0f * kDeg);
				b.add(join(Primitive::box({0.0f, side * 20.0f, -19.0f}, {0.5f * sole - 20.0f, 8.0f, 1.0f}, 0.5f, laid),
						mat::Brass));
				b.add(join(Primitive::box({0.0f, side * (0.5f * sole_width + 1.5f), -7.0f}, {20.0f, 1.5f, 9.0f}, 0.5f),
						mat::Brass));
			}
		}
		b.grain_origin = {0.0f, 0.0f, 20.0f};
		b.grain_axis = {1, 0, 0};
		return b;
	}
	// A shoulder plane: a narrow steel body as wide as its iron, which runs flush with its
	// sides; a walnut infill behind the iron and a wedge over it.
	b.base = Primitive::box({0.0f, 0.0f, 16.0f}, {0.5f * sole, 0.5f * sole_width, 16.0f}, 1.0f);
	b.base_material = mat::Steel;
	b.add(join(iron(70.0f, blade_width), mat::Steel));
	b.add(join(Primitive::box({-0.25f * sole - 8.0f, 0.0f, 36.0f}, {0.25f * sole - 12.0f, 0.5f * sole_width - 1.0f, 6.0f},
			3.0f), mat::Walnut, Blend::Round, 2.0f));
	b.add(join(Primitive::box(up_back * 40.0f + vec3(4.0f, 0.0f, 4.0f), {16.0f, 0.5f * sole_width - 1.5f, 4.0f}, 1.0f,
			bedded), mat::Walnut));
	b.grain_origin = {0.0f, 0.0f, 36.0f};
	b.grain_axis = {1, 0, 0};
	return b;
}

CutPlan plan_spokeshave(const Spokeshave &shave, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, std::uint32_t seed, bool continuing) {
	Chisel blade;
	blade.width = shave.blade_width;
	blade.approach_deg = shave.bed_deg;
	blade.bevel_deg = 25.0f;
	blade.hand_force = shave.hand_force;
	blade.mallet = 0.0f;
	CutPlan p;
	p.chisel = blade;
	p.width = blade.width;
	// Held flat on the work: its sole settles on the face under it. With a fence, the fence
	// holds it as it was set (across an arris).
	const Frame set = Frame::at(start, normal, path);
	const Frame frame = shave.fence ? set : settle(work, set, {0.5f * shave.sole, 0.5f * shave.sole_width});
	const vec3 n = frame.z, t = frame.x, b = frame.y;
	// The sole across (its blade, and beyond it where the sole is wider).
	std::vector<float> across_sole{-0.5f * blade.width + 1.0f, 0.0f, 0.5f * blade.width - 1.0f};
	if (shave.sole_width > blade.width + 2.0f) {
		across_sole.push_back(-0.5f * shave.sole_width + 1.0f);
		across_sole.push_back(0.5f * shave.sole_width - 1.0f);
	}
	auto on_work = [&](vec3 at) {
		for (const float across : across_sole) {
			if (raycast(work.body, work.octree, at + b * across + n * 40.0f, -n, 100.0f, 1e-3f)) {
				return true;
			}
		}
		return false;
	};
	// A plane (its sole long) is started with its iron over the near end of the work: set on
	// within kPlaneStart of that end, its pass begins there. (Begun further in, its sole
	// would ride the wood an earlier pass left standing at the end, and take less and less.)
	if (shave.kind != Spokeshave::Kind::Spokeshave && !continuing) {
		for (float back = 0.5f; back <= kPlaneStart; back += 0.5f) {
			if (!on_work(start - t * back)) {
				start -= t * (back - 0.5f);
				length += back - 0.5f;
				break;
			}
		}
	}
	p.start = start;
	p.normal = frame.z;
	p.path = frame.x;
	p.open = true; // the sole sets the blade's depth: it cuts from its first millimetre
	const Wood wood = work.wood(start - n * 0.5f);
	p.split = wood.split;
	const vec3 f = work.fibre();
	const float g = grain_factor(f, t, b);
	p.grain = (g - 1.0f) / 3.5f;
	const vec3 ahead = gl::dot(f, t) >= 0.0f ? f : -f;
	const float along = std::fabs(gl::dot(f, t)), rise = gl::dot(ahead, n);
	p.slope = along < 0.5f || std::fabs(rise) < 0.005f ? 0 : (rise > 0.0f ? 1 : -1);
	p.available = shave.hand_force;
	auto stop = [&](float s, unsigned why) {
		p.stop_at = std::max(s, 0.0f);
		p.stop = why;
		p.warnings |= why;
	};

	// Its mouth passes a shaving so thick, and no thicker.
	if (depth > shave.mouth) {
		depth = shave.mouth;
		p.warnings |= kMouth;
	}

	// The surface's height above the plane it was set on along the path (NaN off the work):
	// the highest across its sole.
	const float nan = std::numeric_limits<float>::quiet_NaN();
	auto height_at = [&](float s) {
		float top = nan;
		for (const float across : across_sole) {
			const auto hit = raycast(work.body, work.octree, start + t * s + b * across + n * 40.0f, -n, 100.0f, 1e-3f);
			if (hit) {
				const float h = gl::dot(hit->point - start, n);
				top = std::isnan(top) ? h : std::max(top, h);
			}
		}
		return top;
	};
	const float half = 0.5f * shave.sole;
	const int from = int(std::floor(-half)), to = int(std::ceil(length + half));
	std::vector<float> heights;
	for (int i = from; i <= to; ++i) {
		heights.push_back(height_at(float(i)));
	}
	auto h = [&](int i) { return heights[std::size_t(std::clamp(i, from, to) - from)]; };
	// Where the sole rests: the lowest line lying on the surface under it, at its middle
	// (the upper hull of the heights under the sole): the surface itself where it is convex,
	// bridging hollows shorter than the sole. A plane's long sole is held flat on the work
	// (toe pressure going on, heel pressure going off), riding the highest under it: it does
	// not dip over a rounded end, or follow a curve.
	const bool flat = shave.kind != Spokeshave::Kind::Spokeshave;
	auto rest_at = [&](int s) {
		float best = -std::numeric_limits<float>::infinity();
		const int lo = std::max(from, s - int(half)), hi = std::min(to, s + int(half));
		if (flat) {
			for (int i = lo; i <= hi; ++i) {
				best = std::isnan(h(i)) ? best : std::max(best, h(i));
			}
			return best;
		}
		for (int i = lo; i <= s; ++i) {
			if (std::isnan(h(i))) {
				continue;
			}
			for (int j = s; j <= hi; ++j) {
				if (std::isnan(h(j))) {
					continue;
				}
				const float v = i == j ? h(i) : h(i) + (h(j) - h(i)) * float(s - i) / float(j - i);
				best = std::max(best, v);
			}
		}
		return best;
	};

	// Its toe, half a sole ahead of the blade, stops at a rise it cannot ride: over a
	// millimetre within two (a step, not a curve). The blade stops half a sole short of it.
	const int steps = std::max(1, int(std::ceil(length)));
	int last = steps;
	for (int i = 0; i <= steps; ++i) {
		const int toe = i + int(half);
		if (toe - 2 < from || toe > to || std::isnan(h(toe))) {
			continue;
		}
		const float before = std::isnan(h(toe - 2)) ? h(toe - 1) : std::isnan(h(toe - 1)) ? h(toe - 2)
																			   : std::min(h(toe - 1), h(toe - 2));
		if (!std::isnan(before) && h(toe) > before + 1.0f) {
			float wall = h(toe);
			for (int k = toe; k <= std::min(to, toe + 6); ++k) {
				wall = std::isnan(h(k)) ? wall : std::max(wall, h(k));
			}
			last = std::max(i - 1, 0);
			stop(float(last) * length / float(steps), kBlocked);
			p.wall = wall - before;
			break;
		}
	}

	// How deep two hands can push it: the chip's own section across the blade (columns under
	// its floor at the start: on a narrow edge, only as wide as the edge), and the strips its
	// attached sides tear.
	const float k = kCuttingResistance * wood.hardness * g;
	const float side_strip = 1.5f * (1.0f - 0.6f * wood.split * along * along);
	const int sides = std::min(2, int(work.field(start - n * 0.2f + b * (0.5f * p.width + 0.4f)) < 0.0f) +
			int(work.field(start - n * 0.2f - b * (0.5f * p.width + 0.4f)) < 0.0f));
	auto rest_or_plane = [&](int s) {
		const float r = rest_at(std::min(s, to));
		return std::isinf(r) ? 0.0f : r; // off the work: at the plane
	};
	// The force of the chip `d` deep under the sole resting at s (the columns read `asked` deep).
	auto force = [&](const float over[kChipColumns], float asked, float d) {
		float area = 0.0f, thick = 0.0f;
		for (int j = 0; j < kChipColumns; ++j) {
			const float o = std::max(over[j] - (asked - d), 0.0f);
			area += o;
			thick = std::max(thick, o);
		}
		return k * (area * p.width / float(kChipColumns) + float(sides) * side_strip * thick);
	};
	float over[kChipColumns];
	chip_columns(p, work.body, work.octree, 0.0f, depth - rest_or_plane(0), over);
	float d = depth;
	if (force(over, depth, depth) > p.available) {
		float lo = 0.0f, hi = depth;
		for (int i = 0; i < 24; ++i) {
			const float mid = 0.5f * (lo + hi);
			(force(over, depth, mid) <= p.available ? lo : hi) = mid;
		}
		d = lo;
		p.warnings |= kShallow;
	}
	p.depth = d;
	p.force = force(over, depth, d);
	// Along the path, every 2 mm: the chip there, which two hands must still push (a harder
	// or wider part of the work ahead can stall it).
	for (int i = 0; i <= last; ++i) {
		const float s = float(i) * length / float(steps);
		if (i > 0 && i % 2 == 0) {
			chip_columns(p, work.body, work.octree, s, d - rest_or_plane(i), over);
			const float here = force(over, d, d);
			if (here > 1.05f * p.available) {
				last = i - 2;
				stop(float(last) * length / float(steps), kStalls);
				p.floor.resize(std::size_t(std::max(last, 0) + 1));
				break;
			}
			p.force = std::max(p.force, here);
		}
		p.floor.push_back({s, d - rest_or_plane(i)});
	}
	p.length = p.stop_at >= 0.0f ? p.stop_at : length;
	p.height = d + 2.0f;
	p.lift_depth = d;
	add_tear_out(p, work, 0.5f, seed);
	return p;
}

} // namespace sdf::tools
