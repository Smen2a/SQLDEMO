#pragma once

#include "body/layer.h"
#include "compile/octree.h"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace sdf::tools {

// A smoothing layer under construction (see body/layer.h): hand sanding as curvature flow.
//
// It samples the field F of the body as it was (a snapshot) on a grid near the surface,
// in blocks allocated as the tool reaches new ground, and evolves phi (starting as F) by
// mean curvature flow wherever the tool pressed, removing material only:
//   d phi / dt = |grad phi| max(kappa, 0),   kappa = div(grad phi / |grad phi|).
// Convex parts (arrises, ridges left by tools) recede at a speed set by their curvature, so
// an arris rounds over with a radius growing like the square root of the sanding done,
// while flat faces and hollows stay put, as they do under a sheet of paper in the hand.
// The flow acts on every level set near the surface, not just the zero set: each one moves
// by its own curvature, which keeps phi a well-behaved field.
//
// Where the tool reached, w rises to 1 (the layer's phi~ replaces F), falling to 0 over the
// reach's outer edge and away from the surface, where phi is still F. The flow stays inside
// the region where w is 1.
class SmoothingGrid {
public:
	struct Params {
		float spacing = 0.25f; // h (mm)
		// The flow runs wherever phi < flow_outside, fading out over flow_outside_taper
		// beyond. Outside the surface, level sets move with the surface beneath them, so where
		// the flow fades they spread apart, which is harmless; inside, each moves by its own
		// curvature, at least the surface's, and they spread too. (Fading the flow out inside
		// would bunch them up instead, ever closer to the surface as it recedes.)
		float flow_outside = 0.6f, flow_outside_taper = 0.6f;
		float weight_band = 1.4f;  // w reaches 1 where |F| and |phi| < weight_band, 0 beyond
		float weight_taper = 0.6f; //   weight_band + weight_taper
		float reach_taper = 2.0f;  // w falls from 1 to 0 over the reach's outer reach_taper
		// Flow per explicit substep, in units of h^2: stable up to 1/4 for plain curvature
		// flow, less the 1.5x the speed outside the surface may be raised by.
		float max_step = 0.15f;
		// Gradient bounds (Layer::Measures) cover the field where it is above -measured_depth:
		// all a ray sees coming from outside, and the settling and normal taps just inside.
		// Deeper, only its sign matters (it stays negative).
		float measured_depth = 0.9f;
	};

	// Snapshots the body (copies of both), whose field the layer starts from.
	SmoothingGrid(const Body &body, const Octree &octree, const Params &params);

	// The tool pressed round `centre` (it reaches `reach` mm, pressing fully within
	// reach - reach_taper) for `amount` mm^2 of flow at full pressure. Recorded only.
	void press(vec3 centre, float reach, float amount);
	// Samples the ground the presses since the last call reached, raises w there and runs the
	// flow for them, spread over the hardware's threads. Returns the region whose field
	// changed (with the B-spline's reach), or an empty box.
	Aabb update();
	// The layer as it stands (sharing unchanged blocks with the last one).
	std::shared_ptr<const Layer> layer();

	const Params &params() const { return params_; }
	std::size_t blocks() const { return blocks_.size(); }
	struct Stats {
		// Blocks allocated since the update before; explicit substeps run and samples moved in
		// the last update.
		std::size_t allocated = 0, substeps = 0, flowed = 0;
		// The material the flow took out in the last update (mm^3; phi's zero set smeared over
		// a sample, so a small step counts), and its centre weighted by it (divide by removed).
		float removed = 0.0f;
		vec3 removed_centre{0.0f};
	};
	const Stats &last() const { return last_; }

private:
	static constexpr int kB = Layer::kBlock, kN = Layer::kBlockSamples;
	struct Work {
		Layer::Coord coord;
		float F[kN], phi[kN], w[kN], amount[kN];
		// Whether F is linear through the sample's neighbours (a face, not a crease): where
		// phi is still F there, w changes nothing (B-splines reproduce linear fields).
		bool flat[kN];
		bool pending = false; // has amount to flow
		bool dirty = true;    // changed since the last layer()
		std::shared_ptr<const Layer::Block> shared;
		// Measures for Layer::Measures, kept per block.
		float gradient = 0, ramp = 0, offset = 0, w_hi = 0;
	};
	struct Press {
		vec3 centre;
		float reach, amount;
	};
	static std::int64_t key(int x, int y, int z);
	Work *find(int x, int y, int z);
	const Work *find(int x, int y, int z) const { return const_cast<SmoothingGrid *>(this)->find(x, y, z); }
	// A new block's samples, or nullptr where the surface is further than `band` (by
	// `beyond`). Thread-safe.
	std::unique_ptr<Work> sample_block(int x, int y, int z, float band, float &beyond) const;
	void allocate_pressed(); // samples the blocks the presses since the last update reach
	void measure(Work &b);
	float field(vec3 p) const; // F, the snapshot's field
	vec3 sample_at(const Layer::Coord &c, int i) const;

	Body body_;
	Octree octree_;
	Params params_;
	float h_;
	std::unordered_map<std::int64_t, std::unique_ptr<Work>> blocks_;
	// Blocks checked and found away from the surface: by how far beyond the band they were.
	// The band grows with the sanding (the surface recedes), so they may be wanted later.
	std::unordered_map<std::int64_t, float> far_;
	float max_offset_ = 0.0f; // the most phi has moved anywhere
	std::vector<Press> presses_; // since the last update
	Aabb changed_;
	std::size_t allocated_ = 0;
	Stats last_;
};

} // namespace sdf::tools
