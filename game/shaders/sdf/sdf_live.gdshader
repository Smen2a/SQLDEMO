// Live SdfBody's shadow caster: an internal SHADOWS_ONLY child runs this opaque variant in
// shadow passes when SdfBody.live_shadows is on. It must never draw the visible surface:
// Forward+ redraws opaque materials after the depth prepass with an exact-equality depth
// test (scene_shader_forward_clustered.cpp), and a raymarched depth recomputed by a
// separately compiled variant misses it on some pixels, which shows as speckle. The
// visible surface uses sdf_live_single.gdshader. See sdf_live.gdshaderinc for the raymarch.
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

#define SDF_SHADOW_CASTER
#include "res://shaders/sdf/sdf_live.gdshaderinc"
