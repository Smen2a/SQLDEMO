// Live display of an SdfBody: the proxy box's back faces start a ray per pixel, which is
// sphere-traced in body-local millimetres. Included by the wrappers
//   sdf_adf_single.gdshader   the visible surface, from the ADF (default)
//   sdf_adf.gdshader          its shadow caster
//   sdf_live_single.gdshader  the visible surface, from the octree tapes alone (exact)
//   sdf_live.gdshader         its shadow caster
// The ADF wrappers define SDF_ADF.
//
// Two field backends, one march:
// - Tapes (native/src/core/compile/octree.h): each octree leaf carries a pruned tape of
//   edits, interpreted with the same shared functions the C++ evaluator uses. Exact, but
//   every evaluation costs the tape's length in texel fetches.
// - ADF (native/src/core/adf/adf.h): the octree continues into small bricks of sampled
//   distances along the surface, read with hardware filtering; cells at creases stay
//   exact and point at their tape. Costs a lookup, whatever the edit count.
// Both mirror native/src/core/eval/reference_renderer.cpp (octree / ADF paths): empty cells
// are skipped whole, steps are clamped to the cell exit and divided by the cell's
// Lipschitz bound, hits are refined with a few signed steps, normals are tetrahedral. The
// ray keeps the cell it is in and only descends again when it leaves; the refine, normal
// and AO taps reuse the hit's cell the same way.
//
// Data textures (1024 texels wide, written by native/src/godot/sdf_body.cpp):
//   sdf_nodes      RGBA32F, 2 texels per octree node: (first child or -1, tape offset,
//                  tape length, flags) with flags = (state + 1) + 3 * (base primitive in
//                  play), then (Lipschitz bound, -, -, -)
//   sdf_tape       RGBA32F, 4 entries per texel: +(edit + 1), or -(edit + 1) to reset first
//   sdf_edits      RGBA32F, 6 texels per edit: (prim | op << 3 | blend << 6 | profile << 9,
//                  r, r2, material), p0..p4
//   sdf_materials  RGBA32F, 4 texels per material: k0, k1, k2, (specular, shininess, -, -)
//   sdf_adf_nodes  RGBA32F, 1 texel per ADF node: (first child or -1, brick slot or
//                  -1 empty / -2 solid, material id or -(material brick + 1) or 256 +
//                  exact cell, Lipschitz bound or (empty, solid) centre value)
//   sdf_adf_cells  RGBA32F, 1 texel per exact cell, in sdf_nodes' format: its own tape,
//                  the octree cell's pruned to it, stored after the octree's in sdf_tape
//   sdf_bricks     R16F array, 1024^2 layers of 2048 bricks: a brick is a 64x8 tile of
//                  its 8 z-slices of 8x8 samples side by side
//   sdf_brick_materials  RGBA8 array, the same layout: (material 0, material 1, mix, -)

#include "res://shaders/sdf/sdf_prelude.gdshaderinc"
#include "res://shaders/sdf/sdf_common.gdshaderinc"
#include "res://shaders/sdf/sdf_primitives.gdshaderinc"
#include "res://shaders/sdf/sdf_blend.gdshaderinc"
#include "res://shaders/sdf/sdf_material.gdshaderinc"

const int SDF_TEX_WIDTH = 1024;
const int SDF_EDIT_TEXELS = 6;
const int SDF_STATE_EMPTY = 1;
const int SDF_STATE_SOLID = -1;
const int SDF_ADF_EMPTY = -1;
const int SDF_ADF_SOLID = -2;
const int SDF_ADF_EXACT_BASE = 256;
const int SDF_BRICKS_PER_LAYER = 2048;
const float SDF_BRICK_CELLS = 7.0;
const int SDF_VIEW_SHADED = 0;
const int SDF_VIEW_NORMALS = 1;
const int SDF_VIEW_STEPS = 2;
const int SDF_VIEW_ALBEDO = 3;

uniform sampler2D sdf_nodes : filter_nearest, repeat_disable;
uniform sampler2D sdf_tape : filter_nearest, repeat_disable;
uniform sampler2D sdf_edits : filter_nearest, repeat_disable;
uniform sampler2D sdf_materials : filter_nearest, repeat_disable;
#ifdef SDF_ADF
uniform sampler2D sdf_adf_nodes : filter_nearest, repeat_disable;
uniform sampler2D sdf_adf_cells : filter_nearest, repeat_disable;
uniform sampler2DArray sdf_bricks : filter_linear, repeat_disable;
uniform sampler2DArray sdf_brick_materials : filter_nearest, repeat_disable;
#endif

