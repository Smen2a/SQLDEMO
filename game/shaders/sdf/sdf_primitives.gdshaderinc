// Shared SDF primitives, tool cross-sections and swept tool strokes. Distances are in
// millimetres in body-local space. Every function here is exact or a Lipschitz-1 bound
// unless its comment says otherwise.

const int SDF_PRIM_SPHERE = 0;        // p0 = (centre, radius)
const int SDF_PRIM_BOX = 1;           // p0 = (centre, rounding), p1 = rotation quat, p2 = (half extents, -)
const int SDF_PRIM_CYLINDER = 2;      // p0 = (centre, rounding), p1 = rotation quat, p2 = (radius, half height, -, -); axis = local y
const int SDF_PRIM_CAPSULE = 3;       // p0 = (a, radius), p1 = (b, -)
const int SDF_PRIM_PLANE = 4;         // p0 = (unit normal, offset); solid where dot(p, n) < offset
const int SDF_PRIM_SWEEP_SEGMENT = 5; // p0 = (a, -), p1 = (b, -), p3 = (up, tool kind), p4 = tool params
const int SDF_PRIM_SWEEP_BEZIER = 6;  // p0, p1, p2 = quadratic control points, p3 = (up, tool kind), p4 = tool params

// Tool cross-sections, in the stroke's (lateral, up) plane with the deepest point of the
// cutting edge at the origin. The region is what the tool removes.
const int SDF_TOOL_FLAT = 0;  // straight chisel: params = (width, height, -, -)
const int SDF_TOOL_V = 1;     // V-tool: params = (half angle in radians, height, -, -)
const int SDF_TOOL_GOUGE = 2; // gouge: params = (sweep radius, width, height, -)

