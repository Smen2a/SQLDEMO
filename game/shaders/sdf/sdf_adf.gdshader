// Shadow caster for a Live SdfBody displayed from its ADF: runs in shadow passes only (an
// internal SHADOWS_ONLY child); see sdf_live.gdshader for why it never draws the surface.
shader_type spatial;
render_mode cull_front, depth_draw_opaque;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
render_mode shadows_disabled;
#endif

#define SDF_ADF
#define SDF_SHADOW_CASTER
#include "res://shaders/sdf/sdf_live.gdshaderinc"
