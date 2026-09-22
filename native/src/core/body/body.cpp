#include "body/body.h"

#include <algorithm>
#include <cmath>

namespace sdf {

namespace {

vec3 rotate(vec3 v, vec4 q) {
	return gl::sdf_rotate_inv(v, vec4(-q.x, -q.y, -q.z, q.w));
}

// AABB of a box with the given half extents after rotation by q.
vec3 rotated_extent(vec3 half, vec4 q) {
	const vec3 ax = rotate(vec3(half.x, 0, 0), q);
	const vec3 ay = rotate(vec3(0, half.y, 0), q);
	const vec3 az = rotate(vec3(0, 0, half.z), q);
	return gl::abs(ax) + gl::abs(ay) + gl::abs(az);
}

// Once max(a, b) reaches this radius the blend's zero set matches the hard result, so an
// edit only reshapes the surface within this distance of its primitive.
//   Smooth:  the offset is at most r/4 (quadratic) or r/6 (cubic), and only where
//            |a - b| < r, so both operands are below r + r/4.
//   Round:   hg_sdf's round equals min(a, b) unless both a and b are below r.
//   Chamfer: the bevel plane undercuts min(a, b) only while max(a, b) < K with
//            K = r r2 / (r + r2 - sqrt(r^2 + r2^2)), i.e. 1.707 r when symmetric.
//   Profile: capped by the distance to the (r, r2) rectangle, which reaches min(a, b)
//            once max(a, b) >= r + r2 + sqrt(2 r r2).
float blend_support(Blend mode, float r, float r2) {
	switch (mode) {
		case Blend::Hard:
			return 0.0f;
		case Blend::Smooth:
		case Blend::SmoothC2:
			return 1.25f * r;
		case Blend::Round:
			return r;
		case Blend::Chamfer: {
			const float n = std::sqrt(r * r + r2 * r2);
			return r * r2 / std::max(r + r2 - n, 1e-12f);
		}
		case Blend::Profile:
			return r + r2 + std::sqrt(2.0f * r * r2);
	}
	return 0.0f;
}

float blend_lipschitz(Blend mode) {
	switch (mode) {
		case Blend::Hard:
		case Blend::Smooth:
		case Blend::SmoothC2:
			return 1.0f;
		case Blend::Chamfer:
		case Blend::Round:
		case Blend::Profile:
			return std::sqrt(2.0f);
	}
	return 1.0f;
}

} // namespace

void Aabb::include(vec3 p) {
	lo = gl::min(lo, p);
	hi = gl::max(hi, p);
}

void Aabb::include(const Aabb &o) {
	if (o.empty()) {
		return;
	}
	lo = gl::min(lo, o.lo);
	hi = gl::max(hi, o.hi);
}

Aabb Aabb::expanded(float r) const {
	if (empty()) {
		return *this;
	}
	return {lo - vec3(r), hi + vec3(r)};
}

bool Aabb::overlaps(const Aabb &o) const {
	return lo.x <= o.hi.x && hi.x >= o.lo.x && lo.y <= o.hi.y && hi.y >= o.lo.y && lo.z <= o.hi.z &&
			hi.z >= o.lo.z;
}

ToolProfile ToolProfile::flat(float width, float height) {
	return {gl::SDF_TOOL_FLAT, vec4(width, height, 0, 0)};
}

ToolProfile ToolProfile::v_tool(float included_angle_deg, float height) {
	return {gl::SDF_TOOL_V, vec4(gl::radians(included_angle_deg * 0.5f), height, 0, 0)};
}

ToolProfile ToolProfile::gouge(float sweep_radius, float width, float height) {
	return {gl::SDF_TOOL_GOUGE, vec4(sweep_radius, width, height, 0)};
}

float ToolProfile::extent() const {
	switch (kind) {
		case gl::SDF_TOOL_V:
			return params.y / std::cos(params.x);
		case gl::SDF_TOOL_GOUGE: {
			const float half_width = std::min(params.y * 0.5f, params.x);
			return std::sqrt(half_width * half_width + params.z * params.z);
		}
		default:
			return std::sqrt(0.25f * params.x * params.x + params.y * params.y);
	}
}

Primitive Primitive::sphere(vec3 centre, float radius) {
	Primitive p;
	p.type = Prim::Sphere;
	p.p[0] = vec4(centre, radius);
	return p;
}

Primitive Primitive::box(vec3 centre, vec3 half_extents, float rounding, vec4 rotation) {
	Primitive p;
	p.type = Prim::Box;
	p.p[0] = vec4(centre, rounding);
	p.p[1] = rotation;
	p.p[2] = vec4(half_extents, 0);
	return p;
}

Primitive Primitive::cylinder(vec3 centre, float radius, float half_height, float rounding, vec4 rotation) {
	Primitive p;
	p.type = Prim::Cylinder;
	p.p[0] = vec4(centre, rounding);
	p.p[1] = rotation;
	p.p[2] = vec4(radius, half_height, 0, 0);
	return p;
}

Primitive Primitive::capsule(vec3 a, vec3 b, float radius) {
	Primitive p;
	p.type = Prim::Capsule;
	p.p[0] = vec4(a, radius);
	p.p[1] = vec4(b, 0);
	return p;
}

Primitive Primitive::plane(vec3 normal, float offset) {
	Primitive p;
	p.type = Prim::Plane;
	p.p[0] = vec4(gl::normalize(normal), offset);
	return p;
}

Primitive Primitive::sweep(vec3 a, vec3 b, vec3 up, const ToolProfile &tool) {
	Primitive p;
	p.type = Prim::SweepSegment;
	p.p[0] = vec4(a, 0);
	p.p[1] = vec4(b, 0);
	p.p[3] = vec4(gl::normalize(up), float(tool.kind));
	p.p[4] = tool.params;
	return p;
}

Primitive Primitive::sweep(vec3 a, vec3 control, vec3 b, vec3 up, const ToolProfile &tool) {
	Primitive p;
	p.type = Prim::SweepBezier;
	p.p[0] = vec4(a, 0);
	p.p[1] = vec4(control, 0);
	p.p[2] = vec4(b, 0);
	p.p[3] = vec4(gl::normalize(up), float(tool.kind));
	p.p[4] = tool.params;
	return p;
}

static ToolProfile tool_of(const Primitive &p) {
	return {int(p.p[3].w), p.p[4]};
}

Aabb Primitive::bounds() const {
	Aabb b;
	switch (type) {
		case Prim::Sphere:
			b.include(gl::sdf_xyz(p[0]) - vec3(p[0].w));
			b.include(gl::sdf_xyz(p[0]) + vec3(p[0].w));
			break;
		case Prim::Box: {
			const vec3 e = rotated_extent(gl::sdf_xyz(p[2]), p[1]);
			b.include(gl::sdf_xyz(p[0]) - e);
			b.include(gl::sdf_xyz(p[0]) + e);
			break;
		}
		case Prim::Cylinder: {
			const vec3 e = rotated_extent(vec3(p[2].x, p[2].y, p[2].x), p[1]);
			b.include(gl::sdf_xyz(p[0]) - e);
			b.include(gl::sdf_xyz(p[0]) + e);
			break;
		}
		case Prim::Capsule:
			b.include(gl::sdf_xyz(p[0]));
			b.include(gl::sdf_xyz(p[1]));
			b = b.expanded(p[0].w);
			break;
		case Prim::Plane:
			return Aabb::infinite();
		case Prim::SweepSegment:
			b.include(gl::sdf_xyz(p[0]));
			b.include(gl::sdf_xyz(p[1]));
			b = b.expanded(tool_of(*this).extent());
			break;
		case Prim::SweepBezier:
			b.include(gl::sdf_xyz(p[0]));
			b.include(gl::sdf_xyz(p[1]));
			b.include(gl::sdf_xyz(p[2]));
			b = b.expanded(tool_of(*this).extent());
			break;
	}
	return b;
}

float Primitive::lipschitz() const {
	if (type != Prim::SweepBezier) {
		return 1.0f;
	}
	// For a quadratic Bezier, B' x B'' is constant, so curvature peaks where |B'| is least.
	const vec3 a = gl::sdf_xyz(p[1]) - gl::sdf_xyz(p[0]);
	const vec3 b = gl::sdf_xyz(p[0]) - 2.0f * gl::sdf_xyz(p[1]) + gl::sdf_xyz(p[2]);
	const float bb = gl::dot(b, b);
	if (bb < 1e-10f) {
		return 1.0f;
	}
	const float t = gl::clamp(-gl::dot(a, b) / bb, 0.0f, 1.0f);
	const float speed = gl::length(a + b * t);
	const float curvature = gl::length(gl::cross(a, b)) / (2.0f * speed * speed * speed);
	const float stretch = curvature * tool_of(*this).extent();
	return stretch < 0.9f ? 1.0f / (1.0f - stretch) : 10.0f;
}

float Edit::influence() const {
	switch (op) {
		case Op::Union:
		case Op::Subtract:
		case Op::Intersect:
			return blend_support(blend, r, r2);
		case Op::Engrave:
		case Op::Paint:
			return r;
		case Op::Groove:
			return r2;
		case Op::Tongue:
			return std::max(r, r2);
	}
	return 0.0f;
}

Aabb Edit::bounds() const {
	if (op == Op::Intersect) {
		return Aabb::infinite();
	}
	return prim.bounds().expanded(influence());
}

float Edit::lipschitz() const {
	float op_l = 1.0f;
	switch (op) {
		case Op::Union:
		case Op::Subtract:
		case Op::Intersect:
			op_l = blend_lipschitz(blend);
			break;
		case Op::Engrave:
			op_l = std::sqrt(2.0f);
			break;
		default:
			break;
	}
	return op_l * prim.lipschitz();
}

void Body::add(const Edit &e) {
	edits_.push_back(e);
	culls_.push_back({e.op == Op::Intersect ? Aabb::infinite() : e.prim.bounds(), e.influence()});
}

void Body::replace(std::size_t i, const Edit &e) {
	edits_[i] = e;
	culls_[i] = {e.op == Op::Intersect ? Aabb::infinite() : e.prim.bounds(), e.influence()};
}

void Body::pop() {
	edits_.pop_back();
	culls_.pop_back();
}

namespace {

float box_distance(const Aabb &b, vec3 p) {
	const vec3 outside = gl::max(gl::max(b.lo - p, p - b.hi), 0.0f);
	return gl::length(outside);
}

vec4 apply(const Edit &e, float d, vec3 mat, vec3 p) {
	return gl::sdf_apply_edit(d, mat, e.prim.eval(p), int(e.op), int(e.blend), e.r, e.r2, int(e.shape),
			float(e.material));
}

} // namespace

Sample Body::sample(vec3 p) const {
	float d = base.eval(p);
	vec3 mat(float(base_material), float(base_material), 0.0f);
	for (std::size_t i = 0; i < edits_.size(); ++i) {
		if (box_distance(culls_[i].box, p) >= std::fabs(d) + culls_[i].influence) {
			continue;
		}
		const vec4 r = apply(edits_[i], d, mat, p);
		d = r.x;
		mat = vec3(r.y, r.z, r.w);
	}
	return {d, mat.x, mat.y, mat.z};
}

Sample Body::sample_exhaustive(vec3 p) const {
	float d = base.eval(p);
	vec3 mat(float(base_material), float(base_material), 0.0f);
	for (const Edit &e : edits_) {
		const vec4 r = apply(e, d, mat, p);
		d = r.x;
		mat = vec3(r.y, r.z, r.w);
	}
	return {d, mat.x, mat.y, mat.z};
}

vec3 Body::normal(vec3 p, float h) const {
	const vec3 k0(1, -1, -1), k1(-1, -1, 1), k2(-1, 1, -1), k3(1, 1, 1);
	const vec3 n = k0 * distance(p + k0 * h) + k1 * distance(p + k1 * h) + k2 * distance(p + k2 * h) +
			k3 * distance(p + k3 * h);
	return gl::normalize(n);
}

Aabb Body::bounds() const {
	Aabb b = base.bounds();
	for (const Edit &e : edits_) {
		if (e.op == Op::Union || e.op == Op::Tongue) {
			b.include(e.bounds());
		}
	}
	return b;
}

float Body::lipschitz() const {
	float l = base.lipschitz();
	for (const Edit &e : edits_) {
		l = std::max(l, e.lipschitz());
	}
	return l;
}

vec4 quat_axis_angle(vec3 axis, float angle) {
	const vec3 a = gl::normalize(axis) * std::sin(angle * 0.5f);
	return vec4(a, std::cos(angle * 0.5f));
}

} // namespace sdf
