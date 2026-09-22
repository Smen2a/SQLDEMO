// Live display of an SdfBody: the proxy box's back faces start a ray per pixel, which is
// sphere-traced through the body's octree in body-local millimetres. Each octree leaf
// carries a pruned tape of edits, interpreted here with the same shared functions the C++
// evaluator uses, so what is drawn is the body itself, not an approximation of it.
//
// Mirrors native/src/core/eval/reference_renderer.cpp (octree path): empty cells are
// skipped whole, steps are clamped to the cell exit and divided by the leaf's Lipschitz
// bound, hits are refined with a few signed steps, normals are tetrahedral.
//
// Data textures (RGBA32F, 1024 texels wide, written by native/src/godot/sdf_body.cpp):
//   sdf_nodes      2 texels per node: (first child or -1, tape offset, tape length, state)
//                                      (base primitive in play, Lipschitz bound, -, -)
//   sdf_tape       4 entries per texel: +(edit + 1), or -(edit + 1) to reset first
//   sdf_edits      7 texels per edit:  (prim, op, blend, profile) (r, r2, material, -) p0..p4
//   sdf_materials  4 texels per material: k0, k1, k2, (specular, shininess, -, -)
//
// Shadows: casting works everywhere, since the shadow pass runs this shader and takes the
// raymarched DEPTH. Receiving needs the shadow lookup at LIGHT_VERTEX, which Forward+ and
// Mobile do per fragment; the Compatibility renderer computes shadow coordinates in the
// vertex stage (drivers/gles3/shaders/scene.glsl), i.e. on the proxy box, which darkens
// whole faces, so there Live bodies do not receive shadow-map shadows.
shader_type spatial;
render_mode cull_front, depth_draw_opaque;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
render_mode shadows_disabled;
#endif

#include "res://shaders/sdf/sdf_prelude.gdshaderinc"
#include "res://shaders/sdf/sdf_common.gdshaderinc"
#include "res://shaders/sdf/sdf_primitives.gdshaderinc"
#include "res://shaders/sdf/sdf_blend.gdshaderinc"
#include "res://shaders/sdf/sdf_material.gdshaderinc"

const int SDF_TEX_WIDTH = 1024;
const int SDF_STATE_EMPTY = 1;
const int SDF_VIEW_SHADED = 0;
const int SDF_VIEW_NORMALS = 1;
const int SDF_VIEW_STEPS = 2;
const int SDF_VIEW_ALBEDO = 3;

uniform sampler2D sdf_nodes : filter_nearest, repeat_disable;
uniform sampler2D sdf_tape : filter_nearest, repeat_disable;
uniform sampler2D sdf_edits : filter_nearest, repeat_disable;
uniform sampler2D sdf_materials : filter_nearest, repeat_disable;

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

// Leaf containing p (which must lie in the root cube): node index, and the cell's lower
// corner and size through lo / size.
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

// The leaf's tape at p: vec4(distance, material state).
vec4 sdf_eval_leaf(vec3 p, int node) {
	vec4 n0 = sdf_texel(sdf_nodes, node * 2);
	vec4 n1 = sdf_texel(sdf_nodes, node * 2 + 1);
	float d = SDF_BIG;
	if (n1.x > 0.5) {
		d = sdf_primitive(p, sdf_base_type, sdf_base_p0, sdf_base_p1, sdf_base_p2, sdf_base_p3, sdf_base_p4);
	}
	vec3 mat = vec3(sdf_base_material, sdf_base_material, 0.0);
	int offset = int(n0.y);
	int count = int(n0.z);
	for (int k = 0; k < count; k++) {
		int entry = offset + k;
		float code = sdf_component(sdf_texel(sdf_tape, entry / 4), entry % 4);
		int e = (int(abs(code)) - 1) * 7;
		vec4 head = sdf_texel(sdf_edits, e);
		vec4 radii = sdf_texel(sdf_edits, e + 1);
		int op = int(head.y);
		if (code < 0.0) {
			d = op == SDF_OP_UNION ? SDF_BIG : -SDF_BIG;
		}
		float pd = sdf_primitive(p, int(head.x), sdf_texel(sdf_edits, e + 2), sdf_texel(sdf_edits, e + 3),
				sdf_texel(sdf_edits, e + 4), sdf_texel(sdf_edits, e + 5), sdf_texel(sdf_edits, e + 6));
		vec4 r = sdf_apply_edit(d, mat, pd, op, int(head.z), radii.x, radii.y, int(head.w), radii.z);
		d = r.x;
		mat = vec3(r.y, r.z, r.w);
	}
	return vec4(d, mat);
}

