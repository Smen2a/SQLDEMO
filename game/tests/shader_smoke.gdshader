// Compiles every shared SDF function through Godot's shader pipeline. If this renders
// without a SHADER ERROR, the shared files are valid Godot shading language as well as C++.
shader_type spatial;
render_mode unshaded;

#include "res://shaders/sdf/sdf_prelude.gdshaderinc"
#include "res://shaders/sdf/sdf_common.gdshaderinc"
#include "res://shaders/sdf/sdf_primitives.gdshaderinc"
#include "res://shaders/sdf/sdf_blend.gdshaderinc"
#include "res://shaders/sdf/sdf_material.gdshaderinc"

void fragment() {
	vec3 p = vec3(UV.x, UV.y, 0.5) * 20.0 - 10.0;
	vec4 q = vec4(0.0, 0.0, 0.0, 1.0);
	vec4 tool = vec4(0.5235988, 5.0, 0.0, 0.0);

	float d = sdf_primitive(p, SDF_PRIM_SPHERE, vec4(0.0, 0.0, 0.0, 4.0), q, q, q, q);
	float e = sdf_primitive(p, SDF_PRIM_BOX, vec4(1.0, 0.0, 0.0, 0.2), q, vec4(3.0, 2.0, 1.0, 0.0), q, q);
	e = min(e, sdf_primitive(p, SDF_PRIM_CYLINDER, vec4(0.0), q, vec4(1.0, 3.0, 0.0, 0.0), q, q));
	e = min(e, sdf_primitive(p, SDF_PRIM_CAPSULE, vec4(0.0, 0.0, 0.0, 1.0), vec4(3.0, 3.0, 0.0, 0.0), q, q, q));
	e = min(e, sdf_primitive(p, SDF_PRIM_PLANE, vec4(0.0, 0.0, 1.0, 0.0), q, q, q, q));
	e = min(e, sdf_primitive(p, SDF_PRIM_SWEEP_SEGMENT, vec4(-5.0, 0.0, 0.0, 0.0), vec4(5.0, 0.0, 0.0, 0.0), q,
			vec4(0.0, 0.0, 1.0, float(SDF_TOOL_V)), tool));
	e = min(e, sdf_primitive(p, SDF_PRIM_SWEEP_BEZIER, vec4(-5.0, 0.0, 0.0, 0.0), vec4(0.0, 3.0, 0.0, 0.0),
			vec4(5.0, 0.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, float(SDF_TOOL_GOUGE)), vec4(3.0, 4.0, 5.0, 0.0)));

	vec3 mat = vec3(0.0);
	vec4 st = vec4(d, mat);
	for (int op = 0; op <= SDF_OP_PAINT; op++) {
		for (int mode = 0; mode <= SDF_BLEND_PROFILE; mode++) {
			st = sdf_apply_edit(st.x, vec3(st.y, st.z, st.w), e, op, mode, 1.0, 0.5, SDF_PROFILE_OGEE, 2.0);
		}
	}

	vec4 wood = vec4(float(SDF_MAT_WOOD), 4.0, 0.5, 0.3);
	vec3 albedo = sdf_material_albedo(p, wood, vec4(0.8, 0.6, 0.4, 0.0), vec4(0.5, 0.3, 0.2, 0.0),
			vec4(0.0), vec4(0.0, 1.0, 0.0, 0.0));
	albedo += sdf_material_albedo(p, vec4(float(SDF_MAT_STONE), 5.0, 0.8, 0.0), vec4(0.7), vec4(0.2), vec4(0.0), vec4(0.0));
	albedo += sdf_material_albedo(p, vec4(float(SDF_MAT_METAL), 0.2, 0.0, 0.0), vec4(0.6), vec4(0.0), vec4(0.0), vec4(0.0));
	ALBEDO = albedo * 0.33 * (0.5 + 0.5 * cos(st.x)) + vec3(st.w * 0.1);
}
