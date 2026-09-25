#pragma once

#include "adf/adf.h"
#include "body/body.h"
#include "body/region.h"
#include "compile/octree.h"

#include <memory>

#include <cstddef>
#include <cstdint>
#include <vector>

// The parts a body's material lies in: which of its ADF's inside samples (and solid leaves)
// hold together. Cuts meeting can leave an island that no single plane separates (a
// corner cut off by two saw cuts, a thin bridge chiselled through); this finds it.
namespace sdf {

// Inside samples next to each other in a brick, and across the faces between leaves, are
// joined when the field between them is provably negative: |a| + |b| > L h (Lipschitz), or
// the brick's reconstruction is further inside than its error bound all along the segment
// between them; otherwise the exact field sampled along the segment decides, down to
// `finest` (a kerf thinner than a voxel shows there). A bridge too thin for the samples to
// see is not seen: parts found here are candidates, which cutting one out confirms
// (see region.h).
struct Parts {
	struct Part {
		double volume = 0.0; // mm^3: solid leaves and the inside voxels of bricks
		vec3 centre{0.0f};   // centre of volume
		Aabb bounds;         // of its solid leaves and inside voxels
		vec3 inside{0.0f};   // a sample in its material
		// Whether it reaches the edge of the region looked at (and so may go on beyond it).
		bool touches_region = false;
	};
	std::vector<Part> parts; // largest first
	std::size_t exact_checks = 0; // joins the exact field decided
	double ms = 0.0;

	// The part a solid leaf (by ADF node) or an inside sample (by brick slot and sample) is
	// in, or -1: anything else, and anything outside the region looked at.
	int of_node(int node) const {
		return node >= 0 && std::size_t(node) < node_part.size() ? node_part[std::size_t(node)] : -1;
	}
	int of_sample(int slot, int sample) const {
		if (slot < 0 || std::size_t(slot) >= slot_group.size() || slot_group[std::size_t(slot)] < 0) {
			return -1;
		}
		const std::uint16_t g = group[std::size_t(slot) * Adf::kBrickSamples + std::size_t(sample)];
		return g == kOutside ? -1 : group_part[std::size_t(slot_group[std::size_t(slot)] + g)];
	}

	// Each brick's inside samples in groups (joined within the brick): per slot, its first
	// group's number (-1: not looked at); per sample, its group within the brick; per group,
	// its part.
	static constexpr std::uint16_t kOutside = 0xFFFF;
	std::vector<std::int32_t> node_part, slot_group, group_part;
	std::vector<std::uint16_t> group;
	// How many parts hold at least `least` mm^3.
	int count(double least = 0.0) const;
};

// The parts of the material in the ADF's leaves overlapping `region` (all of them without
// one). With a region, what they tell is whether the material near a cut is still in one
// piece *within* it: if it is, and the body was in one piece before the cut, it still is
// (any path round the cut can be rerouted inside the region), so a cut only needs the whole
// body looked at when the region shows several.
Parts find_parts(const Body &body, const Octree &octree, const Adf &adf, const Aabb *region = nullptr,
		float finest = 0.05f);

// A part of at least `least` mm^3 come away from the rest, looked for round `near` (where a
// cut was just made): if the material there is in one piece, nothing came away; if a part
// lies wholly inside `near`, with `margin` mm to spare, that is the island; otherwise the
// whole body is looked at, and the island is its smallest part.
struct Island {
	Parts parts;      // of the last region looked at
	Aabb region;
	int island = -1;  // the island's part, or -1
	int passes = 0;
	double ms = 0.0;  // all passes
};
Island find_island(const Body &body, const Octree &octree, const Adf &adf, const Aabb &near, double least = 1.0,
		float margin = 2.0f, float finest = 0.05f);

// The island cut out: a Region (body/region.h) whose island cubes hold all of `island`'s
// material and whose rest cubes hold everything else's near it, over the island's bounds
// with `margin` mm round them. Cubes split where the samples in them are of both sides,
// and where an island cube touches a rest cube, down to `finest` mm. What is left is the
// proof that the island came away: its cubes and the rest's meet nowhere, and free cubes
// hold no material (checked against the exact field). No region (and `failed` says why) if
// the sides still touch at the finest: a bridge the samples did not see, or a gap too thin
// for free air.
struct CutOut {
	std::shared_ptr<const Region> region;
	const char *failed = nullptr;
	std::size_t leaves = 0, evaluations = 0;
	int passes = 0; // of splitting where the sides touched
	double ms = 0.0;
};
CutOut cut_out(const Body &body, const Octree &octree, const Adf &adf, const Parts &parts, int island,
		float margin = 2.0f, float finest = 0.05f);

} // namespace sdf
