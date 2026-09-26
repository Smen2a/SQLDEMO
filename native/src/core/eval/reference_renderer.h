#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "adf/adf.h"
#include "compile/octree.h"
#include "eval/image.h"

namespace sdf {

struct Camera {
	vec3 eye{0, -100, 80};
	vec3 target{0, 0, 0};
	vec3 up{0, 0, 1};
	float fov_deg = 35.0f;
};

struct RenderSettings {
	enum class Output {
		Shaded,
		Normals, // body-space normal as n * 0.5 + 0.5, stored without gamma; misses are black
		Albedo,  // unlit material colour, stored without gamma; misses are black
	};
	Output output = Output::Shaded;
	int width = 640;
	int height = 400;
	int samples_per_axis = 1; // supersampling: n x n jittered samples per pixel
	int max_steps = 256;
	// Direction towards the key light, in body space.
	vec3 light_dir = gl::normalize(vec3(-0.55f, -0.40f, 0.73f));
	vec3 light_colour{1.00f, 0.95f, 0.86f};
	vec3 sky_colour{0.42f, 0.47f, 0.55f};
	bool shadows = true;
	bool ambient_occlusion = true;
	int threads = 0; // 0 = hardware concurrency
};

// Ground-truth renderer: sphere-traces the body's field on the CPU with Lipschitz-scaled
// steps and a pixel-footprint hit epsilon, shades with the shared material functions,
// SDF soft shadows and SDF ambient occlusion. Slow, simple, and the reference the GPU
// path is compared against. With an octree, it traces the pruned per-cell tapes instead,
// skipping empty cells and clamping every step at the cell's exit. Those tapes are exact
// only within the octree's value_margin of the surface, so ambient occlusion — which
// samples further out — is approximate on that path (still from valid distance bounds).
// With an ADF as well (built from that octree), it traces the ADF exactly as the Live
// shader does: trilinear bricks, tapes in exact cells, normals half a voxel wide in bricks.
Image render(const Body &body, const MaterialTable &materials, const Camera &camera, const RenderSettings &settings,
		const Octree *octree = nullptr, const Adf *adf = nullptr);

} // namespace sdf
