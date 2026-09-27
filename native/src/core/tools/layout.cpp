#include "tools/layout.h"

#include "body/materials.h"

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