bool sdf_in_root(vec3 p) {
	vec3 rel = p - sdf_root_lo;
	return min(rel.x, min(rel.y, rel.z)) >= 0.0 && max(rel.x, max(rel.y, rel.z)) <= sdf_root_size;
}

// The body's field at p, as Octree::sample.
vec4 sdf_sample(vec3 p) {
	if (!sdf_in_root(p)) {
		vec3 outside = max(max(sdf_root_lo - p, p - (sdf_root_lo + vec3(sdf_root_size))), vec3(0.0));
		return vec4(max(length(outside), 1.0e-3), sdf_base_material, sdf_base_material, 0.0);
	}
	vec3 lo;
	float size;
	return sdf_eval_leaf(p, sdf_find_leaf(p, lo, size));
}

float sdf_distance(vec3 p) {
	return sdf_sample(p).x;
}

vec3 sdf_normal(vec3 p, float h) {
	vec3 k0 = vec3(1.0, -1.0, -1.0);
	vec3 k1 = vec3(-1.0, -1.0, 1.0);
	vec3 k2 = vec3(-1.0, 1.0, -1.0);
	vec3 k3 = vec3(1.0, 1.0, 1.0);
	return normalize(k0 * sdf_distance(p + k0 * h) + k1 * sdf_distance(p + k1 * h) + k2 * sdf_distance(p + k2 * h) +
			k3 * sdf_distance(p + k3 * h));
}

float sdf_occlusion(vec3 p, vec3 n) {
	float occ = 0.0;
	float weight = 1.0;
	for (int i = 0; i < 5; i++) {
		float h = i == 0 ? 0.3 : (i == 1 ? 0.9 : (i == 2 ? 1.8 : (i == 3 ? 3.0 : 4.5)));
		occ += (h - sdf_distance(p + n * h)) * weight;
		weight *= 0.8;
	}
	return clamp(1.0 - 0.25 * occ, 0.0, 1.0);
}

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
	float d = SDF_BIG;
	float eps = 1.0e-4;
	bool hit = false;
	int steps = 0;
	for (int i = 0; i < sdf_max_steps && t <= span.y; i++) {
		steps = i;
		vec3 p = ro + rd * t;
		eps = max(ortho ? pixel * v_scale * 0.5 : t * pixel * 0.5, 1.0e-4);
		if (!sdf_in_root(p)) {
			break;
		}
		vec3 lo;
		float size;
		int node = sdf_find_leaf(p, lo, size);
		float exit = sdf_cell_exit(p, rd, lo, size);
		vec4 n0 = sdf_texel(sdf_nodes, node * 2);
		if (int(n0.w) == SDF_STATE_EMPTY) {
			t += exit + 1.0e-4;
			continue;
		}
		d = sdf_eval_leaf(p, node).x;
		if (d < eps) {
			hit = true;
			break;
		}
		float lipschitz = sdf_texel(sdf_nodes, node * 2 + 1).y;
		t += min(max(d / lipschitz, eps * 0.5), exit + 1.0e-4);
	}

	if (sdf_debug_view == SDF_VIEW_STEPS) {
		ALBEDO = vec3(0.0);
		ROUGHNESS = 1.0;
		SPECULAR = 0.0;
		EMISSION = sdf_heat(float(steps) / float(sdf_max_steps));
		if (!hit) {
			// Still write a depth so the heat map covers misses too.
			d = 0.0;
		}
	} else if (!hit) {
		discard;
	}

	// Settle onto the surface so shading does not depend on the step sequence.
	for (int i = 0; i < 4 && hit && abs(d) > 1.0e-5; i++) {
		t += d;
		d = sdf_distance(ro + rd * t);
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

	if (sdf_debug_view != SDF_VIEW_STEPS) {
		vec3 n = sdf_normal(p, max(eps, 2.0e-3));
		NORMAL = normalize((VIEW_MATRIX * (MODEL_MATRIX * vec4(n, 0.0))).xyz);
		vec4 s = sdf_sample(p);
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
				AO = sdf_occlusion(p, n);
			}
		}
	}
}
