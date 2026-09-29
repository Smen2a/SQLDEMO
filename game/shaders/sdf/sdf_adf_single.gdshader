// Live SdfBody's visible surface from its ADF (the default), in one pass; see
// sdf_live_single.gdshader for why one pass, and sdf_live.gdshaderinc for the raymarch.
shader_type spatial;
render_mode cull_front, depth_draw_always;
#if CURRENT_RENDERER == RENDERER_COMPATIBILITY
render_mode shadows_disabled;
#endif

#define SDF_ADF
#define SDF_SINGLE_PASS
#include "res://shaders/sdf/sdf_live.gdshaderinc"
