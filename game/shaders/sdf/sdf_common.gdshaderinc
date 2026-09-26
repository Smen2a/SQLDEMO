// Shared SDF code: compiled as C++ (native/src/core, via glsl_compat.h) and included by
// Godot shaders (game/shaders/sdf). The copies under game/shaders/sdf must stay
// byte-identical to native/src/core/shared; run tools/sync_shaders.sh after editing.
//
// Subset rules: no swizzles, no user-function overloading, no arrays, no structs,
// float literals always carry a decimal point, out-params go through OUT()/INOUT().

const float SDF_PI = 3.14159265358979;
const float SDF_SQRT_HALF = 0.707106781186548;
const float SDF_BIG = 1.0e9;

SDF_FN vec3 sdf_xyz(vec4 v) {
	return vec3(v.x, v.y, v.z);
}

// Rotates v by the inverse of unit quaternion q (xyz = vector part, w = scalar), i.e.
// maps a body-space offset into a primitive's local frame.
SDF_FN vec3 sdf_rotate_inv(vec3 v, vec4 q) {
	vec3 u = vec3(-q.x, -q.y, -q.z);
	vec3 t = 2.0 * cross(u, v);
	return v + q.w * t + cross(u, t);
}

// Integer hashing keeps procedural noise bit-identical between CPU and GPU.
// lowbias32 (Chris Wellons).
SDF_FN uint sdf_hash_u(uint x) {
	x ^= x >> 16u;
	x *= 2146121005u;
	x ^= x >> 15u;
	x *= 2221713035u;
	x ^= x >> 16u;
	return x;
}

SDF_FN float sdf_hash3(int x, int y, int z) {
	uint h = sdf_hash_u(uint(x) * 73856093u ^ sdf_hash_u(uint(y) * 19349663u ^ sdf_hash_u(uint(z) * 83492791u)));
	return float(h) * (1.0 / 4294967295.0);
}

// Trilinear value noise in [0, 1].
SDF_FN float sdf_value_noise(vec3 p) {
	vec3 i = floor(p);
	vec3 f = p - i;
	vec3 u = f * f * (3.0 - 2.0 * f);
	int x = int(i.x);
	int y = int(i.y);
	int z = int(i.z);
	float a = mix(sdf_hash3(x, y, z), sdf_hash3(x + 1, y, z), u.x);
	float b = mix(sdf_hash3(x, y + 1, z), sdf_hash3(x + 1, y + 1, z), u.x);
	float c = mix(sdf_hash3(x, y, z + 1), sdf_hash3(x + 1, y, z + 1), u.x);
	float d = mix(sdf_hash3(x, y + 1, z + 1), sdf_hash3(x + 1, y + 1, z + 1), u.x);
	return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}

// Three octaves of value noise, centred on zero, roughly in [-0.5, 0.5].
SDF_FN float sdf_fbm(vec3 p) {
	float n = sdf_value_noise(p) * 0.5714;
	n += sdf_value_noise(p * 2.03 + vec3(17.1, 5.3, 9.7)) * 0.2857;
	n += sdf_value_noise(p * 4.07 + vec3(3.9, 31.7, 12.1)) * 0.1429;
	return n - 0.5;
}
