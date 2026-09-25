#include "tools/shaping.h"

#include "body/materials.h"

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

// A tool worked back and forth along a line over [0, length] from where it was set: every
// millimetre of travel deepens its pass by `rate`. Applied as it moves, the pass is re-cut
// every 5 um it deepens; merged (the preview, the commit), it is exactly as deep as the
// travel has taken it.
class PassStroke : public Stroke {
public:
	using Pass = std::function<Edit(float depth)>;
	PassStroke(const Frame &frame, float length, float rate, Pass pass)
		: frame_(frame), length_(length), rate_(rate), pass_(std::move(pass)) {}

	StrokeUpdate move_to(vec3 point) override {
		const float s = std::clamp(gl::dot(point - frame_.origin, frame_.x), 0.0f, length_);
		travel_ += std::fabs(s - at_);
		at_ = s;
		const float depth = rate_ * travel_;
		StrokeUpdate u;
		if (depth >= cut_ + 0.005f) {
			u.drop = cut_ > 0.0f ? 1 : 0;
			u.edits.push_back(pass_(depth));
			cut_ = depth;
		}
		return u;
	}

	std::vector<Edit> edits() const override {
		if (travel_ <= 0.0f) {
			return {};
		}
		return {pass_(rate_ * travel_)};
	}

	Frame pose() const override {
		Frame f = frame_;
		f.origin = frame_.point({at_, 0.0f, -rate_ * travel_});
		return f;
	}

private:
	Frame frame_;
	float length_, rate_;
	Pass pass_;
	float travel_ = 0.0f, at_ = 0.0f, cut_ = 0.0f;
};

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
	return 8e-4f * coarseness * pressure * 5740.0f / std::max(wood.hardness, 1.0f);
}

ToolProfile Rasp::profile(float height) const {
	return round ? ToolProfile::gouge(round_radius, width, height) : ToolProfile::flat(width, height);
}

std::unique_ptr<Stroke> rasp_stroke(const Rasp &rasp, const Wood &wood, vec3 contact, vec3 normal, vec3 path,
		float length) {
	const Frame f = Frame::at(contact, normal, path);
	// Tilted about its line, its face's normal turns across it.
	const float tilt = rasp.tilt_deg * kDeg;
	const vec3 up = f.z * std::cos(tilt) + f.y * std::sin(tilt);
	Frame tilted = f;
	tilted.z = up;
	tilted.y = gl::cross(up, f.x);
	return std::make_unique<PassStroke>(tilted, length, rasp.removal_per_mm(wood), [=](float depth) {
		const vec3 a = contact - up * depth, b = a + f.x * length;
		return cut(Primitive::sweep(a, b, up, rasp.profile(depth + 2.0f)));
	});
}

// --- card scraper ----------------------------------------------------------------------

Body CardScraper::model() const {
	// Stood on its long edge (the burr) at z = 0, along y; a little bowed, as flexed.
	Body b;
	b.base = Primitive::box({0.0f, 0.0f, 0.5f * height}, {0.5f * thickness, 0.5f * width, 0.5f * height}, 0.3f);
	b.base_material = mat::Steel;
	return b;
}

float CardScraper::removal_per_mm(const Wood &wood) const {
	return 6e-5f * pressure * 5740.0f / std::max(wood.hardness, 1.0f);
}

std::unique_ptr<Stroke> scraper_stroke(const CardScraper &scraper, const Wood &wood, vec3 contact, vec3 normal,
		vec3 path, float length) {
	const Frame f = Frame::at(contact, normal, path);
	SandingBlock pass_shape;
	pass_shape.feather = scraper.feather;
	const float half = 0.5f * scraper.width;
	return std::make_unique<PassStroke>(f, length, scraper.removal_per_mm(wood), [=](float depth) {
		return pass_shape.pass(f, {0.0f, -half}, {length, half}, depth);
	});
}

// --- spokeshave ------------------------------------------------------------------------

