#pragma once

#include "body/body.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sdf {

// A smoothing layer: part of a body's field re-sampled and reshaped (sanding), blended in
// over the field of everything before it. An Edit with Op::Layer applies one.
//
// It stores two sampled fields on a grid of spacing h (sample (i, j, k) at h * (i, j, k)):
//   phi  the new field (for sanding: the field before, after some curvature flow),
//   w    how much phi replaces the field before, 0..1.
// Both are reconstructed as quadratic B-splines (C1, so normals stay continuous), and the
// field after the layer is F' = (1 - w~) F + w~ phi~. Where w~ = 1 the surface is phi~'s
// alone: it has none of F's creases, which an offset F + delta would keep (inside a sharp
// arris F has a crease along the bisector, and no smooth delta cancels it). B-splines
// reproduce linear fields exactly, so flat faces re-sampled unchanged stay exactly where
// they were.
//
// Samples live in sparse blocks of 8^3, near the surface only; a missing sample has w = 0
// and phi = F. Whoever builds a layer keeps phi valid wherever w~ can be nonzero, and w
// ramping only where phi is close to F (see ramp()). A layer is immutable once built;
// layers built one after another share the blocks they have in common.
class Layer {
public:
	static constexpr int kBlock = 8;
	static constexpr int kBlockSamples = kBlock * kBlock * kBlock;

	struct Block {
		float phi[kBlockSamples];
		float w[kBlockSamples]; // x fastest, then y, then z
		float phi_lo = 0, phi_hi = 0, w_lo = 0, w_hi = 0;
		void finish(); // computes the bounds above
	};
	using Coord = std::array<int, 3>; // block coordinates: samples [8 c, 8 c + 8) on each axis

	// Bounds the builder measured (see apply()'s Lipschitz bound):
	struct Measures {
		float gradient = 1.0f;   // bound on |grad phi~| where w~ > 0
		float ramp = 0.0f;       // bound on |phi~ - F| |grad w~|
		float max_offset = 0.0f; // largest |phi - F| where w > 0
	};

	Layer(float spacing, const std::vector<std::pair<Coord, std::shared_ptr<const Block>>> &blocks,
			const Measures &measures);

	float spacing() const { return h_; }
	// The field after the layer at p, given the field before it there.
	float apply(vec3 p, float d) const;
	// What w~ and phi~ can be in the cube (lo, size): min and max of their samples in reach
	// (B-splines are weighted averages). A missing sample counts as w = 0, and as phi = F,
	// which the caller's own range covers.
	struct Range {
		float w_lo = 0, w_hi = 0, phi_lo = 0, phi_hi = 0;
	};
	Range range(vec3 lo, float size) const;
	// Where w~ can be nonzero.
	const Aabb &box() const { return box_; }
	// |grad F'| <= max(|grad F|, gradient()) + ramp().
	float gradient() const { return measures_.gradient; }
	float ramp() const { return measures_.ramp; }
	float max_offset() const { return measures_.max_offset; }
	std::size_t blocks() const { return blocks_.size(); }
	std::size_t bytes() const { return blocks_.size() * sizeof(Block); }

private:
	const Block *block(int bx, int by, int bz) const;

	float h_;
	Coord lo_{0, 0, 0}, dims_{0, 0, 0}; // dense index over the blocks' range
	std::vector<std::int32_t> index_;   // into blocks_, or -1
	std::vector<std::shared_ptr<const Block>> blocks_;
	Aabb box_;
	Measures measures_;
};

} // namespace sdf