uniform vec3 sdf_root_lo;
uniform float sdf_root_size = 1.0;
uniform vec3 sdf_box_lo;
uniform vec3 sdf_box_hi;
uniform int sdf_base_type = 0;
uniform vec4 sdf_base_p0;
uniform vec4 sdf_base_p1;
uniform vec4 sdf_base_p2;
uniform vec4 sdf_base_p3;
uniform vec4 sdf_base_p4;
uniform float sdf_base_material = 0.0;
uniform vec3 sdf_grain_origin;
uniform vec3 sdf_grain_axis = vec3(1.0, 0.0, 0.0);

uniform int sdf_debug_view = 0;
uniform int sdf_max_steps = 512;
uniform bool sdf_ambient_occlusion = true;

varying vec3 v_local;
varying flat vec3 v_eye;     // camera position in body space (perspective)
varying flat vec3 v_forward; // view direction in body space (orthographic)
varying flat float v_scale;  // body units per view unit, inverted

vec4 sdf_texel(sampler2D tex, int i) {
	return texelFetch(tex, ivec2(i % SDF_TEX_WIDTH, i / SDF_TEX_WIDTH), 0);
}

float sdf_component(vec4 v, int i) {
	if (i == 0) {
		return v.x;
	}
	if (i == 1) {
		return v.y;
	}
	if (i == 2) {
		return v.z;
	}
	return v.w;
}

bool sdf_in_cell(vec3 p, vec3 lo, float size) {
	vec3 rel = p - lo;
	return min(rel.x, min(rel.y, rel.z)) >= 0.0 && max(rel.x, max(rel.y, rel.z)) <= size;
}

// --- tapes ------------------------------------------------------------------------------

// Octree leaf containing p (which must lie in the root cube): node index, and the cell's
// lower corner and size through lo / size.
int sdf_find_leaf(vec3 p, out vec3 lo, out float size) {
	lo = sdf_root_lo;
	size = sdf_root_size;
	int node = 0;
	for (int depth = 0; depth < 24; depth++) {
		int child = int(sdf_texel(sdf_nodes, node * 2).x);
		if (child < 0) {
			break;
		}
		size *= 0.5;
		vec3 upper = step(lo + vec3(size), p);
		lo += upper * size;
		node = child + int(upper.x) + int(upper.y) * 2 + int(upper.z) * 4;
	}
	return node;
}

// A leaf's tape at p, given the leaf's first node texel: vec4(distance, material state).
vec4 sdf_eval_tape(vec3 p, vec4 n0) {
	float d = SDF_BIG;
	if (int(n0.w) >= 3) {
		d = sdf_primitive(p, sdf_base_type, sdf_base_p0, sdf_base_p1, sdf_base_p2, sdf_base_p3, sdf_base_p4);
	}
	vec3 mat = vec3(sdf_base_material, sdf_base_material, 0.0);
	int offset = int(n0.y);
	int count = int(n0.z);
	vec4 codes = vec4(0.0);
	for (int k = 0; k < count; k++) {
		int entry = offset + k;
		if (k == 0 || entry % 4 == 0) {
			codes = sdf_texel(sdf_tape, entry / 4);
		}
		float code = sdf_component(codes, entry % 4);
		int e = (int(abs(code)) - 1) * SDF_EDIT_TEXELS;
		vec4 head = sdf_texel(sdf_edits, e);
		int packed = int(head.x);
		int op = (packed >> 3) & 7;
		if (code < 0.0) {
			d = op == SDF_OP_UNION ? SDF_BIG : -SDF_BIG;
		}
		float pd = sdf_primitive(p, packed & 7, sdf_texel(sdf_edits, e + 1), sdf_texel(sdf_edits, e + 2),
				sdf_texel(sdf_edits, e + 3), sdf_texel(sdf_edits, e + 4), sdf_texel(sdf_edits, e + 5));
		vec4 r = sdf_apply_edit(d, mat, pd, op, (packed >> 6) & 7, head.y, head.z, (packed >> 9) & 3, head.w);
		d = r.x;
		mat = vec3(r.y, r.z, r.w);
	}
	return vec4(d, mat);
}