Body Spokeshave::model() const {
	// Its sole on the work (z = 0) around the blade's edge at the origin; the body across the
	// stroke (y), a handle out to each side.
	Body b;
	b.base = Primitive::box({0.0f, 0.0f, 6.0f}, {0.5f * sole, 0.5f * blade_width + 6.0f, 6.0f}, 2.0f);
	b.base_material = mat::Steel;
	// The blade through its mouth, bedded at its angle.
	const float bed = bed_deg * kDeg;
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

CutPlan plan_spokeshave(const Spokeshave &shave, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, std::uint32_t seed) {
	Chisel blade;
	blade.width = shave.blade_width;
	blade.approach_deg = shave.bed_deg;
	blade.bevel_deg = 25.0f;
	blade.hand_force = shave.hand_force;
	blade.mallet = 0.0f;
	CutPlan p;
	p.chisel = blade;
	p.width = blade.width;
	const Frame frame = Frame::at(start, normal, path);
	p.start = start;
	p.normal = frame.z;
	p.path = frame.x;
	p.open = true; // the sole sets the blade's depth: it cuts from its first millimetre
	const vec3 n = frame.z, t = frame.x, b = frame.y;
	const Wood wood = work.wood(start - n * 0.5f);
	p.split = wood.split;
	const vec3 f = work.fibre();
	const float g = grain_factor(f, t, b);
	p.grain = (g - 1.0f) / 3.5f;
	const vec3 ahead = gl::dot(f, t) >= 0.0f ? f : -f;
	const float along = std::fabs(gl::dot(f, t)), rise = gl::dot(ahead, n);
	p.slope = along < 0.5f || std::fabs(rise) < 0.005f ? 0 : (rise > 0.0f ? 1 : -1);
	p.available = shave.hand_force;

	// The surface's height above the plane it was set on, along the path (NaN off the work).
	const float nan = std::numeric_limits<float>::quiet_NaN();
	auto height_at = [&](float s) {
		const vec3 at = start + t * s;
		float hi = 20.0f;
		if (work.field(at + n * hi) <= 0.0f) {
			return hi;
		}
		for (float z = hi - 0.5f; z > -40.0f; z -= 0.5f) {
			if (work.field(at + n * z) <= 0.0f) {
				float lo = z;
				for (int i = 0; i < 20; ++i) {
					const float mid = 0.5f * (lo + hi);
					(work.field(at + n * mid) <= 0.0f ? lo : hi) = mid;
				}
				return 0.5f * (lo + hi);
			}
			hi = z;
		}
		return nan;
	};
	const float half = 0.5f * shave.sole;
	const int from = int(std::floor(-half)), to = int(std::ceil(length + half));
	std::vector<float> heights;
	for (int i = from; i <= to; ++i) {
		heights.push_back(height_at(float(i)));
	}
	auto h = [&](int i) { return heights[std::size_t(i - from)]; };
	// Where the sole rests: the lowest line lying on the surface under it, at its middle
	// (the upper hull of the heights under the sole): the surface itself where it is convex,
	// bridging hollows shorter than the sole.
	auto rest_at = [&](int s) {
		float best = -std::numeric_limits<float>::infinity();
		const int lo = std::max(from, s - int(half)), hi = std::min(to, s + int(half));
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

	// How deep two hands can push it: F = k (width + attached sides) d.
	const float k = kCuttingResistance * wood.hardness * g;
	const float side_strip = 1.5f * (1.0f - 0.6f * wood.split * along * along);
	const int sides = std::min(2, int(work.field(start - n * 0.2f + b * (0.5f * p.width + 0.4f)) < 0.0f) +
			int(work.field(start - n * 0.2f - b * (0.5f * p.width + 0.4f)) < 0.0f));
	const float per_mm = k * (p.width + float(sides) * side_strip);
	const float d = std::min(depth, p.available / std::max(per_mm, 1e-6f));
	if (d < depth - 1e-3f) {
		p.warnings |= kShallow;
	}
	p.depth = d;
	p.force = per_mm * d;
	const int steps = std::max(1, int(std::ceil(length)));
	for (int i = 0; i <= steps; ++i) {
		const float rest = rest_at(std::min(i, to));
		const float floor = std::isinf(rest) ? -d : rest - d; // off the work: at the plane
		p.floor.push_back({float(i) * length / float(steps), -floor});
	}
	p.length = length;
	p.height = d + 2.0f;
	p.lift_depth = d;
	add_tear_out(p, work, 0.5f, seed);
	return p;
}

} // namespace sdf::tools
