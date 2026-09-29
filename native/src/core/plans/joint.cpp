#include "plans/joint.h"

#include <cmath>

namespace sdf::plans {

namespace {

constexpr float kAround = 1.0f; // mm round the part that goes in, where its surface is sampled

vec3 unit(int axis) {
	return axis == 0 ? vec3(1, 0, 0) : axis == 1 ? vec3(0, 1, 0) : vec3(0, 0, 1);
}

// A range, or the whole of `size` where it has none.
vec2 range(vec2 r, float size) {
	return r.y > r.x ? r : vec2(0.0f, size);
}

// The reflection through the plane through `c` square to the unit `n`.
Pose reflection(vec3 c, vec3 n) {
	Pose r;
	r.x = vec3(1, 0, 0) - n * (2.0f * n.x);
	r.y = vec3(0, 1, 0) - n * (2.0f * n.y);
	r.z = vec3(0, 0, 1) - n * (2.0f * n.z);
	r.origin = n * (2.0f * gl::dot(n, c));
	return r;
}

// The proper rotation taking b's directions u1, u2 (orthonormal) to a's v1, v2.
Pose rotation(vec3 u1, vec3 u2, vec3 v1, vec3 v2) {
	const vec3 u3 = gl::cross(u1, u2), v3 = gl::cross(v1, v2);
	Pose r;
	r.x = v1 * u1.x + v2 * u2.x + v3 * u3.x;
	r.y = v1 * u1.y + v2 * u2.y + v3 * u3.y;
	r.z = v1 * u1.z + v2 * u2.z + v3 * u3.z;
	return r;
}

// A tenon (b) into a hole (a): along the tenon, into the hole's face; the section's longer
// side along the hole's longer side; home with the shoulder on the face.
JointPose tenon_into_hole(const Part &a, const Feature &hole, const Part &b, const Feature &tenon) {
	JointPose j;
	j.kind = JointPose::Kind::MortiseTenon;
	const vec3 sa = a.size, sb = b.size;
	const float length = tenon.length;
	const float shoulder = tenon.far ? sb.x - length : length;
	const vec3 out = tenon.far ? vec3(1, 0, 0) : vec3(-1, 0, 0); // out of the shoulder
	const vec2 ty = range(tenon.y, sb.y), tz = range(tenon.z, sb.z);
	const vec3 middle_b(shoulder, 0.5f * (ty.x + ty.y), 0.5f * (tz.x + tz.y));
	const vec3 long_b = ty.y - ty.x >= tz.y - tz.x ? vec3(0, 1, 0) : vec3(0, 0, 1);

	const int axis = axis_of(hole.face), across = axis == 2 ? 1 : 2;
	const vec3 in = outward(hole.face) * -1.0f;
	const vec2 hx = range(hole.along, sa.x), ha = hole.across;
	vec3 middle_a(0.5f * (hx.x + hx.y), 0.0f, 0.0f);
	(across == 1 ? middle_a.y : middle_a.z) = 0.5f * (ha.x + ha.y);
	(axis == 1 ? middle_a.y : middle_a.z) = position(hole.face, sa);
	const vec3 long_a = hx.y - hx.x >= ha.y - ha.x ? vec3(1, 0, 0) : unit(across);

	j.home = rotation(out, long_b, in, long_a);
	j.home.origin = middle_a - j.home.turn(middle_b);
	j.mirror = reflection(middle_b, long_b.y > 0.5f ? vec3(0, 0, 1) : vec3(0, 1, 0));
	j.axis = in;
	j.travel = length;
	// The tenon, and a little round it: its shoulders, and the part just behind them.
	const float x0 = tenon.far ? shoulder - 0.5f : -kAround, x1 = tenon.far ? sb.x + kAround : shoulder + 0.5f;
	j.region = {vec3(x0, -kAround, -kAround), vec3(x1, sb.y + kAround, sb.z + kAround)};
	return j;
}

// A wedge (b, tapered) into a kerf (a): thin end first, along the kerf's depth; its
// thickness across the kerf, centred on it; home with its thin end at the kerf's bottom.
JointPose wedge_into_kerf(const Part &a, const Feature &kerf, const Part &b, const Feature &taper) {
	JointPose j;
	j.kind = JointPose::Kind::Wedge;
	const vec3 sa = a.size, sb = b.size;
	const vec3 in = kerf.far ? vec3(-1, 0, 0) : vec3(1, 0, 0);
	const vec3 square = unit(kerf.axis == 2 ? 2 : 1); // square to the kerf's faces
	const vec3 mouth_a(kerf.far ? sa.x : 0.0f, kerf.axis == 2 ? 0.5f * sa.y : kerf.at, kerf.axis == 2 ? kerf.at : 0.5f * sa.z);

	const bool thin_far = taper.to <= taper.from;
	const vec3 lead = thin_far ? vec3(1, 0, 0) : vec3(-1, 0, 0);
	const float thin = thin_far ? taper.to : taper.from;
	const bool off_back = taper.face == Face::Back; // (the wood is then z 0 to its thickness)
	const vec3 tip_b(thin_far ? sb.x : 0.0f, 0.5f * sb.y, off_back ? 0.5f * thin : sb.z - 0.5f * thin);

	j.home = rotation(lead, vec3(0, 0, 1), in, square);
	j.home.origin = mouth_a + in * kerf.depth - j.home.turn(tip_b);
	j.mirror = reflection(vec3(0.0f, 0.5f * sb.y, 0.0f), vec3(0, 1, 0));
	j.axis = in;
	j.travel = kerf.depth;
	j.region = {vec3(-kAround), sb + vec3(kAround)};
	return j;
}

} // namespace

JointPose joint_pose(const Part &a, const Feature &fa, const Part &b, const Feature &fb) {
	using Kind = Feature::Kind;
	if (fa.kind == Kind::Hole && fb.kind == Kind::Tenon) {
		return tenon_into_hole(a, fa, b, fb);
	}
	if (fa.kind == Kind::Kerf && fb.kind == Kind::Taper) {
		return wedge_into_kerf(a, fa, b, fb);
	}
	JointPose j;
	if (fa.kind == Kind::Tenon && fb.kind == Kind::Hole) {
		j = tenon_into_hole(b, fb, a, fa);
	} else if (fa.kind == Kind::Taper && fb.kind == Kind::Kerf) {
		j = wedge_into_kerf(b, fb, a, fa);
	} else {
		return j;
	}
	j.a_moves = true;
	return j;
}

const Feature *feature_named(const Part &p, const std::string &name) {
	for (const Feature &f : p.features) {
		if (f.name == name) {
			return &f;
		}
	}
	return nullptr;
}

} // namespace sdf::plans