// --- ADF --------------------------------------------------------------------------------
#ifdef SDF_ADF

int sdf_adf_find_leaf(vec3 p, out vec3 lo, out float size) {
	lo = sdf_root_lo;
	size = sdf_root_size;
	int node = 0;
	for (int depth = 0; depth < 32; depth++) {
		int child = int(sdf_texel(sdf_adf_nodes, node).x);
		if (child < 0) {
			break;
		}
		size *= 0.5;
		vec3 upper = step(lo + vec3(size), p);
		lo += upper * size;
		node = child + int(upper.x) + int(upper.y) * 2 + int(upper.z) * 4;
	}
	return node;
}

// Top-left texel of a brick's tile, and its layer.
ivec3 sdf_brick_origin(int slot) {
	int tile = slot % SDF_BRICKS_PER_LAYER;
	return ivec3((tile % 16) * 64, (tile / 16) * 8, slot / SDF_BRICKS_PER_LAYER);
}

// Trilinear lookup at voxel coordinates u in [0, 7]^3: hardware bilinear within the two
// neighbouring z slices, then a lerp between them (as Adf's brick_value on the CPU).
float sdf_brick_value(int slot, vec3 u) {
	u = clamp(u, vec3(0.0), vec3(SDF_BRICK_CELLS));
	float z = min(floor(u.z), SDF_BRICK_CELLS - 1.0);
	ivec3 o = sdf_brick_origin(slot);
	vec2 uv = (vec2(float(o.x) + z * 8.0 + u.x + 0.5, float(o.y) + u.y + 0.5)) / float(SDF_TEX_WIDTH);
	float a = textureLod(sdf_bricks, vec3(uv, float(o.z)), 0.0).r;
	float b = textureLod(sdf_bricks, vec3(uv + vec2(8.0 / float(SDF_TEX_WIDTH), 0.0), float(o.z)), 0.0).r;
	return mix(a, b, u.z - z);
}

bool sdf_adf_exact(vec4 a) {
	return int(a.y) >= 0 && int(a.z) >= SDF_ADF_EXACT_BASE;
}

// The trilinear brick value in a brick leaf (exact or not), the centre value elsewhere.
float sdf_adf_approx(vec3 p, vec3 lo, float size, vec4 a) {
	int kind = int(a.y);
	return kind >= 0 ? sdf_brick_value(kind, (p - lo) / size * SDF_BRICK_CELLS) : a.w;
}

// The field inside a known ADF leaf, given its node texel and, for exact leaves, their
// cell texel (n0).
vec4 sdf_adf_eval(vec3 p, vec3 lo, float size, vec4 a, vec4 n0) {
	int kind = int(a.y);
	if (sdf_adf_exact(a)) {
		return sdf_eval_tape(p, n0);
	}
	if (kind < 0) {
		return vec4(a.w, 0.0, 0.0, 0.0);
	}
	vec3 u = (p - lo) / size * SDF_BRICK_CELLS;
	float d = sdf_brick_value(kind, u);
	int m = int(a.z);
	if (m >= 0) {
		return vec4(d, float(m), float(m), 0.0);
	}
	ivec3 o = sdf_brick_origin(-m - 1);
	ivec3 v = ivec3(clamp(u + vec3(0.5), vec3(0.0), vec3(SDF_BRICK_CELLS)));
	vec4 t = texelFetch(sdf_brick_materials, ivec3(o.x + v.z * 8 + v.x, o.y + v.y, o.z), 0);
	return vec4(d, round(t.r * 255.0), round(t.g * 255.0), round(t.b * 255.0) / 255.0);
}

#endif

// --- the field, whichever backend -------------------------------------------------------

