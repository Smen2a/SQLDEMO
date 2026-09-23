// Live SdfBody, opaque: Godot runs it in the depth prepass and again in the colour pass,
// and in shadow passes when the body casts shadows (SdfBody.live_shadows). See
// sdf_live.gdshaderinc for the raymarch itself.
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

#include "res://shaders/sdf/sdf_live.gdshaderinc"
