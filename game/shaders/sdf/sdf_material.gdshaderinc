// Shared procedural materials, evaluated in body-local millimetres so that cutting or
// breaking a part reveals the figure that was always inside it. Parameters come in five
// vec4s per material (from a table on the CPU, a texture on the GPU):
//   k0 = (kind, a, b, c)   k1 = colour A   k2 = colour B   k3 = (origin, -)   k4 = (axis, -)

const int SDF_MAT_WOOD = 0;  // k0 = (kind, ring spacing mm, figure strength, fibre contrast); origin = pith point, axis = log axis
const int SDF_MAT_METAL = 1; // k0 = (kind, variation, -, -)
const int SDF_MAT_STONE = 2; // k0 = (kind, vein scale mm, vein strength, -); B = vein colour
const int SDF_MAT_PLAIN = 3; // flat colour A

SDF_FN vec3 sdf_material_albedo(vec3 p, vec4 k0, vec4 k1, vec4 k2, vec4 k3, vec4 k4) {
	int kind = int(k0.x);
	vec3 col_a = sdf_xyz(k1);
	vec3 col_b = sdf_xyz(k2);
	if (kind == SDF_MAT_WOOD) {
		vec3 axis = sdf_xyz(k4);
		vec3 q = p - sdf_xyz(k3);
		float along = dot(q, axis);
		vec3 radial_v = q - axis * along;
		float radial = length(radial_v);
		// Rings wander with low-frequency noise, stretched along the grain.
		vec3 stretched = (q - axis * along * 0.85) * 0.08;
		float wobble = sdf_fbm(stretched) * k0.z * k0.y;
		float ring = fract((radial + wobble) / k0.y);
		float late = smoothstep(0.55, 0.85, ring) * (1.0 - smoothstep(0.93, 1.0, ring));
		// Fine fibres: high-frequency noise, long along the axis, short across it.
		vec3 fibre_p = (q - axis * along * 0.97) * 2.2;
		float fibre = sdf_value_noise(fibre_p) - 0.5;
		vec3 col = mix(col_a, col_b, late);
		return col * (1.0 + fibre * k0.w);
	}
	if (kind == SDF_MAT_METAL) {
		float n = sdf_fbm(p * 0.35);
		return col_a * (1.0 + n * k0.y);
	}
	if (kind == SDF_MAT_STONE) {
		float n = sdf_fbm(p / k0.y);
		float vein = 1.0 - smoothstep(0.0, 0.06, abs(sdf_fbm(p / (k0.y * 2.5) + vec3(n * 1.5))));
		return mix(col_a * (0.9 + n * 0.3), col_b, vein * k0.z);
	}
	return col_a;
}
