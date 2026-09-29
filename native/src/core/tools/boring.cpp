#include "tools/boring.h"

#include "body/materials.h"
#include "tools/debris.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf::tools {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kFreeze = 5.0f;  // mm of hole an update's open slice grows to before it is left behind
constexpr float kAbove = 5.0f;   // mm above the work a hole's cut begins
constexpr float kScrew = 1.2f;   // the lead screw's radius (mm)
constexpr float kDustStep = 0.5f; // mm of depth the waste is measured by

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

// Rotation taking a cylinder's axis (local y) along `axis` (unit).
vec4 along(vec3 axis) {
	const Frame f = Frame::at(vec3(0.0f), axis, {1.0f, 0.0f, 0.0f});
	return Frame{vec3(0.0f), f.x, axis, -f.y}.rotation();
}

// A cylinder along `axis` from a to b (distances along it from `at`).
Primitive rod(vec3 at, vec3 axis, float a, float b, float radius, float rounding = 0.0f) {
	return Primitive::cylinder(at + axis * (0.5f * (a + b)), radius, 0.5f * (b - a), rounding, along(axis));
}

} // namespace

Edit Bit::hole(vec3 contact, vec3 normal, float from, float to) const {
	return cut(rod(contact, -normal, from, to, radius()));
}

Edit Bit::screw_hole(vec3 contact, vec3 normal, float depth) const {
	return cut(rod(contact, -normal, depth - 1.0f, depth + screw, kScrew, 0.5f * kScrew));
}

Body Brace::model() const {
	const vec3 up(0.0f, 0.0f, 1.0f);
	const float r = bit.radius(), l = bit.length, out = 0.5f * sweep;
	Body b;
	// The bit: its screw under the cutters, the cutters, the twist, the shank into the chuck.
	b.base = rod(vec3(0.0f), up, 6.0f, 0.7f * l, 0.85f * r);
	b.base_material = mat::Steel;
	b.add(join(rod(vec3(0.0f), up, -bit.screw, 0.5f, kScrew, 0.5f * kScrew), mat::Steel));
	b.add(join(rod(vec3(0.0f), up, 0.0f, 6.0f, r, 0.3f), mat::Steel));
	// (the twist: its two flutes, notches either side of the core each quarter turn)
	for (float z = 10.0f; z < 0.7f * l - 4.0f; z += 0.4f * bit.diameter) {
		const float turn = 0.5f * kPi * z / (0.4f * bit.diameter);
		for (const float side : {-1.0f, 1.0f}) {
			const vec3 at(-std::sin(turn) * 0.8f * r * side, std::cos(turn) * 0.8f * r * side, z);
			b.add(cut(Primitive::box(at, {1.2f * r, 0.4f * r, 0.1f * bit.diameter}, 0.0f, quat_axis_angle(up, turn))));
		}
	}
	b.add(join(rod(vec3(0.0f), up, 0.7f * l - 1.0f, l + 10.0f, std::min(0.45f * r, 4.0f)), mat::Steel));
	// The chuck, and the crank: up from it, out to the grip, back in to the head.
	b.add(join(rod(vec3(0.0f), up, l, l + 45.0f, 13.0f, 3.0f), mat::Steel, Blend::Round, 2.0f));
	const vec3 bends[6] = {{0.0f, 0.0f, l + 40.0f}, {0.0f, 0.0f, l + 60.0f}, {out, 0.0f, l + 85.0f},
			{out, 0.0f, l + 185.0f}, {0.0f, 0.0f, l + 210.0f}, {0.0f, 0.0f, l + 232.0f}};
	for (int i = 0; i < 5; ++i) {
		b.add(join(Primitive::capsule(bends[i], bends[i + 1], 5.0f), mat::Steel, Blend::Round, 3.0f));
	}
	// A beech grip round the crank's outer run, and the round head on top.
	b.add(join(rod({out, 0.0f, 0.0f}, up, l + 95.0f, l + 175.0f, 14.0f, 5.0f), mat::Walnut));
	b.add(join(rod(vec3(0.0f), up, l + 225.0f, l + 250.0f, 32.0f, 9.0f), mat::Walnut, Blend::Round, 3.0f));
	b.grain_origin = {out, 0.0f, l + 135.0f};
	b.grain_axis = up;
	return b;
}

