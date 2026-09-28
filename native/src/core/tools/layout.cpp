#include "tools/layout.h"

#include "body/materials.h"
#include "tools/cutting.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf::tools {

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;

Edit join(const Primitive &prim, std::uint16_t material, Blend blend = Blend::Hard, float r = 0.0f) {
	Edit e;
	e.prim = prim;
	e.op = Op::Union;
	e.blend = blend;
	e.r = e.r2 = r;
	e.material = material;
	return e;
}

// Rotation taking a cylinder's axis (local y) up, along z.
vec4 along_z() {
	return quat_axis_angle({1, 0, 0}, 90.0f * kDeg);
}

} // namespace

Body MarkingGauge::model() const {
	// The beam lying across the work from just behind the pin to beyond the fence, the fence
	// a block through it hanging down past the edge, the pin a steel point under the beam.
	const float far = distance + 30.0f;
	Body b;
	b.base = Primitive::box({0.0f, 0.5f * (far - 12.0f), 9.0f}, {8.0f, 0.5f * (far + 12.0f), 8.0f}, 1.0f);
	b.base_material = mat::Walnut;
	b.add(join(Primitive::box({0.0f, distance + 7.0f, 3.0f}, {35.0f, 7.0f, 33.0f}, 2.0f), mat::Walnut));
	// A brass wear strip on the fence's face.
	b.add(join(Primitive::box({0.0f, distance + 0.6f, 3.0f}, {33.0f, 0.6f, 31.0f}, 0.3f), mat::Brass));
	b.add(join(Primitive::cylinder({0.0f, 0.0f, 0.8f}, 0.7f, 1.2f, 0.0f, along_z()), mat::Steel));
	b.grain_origin = {0.0f, 0.0f, 9.0f};
	b.grain_axis = {0, 1, 0};
	return b;
}

Body MarkingKnife::model() const {
	// A thin blade standing on its point, bevelled on the far side; a round handle above.
	Body b;
	b.base = Primitive::box({12.0f, -0.6f, 11.0f}, {14.0f, 0.6f, 11.0f}, 0.2f);
	b.base_material = mat::Steel;
	b.add(join(Primitive::cylinder({12.0f, -4.0f, 70.0f}, 9.0f, 48.0f, 3.0f, along_z()), mat::Walnut, Blend::Round,
			2.0f));
	b.grain_origin = {12.0f, -4.0f, 70.0f};
	b.grain_axis = {0, 0, 1};
	return b;
}

Stop Stop::through(vec3 point, vec3 waste) {
	Stop s;
	s.normal = gl::normalize(waste);
	s.offset = -gl::dot(s.normal, point);
	return s;
}

float Limits::depth_below(vec3 p, vec3 normal) const {
	float allowed = std::numeric_limits<float>::infinity();
	for (const Stop &f : floors) {
		// Going down (-normal) takes it towards the floor at this rate; a floor it cannot
		// reach that way (square to the cut, or behind it) holds nothing.
		const float rate = gl::dot(f.normal, normal);
		if (rate > 0.2f) {
			allowed = std::min(allowed, f.at(p) / rate);
		}
	}
	return allowed;
}

float Limits::reach(vec3 p, vec3 dir) const {
	float nearest = std::numeric_limits<float>::infinity();
	for (const Stop &e : ends) {
		const float rate = -gl::dot(e.normal, dir); // how fast it goes towards the side to keep
		if (rate > 1e-4f) {
			nearest = std::min(nearest, std::max(e.at(p), 0.0f) / rate);
		}
	}
	return nearest;
}

bool limit_plan(CutPlan &plan, const Limits &limits) {
	if (plan.chop || limits.empty() || plan.floor.size() < 2) {
		return false;
	}
	bool held = false;
	// The edge's middle and corners (a skewed edge sweeps less than its width, still square to
	// the path here: near enough for a line).
	const vec3 across = gl::normalize(gl::cross(plan.normal, plan.path));
	const float half = 0.5f * plan.width;
	const vec3 corners[3] = {across * -half, vec3(0.0f), across * half};
	// Its end: where its edge first meets an end line, and square there.
	float end = plan.length;
	for (const vec3 &c : corners) {
		end = std::min(end, limits.reach(plan.start + c, plan.path));
	}
	if (end < plan.length - 1e-3f) {
		end = std::max(end, 0.0f);
		const float at_end = plan.depth_at(end);
		std::vector<vec2> kept;
		for (const vec2 &f : plan.floor) {
			if (f.x < end) {
				kept.push_back(f);
			}
		}
		kept.push_back({end, at_end});
		plan.floor = kept;
		std::vector<Edit> chips;
		std::vector<float> chips_at;
		for (std::size_t i = 0; i < plan.chips.size(); ++i) {
			if (plan.chips_at[i] <= end) {
				chips.push_back(plan.chips[i]);
				chips_at.push_back(plan.chips_at[i]);
			}
		}
		plan.chips = chips;
		plan.chips_at = chips_at;
		plan.length = end;
		plan.stop_at = end;
		plan.square_end = true;
		held = true;
	}
	// Its floor: no deeper than the floors under its edge.
	for (vec2 &f : plan.floor) {
		float allowed = std::numeric_limits<float>::infinity();
		for (const vec3 &c : corners) {
			allowed = std::min(allowed, limits.depth_below(plan.point(f.x, 0.0f) + c, plan.normal));
		}
		if (f.y > allowed + 1e-4f) {
			f.y = allowed;
			held = true;
		}
	}
	if (plan.lift_depth >= 0.0f) {
		plan.lift_depth = std::min(plan.lift_depth, std::max(plan.depth_at(plan.length), 0.0f));
	}
	if (held) {
		plan.depth = 0.0f;
		for (const vec2 &f : plan.floor) {
			plan.depth = std::max(plan.depth, f.y);
		}
		plan.stop |= kAtLine;
		if (plan.stop_at < 0.0f) {
			plan.stop_at = plan.length; // (the line: it goes all the way, no deeper)
		}
	}
	return held;
}

Chisel scribe_edge() {
	Chisel c;
	c.kind = gl::SDF_TOOL_V;
	c.v_angle_deg = 40.0f;
	c.width = 2.0f;
	c.thickness = 0.8f;
	c.bevel_deg = 20.0f;
	c.approach_deg = 60.0f; // stood steeply: in at once, a short ramp
	c.blade_length = 30.0f;
	return c;
}

} // namespace sdf::tools
