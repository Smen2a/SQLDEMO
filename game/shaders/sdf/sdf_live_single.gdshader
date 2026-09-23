// Live SdfBody's visible surface, in one pass: writing ALPHA moves the material to Godot's
// transparent pipeline, which skips the depth prepass (so the raymarch runs once per pixel
// instead of twice, and no exact-equality depth test can reject it) and never casts
// shadows (see sdf_live.gdshader for those). It still writes depth, so meshes and other bodies
// composite exactly. It does not feed the screen-space effects that read the prepass's
// normal buffer (SSAO, SSR, SSIL); the SDF ambient occlusion stands in for SSAO.
shader_type spatial;
render_mode cull_front, depth_draw_always;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
render_mode shadows_disabled;
#endif

#define SDF_SINGLE_PASS
#include "res://shaders/sdf/sdf_live.gdshaderinc"