vec3 hold_bit(vec3 centre, vec3 normal, float radius, const Limits &limits) {
	const vec3 n = gl::normalize(normal);
	std::vector<Stop> walls;
	for (const std::vector<Stop> *kind : {&limits.sides, &limits.ends}) {
		for (const Stop &s : *kind) {
			if (std::fabs(gl::dot(s.normal, n)) < 0.3f) {
				walls.push_back(s);
			}
		}
	}
	vec3 c = centre;
	for (int pass = 0; pass < 3; ++pass) {
		for (const Stop &w : walls) {
			const float short_by = radius - w.at(c);
			if (short_by <= 1e-4f) {
				continue;
			}
			// Moved away from it by as much, unless that takes it too near a wall facing it:
			// then halfway between the two.
			float step = short_by;
			for (const Stop &v : walls) {
				if (gl::dot(v.normal, w.normal) < -0.9f && v.at(c) - short_by < radius) {
					step = 0.5f * (v.at(c) - w.at(c));
				}
			}
			const vec3 away = w.normal - n * gl::dot(w.normal, n);
			c = c + gl::normalize(away) * step;
		}
	}
	return c;
}

float depth_through(const Work &work, vec3 contact, vec3 normal, float radius) {
	const vec3 n = gl::normalize(normal);
	const Frame f = Frame::at(contact, n, {1.0f, 0.0f, 0.0f});
	const Aabb box = work.body.bounds();
	float extent = 0.0f;
	for (int c = 0; c < 8; ++c) {
		const vec3 corner(c & 1 ? box.hi.x : box.lo.x, c & 2 ? box.hi.y : box.lo.y, c & 4 ? box.hi.z : box.lo.z);
		extent = std::max(extent, gl::dot(contact - corner, n));
	}
	float deepest = 0.0f;
	for (float d = 0.25f; d <= extent + 0.5f; d += 0.5f) {
		for (int k = 0; k <= 8; ++k) {
			const float a = float(k) * 0.25f * kPi, rr = k == 0 ? 0.0f : 0.9f * radius;
			if (work.field(f.point({rr * std::cos(a), rr * std::sin(a), -d})) < 0.0f) {
				deepest = d + 0.25f;
				break;
			}
		}
	}
	return deepest;
}

namespace {

class BoringStroke : public Stroke {
public:
	BoringStroke(const Brace &brace, const Work &work, vec3 contact, vec3 normal, vec3 along, float pace,
			const Limits &limits)
		: brace_(brace), feed_(brace.bit.pitch * pace) {
		const vec3 n = gl::normalize(normal);
		frame_ = Frame::at(hold_bit(contact, n, brace.bit.radius(), limits), n, along);
		through_ = depth_through(work, frame_.origin, n, brace.bit.radius()) + 1.0f;
		const float floor = limits.depth_below(frame_.origin, n);
		deepest_ = std::max(std::min(through_, floor), 0.0f);
		at_line_ = floor < through_;
	}

	StrokeUpdate move_to(vec3 point) override {
		const vec3 r = point - frame_.origin;
		const float x = gl::dot(r, frame_.x), y = gl::dot(r, frame_.y);
		if (x * x + y * y < 0.25f) {
			return {}; // on the axis: no angle to read
		}
		const float a = std::atan2(y, x);
		if (!turning_) {
			turning_ = true;
			angle_ = a; // the hand takes the grip where it is
			return {};
		}
		float step = a - angle_;
		while (step > kPi) {
			step -= 2.0f * kPi;
		}
		while (step <= -kPi) {
			step += 2.0f * kPi;
		}
		angle_ = a;
		if (step < 0.0f) { // clockwise from above: the screw draws it in
			turns_ += -step / (2.0f * kPi);
			depth_ = std::min(depth_ + feed_ * -step / (2.0f * kPi), deepest_);
		}
		return update();
	}

