#pragma once

#include "glsl_compat.h"

namespace sdf {

// Where one space lies in another: its axes and its origin there (a rotation and a shift;
// axes kept orthonormal). apply() takes a point of the inner space to the outer.
struct Pose {
	vec3 x{1.0f, 0.0f, 0.0f}, y{0.0f, 1.0f, 0.0f}, z{0.0f, 0.0f, 1.0f}, origin{0.0f};

	vec3 apply(vec3 p) const { return origin + x * p.x + y * p.y + z * p.z; }
	vec3 turn(vec3 v) const { return x * v.x + y * v.y + z * v.z; }
	// The outer space back into the inner.
	Pose inverse() const {
		Pose r;
		r.x = vec3(x.x, y.x, z.x);
		r.y = vec3(x.y, y.y, z.y);
		r.z = vec3(x.z, y.z, z.z);
		r.origin = r.turn(origin) * -1.0f;
		return r;
	}
	// This pose, then `outer` (the inner space into outer's outer space).
	Pose then(const Pose &outer) const {
		Pose r;
		r.x = outer.turn(x);
		r.y = outer.turn(y);
		r.z = outer.turn(z);
		r.origin = outer.apply(origin);
		return r;
	}
	// Moved along `v` (in the outer space).
	Pose shifted(vec3 v) const {
		Pose r = *this;
		r.origin = origin + v;
		return r;
	}
};

} // namespace sdf