SDF_FN float sdf_box2(vec2 p, vec2 b) {
	vec2 d = abs(p) - b;
	return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

SDF_FN float sdf_box(vec3 p, vec3 b, float rounding) {
	vec3 q = abs(p) - b + vec3(rounding);
	return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - rounding;
}

SDF_FN float sdf_cylinder(vec3 p, float radius, float half_height, float rounding) {
	vec2 d = vec2(length(vec2(p.x, p.z)) - radius + rounding, abs(p.y) - half_height + rounding);
	return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rounding;
}

SDF_FN float sdf_capsule(vec3 p, vec3 a, vec3 b, float radius) {
	vec3 pa = p - a;
	vec3 ba = b - a;
	float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
	return length(pa - ba * h) - radius;
}

SDF_FN float sdf_tool_profile(vec2 q, int kind, vec4 prm) {
	if (kind == SDF_TOOL_V) {
		vec2 qa = vec2(abs(q.x), q.y);
		vec2 wall_dir = vec2(sin(prm.x), cos(prm.x));
		vec2 wall_normal = vec2(cos(prm.x), -sin(prm.x));
		float wedge = dot(qa, wall_dir) < 0.0 ? length(qa) : dot(qa, wall_normal);
		return max(wedge, q.y - prm.y);
	}
	if (kind == SDF_TOOL_GOUGE) {
		float radius = prm.x;
		float half_width = min(prm.y * 0.5, radius);
		float disk = length(q - vec2(0.0, radius)) - radius;
		float channel = min(disk, radius - q.y);
		return max(max(channel, abs(q.x) - half_width), q.y - prm.z);
	}
	return sdf_box2(q - vec2(0.0, prm.y * 0.5), vec2(prm.x * 0.5, prm.y * 0.5));
}

// Exact extrusion of a 2D distance along an axis, given the signed axial offset past the
// nearer end (negative inside the extent).
SDF_FN float sdf_extrude(float d2, float axial) {
	vec2 w = vec2(d2, axial);
	return min(max(w.x, w.y), 0.0) + length(max(w, 0.0));
}

// The cross-section lies in the plane perpendicular to the path, oriented by `up`
// (the tool's approach direction), so a stroke that dives into the wood cuts the channel
// a real gouge pushed along its own axis would.
SDF_FN float sdf_sweep_segment(vec3 p, vec3 a, vec3 b, vec3 up, int kind, vec4 prm) {
	vec3 ab = b - a;
	float len = length(ab);
	vec3 t = ab / len;
	vec3 u = normalize(up - t * dot(up, t));
	vec3 s = cross(t, u);
	vec3 pa = p - a;
	vec2 q = vec2(dot(pa, s), dot(pa, u));
	float along = dot(pa, t);
	return sdf_extrude(sdf_tool_profile(q, kind, prm), abs(along - 0.5 * len) - 0.5 * len);
}

// Parameter of the closest point on a quadratic Bezier (Inigo Quilez's cubic solve).
SDF_FN float sdf_bezier_closest_t(vec3 p, vec3 A, vec3 B, vec3 C) {
	vec3 a = B - A;
	vec3 b = A - 2.0 * B + C;
	vec3 c = a * 2.0;
	vec3 d = A - p;
	float bb = dot(b, b);
	if (bb < 1.0e-10) {
		vec3 ac = C - A;
		return clamp(dot(p - A, ac) / dot(ac, ac), 0.0, 1.0);
	}
	float kk = 1.0 / bb;
	float kx = kk * dot(a, b);
	float ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
	float kz = kk * dot(d, a);
	float pp = ky - kx * kx;
	float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
	float h = q * q + 4.0 * pp * pp * pp;
	if (h >= 0.0) {
		h = sqrt(h);
		float x0 = (h - q) * 0.5;
		float x1 = (-h - q) * 0.5;
		float uv = sign(x0) * pow(abs(x0), 1.0 / 3.0) + sign(x1) * pow(abs(x1), 1.0 / 3.0);
		return clamp(uv - kx, 0.0, 1.0);
	}
	float z = sqrt(-pp);
	float v = acos(q / (pp * z * 2.0)) / 3.0;
	float m = cos(v);
	float n = sin(v) * 1.732050808;
	float t0 = clamp((m + m) * z - kx, 0.0, 1.0);
	float t1 = clamp((-n - m) * z - kx, 0.0, 1.0);
	vec3 e0 = d + (c + b * t0) * t0;
	vec3 e1 = d + (c + b * t1) * t1;
	return dot(e0, e0) < dot(e1, e1) ? t0 : t1;
}

// Stroke along a quadratic Bezier. Exact near the path while the curvature radius exceeds
// the profile's extent; inside the stroke the axial term uses the chord, which
// underestimates arc length and so stays a conservative bound.
SDF_FN float sdf_sweep_bezier(vec3 p, vec3 A, vec3 B, vec3 C, vec3 up, int kind, vec4 prm) {
	float t = sdf_bezier_closest_t(p, A, B, C);
	vec3 c = mix(mix(A, B, t), mix(B, C, t), t);
	vec3 tg = normalize((1.0 - t) * (B - A) + t * (C - B));
	vec3 u = normalize(up - tg * dot(up, tg));
	vec3 s = cross(tg, u);
	vec3 pc = p - c;
	vec2 q = vec2(dot(pc, s), dot(pc, u));
	float along = dot(pc, tg);
	float axial = -min(t, 1.0 - t) * length(C - A);
	if (t <= 0.0) {
		axial = -along;
	} else if (t >= 1.0) {
		axial = along;
	}
	return sdf_extrude(sdf_tool_profile(q, kind, prm), axial);
}

SDF_FN float sdf_primitive(vec3 p, int type, vec4 p0, vec4 p1, vec4 p2, vec4 p3, vec4 p4) {
	if (type == SDF_PRIM_SPHERE) {
		return length(p - sdf_xyz(p0)) - p0.w;
	}
	if (type == SDF_PRIM_BOX) {
		return sdf_box(sdf_rotate_inv(p - sdf_xyz(p0), p1), sdf_xyz(p2), p0.w);
	}
	if (type == SDF_PRIM_CYLINDER) {
		return sdf_cylinder(sdf_rotate_inv(p - sdf_xyz(p0), p1), p2.x, p2.y, p0.w);
	}
	if (type == SDF_PRIM_CAPSULE) {
		return sdf_capsule(p, sdf_xyz(p0), sdf_xyz(p1), p0.w);
	}
	if (type == SDF_PRIM_PLANE) {
		return dot(p, sdf_xyz(p0)) - p0.w;
	}
	if (type == SDF_PRIM_SWEEP_SEGMENT) {
		return sdf_sweep_segment(p, sdf_xyz(p0), sdf_xyz(p1), sdf_xyz(p3), int(p3.w), p4);
	}
	return sdf_sweep_bezier(p, sdf_xyz(p0), sdf_xyz(p1), sdf_xyz(p2), sdf_xyz(p3), int(p3.w), p4);
}