	std::vector<Edit> edits() const override {
		if (cut_ <= 0.0f) {
			return {};
		}
		std::vector<Edit> out{brace_.bit.hole(frame_.origin, frame_.z, -kAbove, cut_)};
		if (cut_ < through_) {
			out.push_back(brace_.bit.screw_hole(frame_.origin, frame_.z, cut_));
		}
		return out;
	}

	Frame pose() const override {
		Frame f = frame_;
		f.x = frame_.x * std::cos(angle_) + frame_.y * std::sin(angle_);
		f.y = gl::cross(f.z, f.x);
		f.origin = frame_.point({0.0f, 0.0f, -cut_});
		return f;
	}

	StrokeState state() const override {
		StrokeState s;
		s.depth = cut_;
		if (cut_ >= deepest_ - 1e-3f) {
			s.limit = at_line_ ? "line" : "through";
		}
		return s;
	}

	// The waste comes up the twist and out of the hole's mouth: coarse dust, as much as the
	// wood in each slice of the hole (sampled over its disc).
	void debris(const Body &body, const Octree &octree, Debris &out, bool ended) override {
		out.ended = out.ended || ended;
		const float r = brace_.bit.radius();
		while (dusted_ + kDustStep <= cut_ + 1e-4f || (ended && dusted_ < cut_)) {
			const float to = std::min(dusted_ + kDustStep, cut_);
			const float mid = 0.5f * (dusted_ + to);
			int wood = 0, all = 0;
			for (int ring = 0; ring < 3; ++ring) {
				const int count = ring == 0 ? 1 : 8 * ring;
				for (int k = 0; k < count; ++k) {
					const float a = (float(k) + 0.5f * float(ring)) * 2.0f * kPi / float(count);
					const float rr = r * float(ring) / 2.5f;
					++all;
					wood += octree.distance(body, frame_.point({rr * std::cos(a), rr * std::sin(a), -mid})) < 0.0f;
				}
			}
			pending_ += kPi * r * r * (to - dusted_) * float(wood) / float(all);
			dusted_ = to;
		}
		if (pending_ < kLeastDust && !(ended && pending_ > 0.0f)) {
			return;
		}
		Dust d;
		d.point = frame_.point({0.0f, 0.0f, 1.0f});
		d.direction = frame_.z;
		d.volume = pending_;
		d.grain = 1.2f;
		d.spread = r;
		out.dust.push_back(d);
		pending_ = 0.0f;
	}

private:
	// The hole's newest slice is cut again as it deepens (with the screw's hole ahead of it),
	// and left behind once it is kFreeze deep.
	StrokeUpdate update() {
		StrokeUpdate u;
		if (depth_ < cut_ + 0.05f && !(depth_ >= deepest_ && cut_ < depth_)) {
			return u;
		}
		u.drop = open_;
		u.edits.push_back(brace_.bit.hole(frame_.origin, frame_.z, frozen_ > 0.0f ? frozen_ - 1.0f : -kAbove, depth_));
		u.edits.push_back(brace_.bit.screw_hole(frame_.origin, frame_.z, depth_));
		open_ = 2;
		if (depth_ - frozen_ >= kFreeze) {
			frozen_ = depth_;
			open_ = 0;
		}
		cut_ = depth_;
		return u;
	}

	Brace brace_;
	Frame frame_;
	float feed_;
	float through_ = 0.0f, deepest_ = 0.0f;
	bool at_line_ = false;
	bool turning_ = false;
	float angle_ = 0.0f, turns_ = 0.0f;
	float depth_ = 0.0f, cut_ = 0.0f, frozen_ = 0.0f;
	std::size_t open_ = 0;
	float dusted_ = 0.0f, pending_ = 0.0f;
};

} // namespace

std::unique_ptr<Stroke> boring_stroke(const Brace &brace, const Work &work, vec3 contact, vec3 normal, vec3 along,
		float pace, const Limits &limits) {
	return std::make_unique<BoringStroke>(brace, work, contact, normal, along, pace, limits);
}

} // namespace sdf::tools
