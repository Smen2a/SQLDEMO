// Shared blend modes and edit operators. The primitive decides the surface; the blend
// mode decides the edge where two surfaces meet.
//
// Every binary blend returns vec2(distance, w), where w is how much the result belongs to
// the second operand (for material mixing). Hard-edged modes return w of exactly 0 or 1;
// smooth modes return a continuous weight. All modes have compact support: they equal
// the hard result once the operands are further apart than their radius, so an edit only
// influences the region within that radius of its primitive.

const int SDF_BLEND_HARD = 0;      // crisp arris
const int SDF_BLEND_CHAMFER = 1;   // flat bevel reaching r onto a and r2 onto b (asymmetric when they differ)
const int SDF_BLEND_ROUND = 2;     // circular fillet of radius r, unchanged outside it
const int SDF_BLEND_SMOOTH = 3;    // quadratic polynomial smooth min, support r (C1)
const int SDF_BLEND_SMOOTH_C2 = 4; // cubic polynomial smooth min, support r (C2)
const int SDF_BLEND_PROFILE = 5;   // edge profile curve reaching r onto a and r2 onto b

// Edge profiles, described for a union's concave corner. Applied through subtract or
// intersect they shape a convex edge instead: ARC_CONCAVE rounds it over, ARC_CONVEX cuts
// a cove into it, OGEE gives the S-curve. The ogee is directional: it leaves the second
// operand's surface tangentially as a cove and meets the first operand's surface convex,
// so swapping the operands mirrors it.
const int SDF_PROFILE_ARC_CONCAVE = 0;
const int SDF_PROFILE_ARC_CONVEX = 1;
const int SDF_PROFILE_OGEE = 2;

const int SDF_OP_UNION = 0;
const int SDF_OP_SUBTRACT = 1;
const int SDF_OP_INTERSECT = 2;
const int SDF_OP_ENGRAVE = 3;  // V-groove of depth r where the guide's surface crosses the body
const int SDF_OP_GROOVE = 4;   // square groove, depth r, half-width r2, along the guide
const int SDF_OP_TONGUE = 5;   // square tongue, height r, half-width r2, along the guide
const int SDF_OP_PAINT = 6;    // material only: inside the guide, over a transition of width r
const int SDF_OP_LAYER = 7;    // a sampled smoothing layer (C++ only: Layer::apply); the identity here

const float SDF_MIN_RADIUS = 1.0e-6;

// Fill region of an edge profile, as a distance in unit corner coordinates (u, v); the
// profile curve runs from (1, 0) to (0, 1). The region deliberately extends to [-1, 1]^2,
// overlapping the operands' own solids: regions that merely touch along a face leave a
// zero-valued phantom sheet on that face under min(), which renders as a surface that is
// not there (the old face, floating in a cut).
SDF_FN float sdf_profile_fill(vec2 uv, int shape) {
	float box = sdf_box2(uv, vec2(1.0, 1.0));
	if (shape == SDF_PROFILE_ARC_CONVEX) {
		return max(box, length(uv) - 1.0);
	}
	if (shape == SDF_PROFILE_OGEE) {
		// Region under v = 1 - smoothstep(u); |slope| <= 1.5 so dividing by
		// sqrt(1 + 1.5^2) keeps the implicit form a distance bound.
		float u = clamp(uv.x, 0.0, 1.0);
		float curve = uv.y - (1.0 - u * u * (3.0 - 2.0 * u));
		return max(box, curve * 0.5547002);
	}
	return max(box, 1.0 - length(uv - vec2(1.0, 1.0)));
}