// Enters the cell containing p (which must lie in the root cube). n0 is the octree node
// texel whose tape applies (if any), a the ADF node texel (ADF only); state is
// SDF_STATE_EMPTY, SDF_STATE_SOLID or 0 (surface).
void sdf_enter_cell(vec3 p, out vec3 lo, out float size, out vec4 n0, out vec4 a, out float lipschitz, out int state) {
#ifdef SDF_ADF
	int node = sdf_adf_find_leaf(p, lo, size);
	a = sdf_texel(sdf_adf_nodes, node);
	int kind = int(a.y);
	state = kind == SDF_ADF_EMPTY ? SDF_STATE_EMPTY : (kind == SDF_ADF_SOLID ? SDF_STATE_SOLID : 0);
	lipschitz = a.w;
	n0 = sdf_adf_exact(a) ? sdf_texel(sdf_adf_cells, int(a.z) - SDF_ADF_EXACT_BASE) : vec4(0.0);
#else
	int node = sdf_find_leaf(p, lo, size);
	n0 = sdf_texel(sdf_nodes, node * 2);
	a = vec4(0.0);
	lipschitz = sdf_texel(sdf_nodes, node * 2 + 1).x;
	state = int(n0.w) % 3 - 1;
#endif
}

vec4 sdf_cell_eval(vec3 p, vec3 lo, float size, vec4 n0, vec4 a) {
#ifdef SDF_ADF
	return sdf_adf_eval(p, lo, size, a, n0);
#else
	return sdf_eval_tape(p, n0);
#endif
}

// The body's field at p (vec4(distance, material state)), as Octree::sample / Adf::sample.
vec4 sdf_sample(vec3 p) {
	if (!sdf_in_cell(p, sdf_root_lo, sdf_root_size)) {
		vec3 outside = max(max(sdf_root_lo - p, p - (sdf_root_lo + vec3(sdf_root_size))), vec3(0.0));
		return vec4(max(length(outside), 1.0e-3), sdf_base_material, sdf_base_material, 0.0);
	}
	vec3 lo;
	float size;
	vec4 n0;
	vec4 a;
	float lipschitz;
	int state;
	sdf_enter_cell(p, lo, size, n0, a, lipschitz, state);
	return sdf_cell_eval(p, lo, size, n0, a);
}

// The same, reusing a known cell when p is in it.
vec4 sdf_sample_near(vec3 p, vec3 lo, float size, vec4 n0, vec4 a) {
	if (sdf_in_cell(p, lo, size)) {
		return sdf_cell_eval(p, lo, size, n0, a);
	}
	return sdf_sample(p);
}

// For ambient occlusion: bricks only (the ADF), within about a voxel of the field.
float sdf_sample_approx(vec3 p, vec3 lo, float size, vec4 n0, vec4 a) {
#ifdef SDF_ADF
	if (!sdf_in_cell(p, lo, size)) {
		if (!sdf_in_cell(p, sdf_root_lo, sdf_root_size)) {
			return sdf_sample(p).x;
		}
		float lipschitz_unused;
		int state_unused;
		sdf_enter_cell(p, lo, size, n0, a, lipschitz_unused, state_unused);
	}
	return sdf_adf_approx(p, lo, size, a);
#else
	return sdf_sample_near(p, lo, size, n0, a).x;
#endif
}

// Tetrahedral normal step: half a voxel inside a brick (smoothing the trilinear field's
// gradient across voxel faces), otherwise as fine as precision allows.
float sdf_normal_step(vec3 p, float eps, vec3 lo, float size, vec4 a) {
#ifdef SDF_ADF
	if (!sdf_in_cell(p, lo, size)) {
		vec4 n0_unused;
		float lipschitz_unused;
		int state_unused;
		sdf_enter_cell(p, lo, size, n0_unused, a, lipschitz_unused, state_unused);
	}
	if (int(a.y) >= 0 && !sdf_adf_exact(a)) {
		return max(eps, 0.5 * size / SDF_BRICK_CELLS);
	}
#endif
	return max(eps, 2.0e-3);
}

vec3 sdf_normal(vec3 p, float h, vec3 lo, float size, vec4 n0, vec4 a) {
	vec3 k0 = vec3(1.0, -1.0, -1.0);
	vec3 k1 = vec3(-1.0, -1.0, 1.0);
	vec3 k2 = vec3(-1.0, 1.0, -1.0);
	vec3 k3 = vec3(1.0, 1.0, 1.0);
	return normalize(k0 * sdf_sample_near(p + k0 * h, lo, size, n0, a).x +
			k1 * sdf_sample_near(p + k1 * h, lo, size, n0, a).x + k2 * sdf_sample_near(p + k2 * h, lo, size, n0, a).x +
			k3 * sdf_sample_near(p + k3 * h, lo, size, n0, a).x);
}

