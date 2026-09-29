#pragma once

// The shared SDF formulas, compiled as C++. The same files are included by the Godot
// shaders, which is what keeps the CPU evaluator and the GPU raymarcher identical.

#include "glsl_compat.h"

namespace sdf::gl {
#include "shared/sdf_common.glsl"
#include "shared/sdf_primitives.glsl"
#include "shared/sdf_blend.glsl"
#include "shared/sdf_material.glsl"
} // namespace sdf::gl