SDF_FN vec2 sdf_union(float a, float b, int mode, float r, float r2, int shape) {
	float hard_w = b < a ? 1.0 : 0.0;
	if (mode == SDF_BLEND_SMOOTH || mode == SDF_BLEND_SMOOTH_C2) {
		if (r < SDF_MIN_RADIUS) {
			return vec2(min(a, b), hard_w);
		}
		float h = max(r - abs(a - b), 0.0) / r;
		float m = h * h * 0.5;
		float s = m * r * 0.5;
		if (mode == SDF_BLEND_SMOOTH_C2) {
			m = h * h * h * 0.5;
			s = m * r * (1.0 / 3.0);
		}
		return a < b ? vec2(a - s, m) : vec2(b - s, 1.0 - m);
	}
	if (mode == SDF_BLEND_ROUND) {
		if (r < SDF_MIN_RADIUS) {
			return vec2(min(a, b), hard_w);
		}
		vec2 u = max(vec2(r - a, r - b), 0.0);
		return vec2(max(r, min(a, b)) - length(u), hard_w);
	}
	if (mode == SDF_BLEND_CHAMFER) {
		if (min(r, r2) < SDF_MIN_RADIUS) {
			return vec2(min(a, b), hard_w);
		}
		float bevel = (a * r2 + b * r - r * r2) / sqrt(r * r + r2 * r2);
		return vec2(min(min(a, b), bevel), hard_w);
	}
	if (mode == SDF_BLEND_PROFILE) {
		if (min(r, r2) < SDF_MIN_RADIUS) {
			return vec2(min(a, b), hard_w);
		}
		// Scaling the unit profile by (r, r2) keeps its zero set exact but makes it an
		// underestimate that would reach arbitrarily far along some directions; the true
		// distance to the profile's [-r, r] x [-r2, r2] box caps that, which bounds the
		// support.
		float fill = min(r, r2) * sdf_profile_fill(vec2(a / r, b / r2), shape);
		float rect = sdf_box2(vec2(a, b), vec2(r, r2));
		return vec2(min(min(a, b), max(fill, rect)), hard_w);
	}
	return vec2(min(a, b), hard_w);
}

SDF_FN vec2 sdf_intersect(float a, float b, int mode, float r, float r2, int shape) {
	vec2 u = sdf_union(-a, -b, mode, r, r2, shape);
	return vec2(-u.x, u.y);
}

SDF_FN vec2 sdf_subtract(float a, float b, int mode, float r, float r2, int shape) {
	return sdf_intersect(a, -b, mode, r, r2, shape);
}

// Material state is vec3(id0, id1, t): a mix of (1 - t) of id0 and t of id1. Mixing in
// material m at weight w keeps the two heaviest of the three candidates.
SDF_FN vec3 sdf_mat_mix(vec3 st, float m, float w) {
	float w0 = (1.0 - st.z) * (1.0 - w);
	float w1 = st.z * (1.0 - w);
	float wm = w;
	if (m == st.x) {
		w0 += wm;
		wm = 0.0;
	} else if (m == st.y) {
		w1 += wm;
		wm = 0.0;
	}
	float id0 = st.x;
	float id1 = st.y;
	if (id0 == id1) {
		w0 += w1;
		w1 = 0.0;
	}
	if (wm > min(w0, w1)) {
		if (w0 < w1) {
			id0 = m;
			w0 = wm;
		} else {
			id1 = m;
			w1 = wm;
		}
	}
	float total = w0 + w1;
	return vec3(id0, id1, total > 0.0 ? w1 / total : 0.0);
}

// Applies one edit to the running state. d is the body so far, e the edit primitive's
// distance, mat the material state. Returns vec4(distance, material state).
SDF_FN vec4 sdf_apply_edit(float d, vec3 mat, float e, int op, int mode, float r, float r2, int shape, float material) {
	if (op == SDF_OP_UNION) {
		vec2 u = sdf_union(d, e, mode, r, r2, shape);
		return vec4(u.x, sdf_mat_mix(mat, material, u.y));
	}
	if (op == SDF_OP_SUBTRACT) {
		return vec4(sdf_subtract(d, e, mode, r, r2, shape).x, mat);
	}
	if (op == SDF_OP_INTERSECT) {
		return vec4(sdf_intersect(d, e, mode, r, r2, shape).x, mat);
	}
	if (op == SDF_OP_ENGRAVE) {
		return vec4(max(d, (d + r - abs(e)) * SDF_SQRT_HALF), mat);
	}
	if (op == SDF_OP_GROOVE) {
		return vec4(max(d, min(d + r, r2 - abs(e))), mat);
	}
	if (op == SDF_OP_TONGUE) {
		return vec4(min(d, max(d - r, abs(e) - r2)), mat);
	}
	if (op == SDF_OP_LAYER) {
		return vec4(d, mat);
	}
	float w = e < 0.0 ? 1.0 : 0.0;
	if (r > SDF_MIN_RADIUS) {
		w = clamp(0.5 - e / r, 0.0, 1.0);
	}
	return vec4(d, sdf_mat_mix(mat, material, w));
}