float sdf_occlusion(vec3 p, vec3 n, vec3 lo, float size, vec4 n0, vec4 a) {
	float occ = 0.0;
	float weight = 1.0;
	for (int i = 0; i < 5; i++) {
		float h = i == 0 ? 0.3 : (i == 1 ? 0.9 : (i == 2 ? 1.8 : (i == 3 ? 3.0 : 4.5)));
		occ += (h - sdf_sample_approx(p + n * h, lo, size, n0, a)) * weight;
		weight *= 0.8;
	}
	return clamp(1.0 - 0.25 * occ, 0.0, 1.0);
}

// --- rays and shading --------------------------------------------------------------------

// 1 / rd, with axis-parallel components nudged off zero so slab tests stay finite.
vec3 sdf_inverse_dir(vec3 rd) {
	return vec3(1.0) / mix(rd, vec3(1.0e-12), lessThan(abs(rd), vec3(1.0e-12)));
}

// Slab test; returns (entry, exit) with entry > exit on a miss.
vec2 sdf_clip_box(vec3 ro, vec3 rd) {
	vec3 inv = sdf_inverse_dir(rd);
	vec3 ta = (sdf_box_lo - ro) * inv;
	vec3 tb = (sdf_box_hi - ro) * inv;
	vec3 tmin = min(ta, tb);
	vec3 tmax = max(ta, tb);
	return vec2(max(max(tmin.x, tmin.y), max(tmin.z, 0.0)), min(tmax.x, min(tmax.y, tmax.z)));
}

float sdf_cell_exit(vec3 p, vec3 rd, vec3 lo, float size) {
	vec3 inv = sdf_inverse_dir(rd);
	vec3 t = max((lo - p) * inv, (lo + vec3(size) - p) * inv);
	return max(min(t.x, min(t.y, t.z)), 0.0);
}

vec3 sdf_material_rgb(float id, vec3 p) {
	int m = int(id + 0.5) * 4;
	return sdf_material_albedo(p, sdf_texel(sdf_materials, m), sdf_texel(sdf_materials, m + 1),
			sdf_texel(sdf_materials, m + 2), vec4(sdf_grain_origin, 0.0), vec4(sdf_grain_axis, 0.0));
}

