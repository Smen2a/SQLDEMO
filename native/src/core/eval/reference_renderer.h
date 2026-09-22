#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "eval/image.h"

namespace sdf {

struct Camera {
	vec3 eye{0, -100, 80};
	vec3 target{0, 0, 0};
	vec3 up{0, 0, 1};
	float fov_deg = 35.0f;
};

struct RenderSettings {
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
// path is compared against.
Image render(const Body &body, const MaterialTable &materials, const Camera &camera, const RenderSettings &settings);

} // namespace sdf