// Emission that comes out of a linear, sRGB-encoded framebuffer as exactly c.
vec3 sdf_srgb_to_linear(vec3 c) {
	return mix(c / 12.92, pow((c + vec3(0.055)) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 sdf_heat(float x) {
	return clamp(vec3(x * 3.0, x * 3.0 - 1.0, x * 3.0 - 2.0), vec3(0.0), vec3(1.0));
}

void vertex() {
	v_local = VERTEX;
	mat4 inv_model = inverse(MODEL_MATRIX);
	v_eye = (inv_model * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
	// The camera looks down its -z axis.
	v_forward = normalize((inv_model * vec4(-INV_VIEW_MATRIX[2].xyz, 0.0)).xyz);
	v_scale = 1.0 / length(MODEL_MATRIX[0].xyz);
}

void fragment() {
	bool ortho = PROJECTION_MATRIX[3][3] > 0.5;
	// Angular (perspective) or linear (orthographic, in view units) size of a pixel. The
	// Compatibility renderer flips y in the projection when drawing into a texture.
	float pixel = 2.0 / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);
	vec3 rd = ortho ? v_forward : normalize(v_local - v_eye);
	vec3 ro = ortho ? v_local - rd * dot(v_local - v_eye, rd) : v_eye;

	vec2 span = sdf_clip_box(ro, rd);
	float t = span.x;
	vec4 s = vec4(SDF_BIG, 0.0, 0.0, 0.0);
	float eps = 1.0e-4;
	bool hit = false;
	int steps = 0;
	// The cell the ray is in.
	vec3 lo = vec3(0.0);
	float size = -1.0;
	vec4 n0 = vec4(0.0);
	vec4 a = vec4(0.0);
	float lipschitz = 1.0;
	int state = 0;
	for (int i = 0; i < sdf_max_steps && t <= span.y; i++) {
		steps = i;
		vec3 p = ro + rd * t;
		eps = max(ortho ? pixel * v_scale * 0.5 : t * pixel * 0.5, 1.0e-4);
		if (!sdf_in_cell(p, lo, size)) {
			if (!sdf_in_cell(p, sdf_root_lo, sdf_root_size)) {
				break;
			}
			sdf_enter_cell(p, lo, size, n0, a, lipschitz, state);
		}
		float exit = sdf_cell_exit(p, rd, lo, size);
		if (state == SDF_STATE_EMPTY) {
			t += exit + 1.0e-4;
			continue;
		}
#ifdef SDF_ADF
		if (sdf_adf_exact(a)) {
			// March on the brick while it guarantees clearance (trilinear weights are convex
			// and every sample is within voxel * sqrt(3)); the tape decides hits.
			float margin = lipschitz * size / SDF_BRICK_CELLS * 1.7320508;
			float coarse = sdf_adf_approx(p, lo, size, a) - margin;
			if (coarse > eps) {
				t += min(max(coarse / lipschitz, eps * 0.5), exit + 1.0e-4);
				continue;
			}
		}
#endif
		s = state == SDF_STATE_SOLID ? vec4(-1.0, 0.0, 0.0, 0.0) : sdf_cell_eval(p, lo, size, n0, a);
		if (s.x < eps) {
			hit = true;
			break;
		}
		t += min(max(s.x / lipschitz, eps * 0.5), exit + 1.0e-4);
	}

	if (sdf_debug_view == SDF_VIEW_STEPS) {
		ALBEDO = vec3(0.0);
		ROUGHNESS = 1.0;
		SPECULAR = 0.0;
		EMISSION = sdf_heat(float(steps) / float(sdf_max_steps));
	} else if (!hit) {
		discard;
	}

	// Settle onto the surface so shading does not depend on the step sequence. s ends up
	// as the field at p, material included.
	for (int i = 0; i < 4 && hit && abs(s.x) > 1.0e-5; i++) {
		t += s.x;
		s = sdf_sample_near(ro + rd * t, lo, size, n0, a);
	}
	vec3 p = ro + rd * t;

	vec4 world = MODEL_MATRIX * vec4(p, 1.0);
	vec4 view = VIEW_MATRIX * world;
	vec4 clip = PROJECTION_MATRIX * view;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
	DEPTH = clip.z / clip.w * 0.5 + 0.5;
#else
	DEPTH = clip.z / clip.w;
#endif
	LIGHT_VERTEX = view.xyz;
#ifdef SDF_SINGLE_PASS
	ALPHA = 1.0;
#endif

	if (sdf_debug_view != SDF_VIEW_STEPS) {
		vec3 n = sdf_normal(p, sdf_normal_step(p, eps, lo, size, a), lo, size, n0, a);
		NORMAL = normalize((VIEW_MATRIX * (MODEL_MATRIX * vec4(n, 0.0))).xyz);
		vec3 albedo = sdf_material_rgb(s.y, p);
		vec4 props = sdf_texel(sdf_materials, int(s.y + 0.5) * 4 + 3);
		if (s.w > 0.0) {
			albedo = mix(albedo, sdf_material_rgb(s.z, p), s.w);
			props = mix(props, sdf_texel(sdf_materials, int(s.z + 0.5) * 4 + 3), s.w);
		}
		if (sdf_debug_view == SDF_VIEW_NORMALS || sdf_debug_view == SDF_VIEW_ALBEDO) {
			vec3 shown = sdf_debug_view == SDF_VIEW_NORMALS ? n * 0.5 + vec3(0.5) : albedo;
			ALBEDO = vec3(0.0);
			ROUGHNESS = 1.0;
			SPECULAR = 0.0;
			// The Compatibility renderer writes emission out as it is; the others store linear
			// light and encode sRGB at the end.
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
			EMISSION = shown;
#else
			EMISSION = sdf_srgb_to_linear(shown);
#endif
		} else {
			ALBEDO = albedo;
			// Blinn-Phong exponent to GGX roughness: alpha = sqrt(2 / (n + 2)), roughness = sqrt(alpha).
			ROUGHNESS = sqrt(sqrt(2.0 / (props.y + 2.0)));
			SPECULAR = clamp(props.x * 5.0, 0.0, 1.0);
			if (sdf_ambient_occlusion) {
				AO = sdf_occlusion(p, n, lo, size, n0, a);
			}
		}
	}
}
