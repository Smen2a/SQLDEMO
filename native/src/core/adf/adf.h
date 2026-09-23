#pragma once

#include "body/body.h"
#include "compile/octree.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdf {

struct AdfParams {
	float max_voxel = 1.0f;    // coarsest sample spacing (mm)
	float min_voxel = 0.02f;   // finest
	float tolerance = 0.005f;  // allowed |reconstruction - exact| near the surface (mm)
	// Cells that miss the tolerance (creases, mostly) stop refining once they are this
	// small and are evaluated exactly instead, if their tape is short enough to be cheap.
	float exact_cell = 1.0f;
	int exact_tape_limit = 16;
	int threads = 0;           // 0 = hardware concurrency
};

// An adaptive distance field: the body's field sampled into small bricks along its
// surface, for display. It is a cache of the octree field, never the truth.
//
// The tree mirrors the tape octree and continues below its surface cells: cells are split
// until their bricks' samples are at most `max_voxel` apart, and further, down to
// `min_voxel`, wherever trilinear reconstruction strays more than `tolerance` from the
// exact field near the surface (at creases, mostly: a plane is reconstructed exactly).
// Leaves hold one brick of 8^3 samples spanning the cell (7 voxels a side), or nothing
// where the surface provably is not.
//
// Trilinear reconstruction rounds a crease over about a voxel, so following creases with
// bricks alone would take ever finer ones. Instead a small cell that still misses the
// tolerance becomes an *exact* leaf: it keeps its brick, which rays march on while they
// are further from the surface than the brick's error bound (cheap), but hits, normals and
// materials there come from a tape (exact): the octree cell's tape, pruned again to the
// small leaf, which leaves only the few edits that form the crease. Creases stay sharp and
// cost short tape evaluations only for the pixels on them. Cells whose pruned tapes are
// still too long keep refining down to min_voxel.
//
// Evaluating a brick costs a descent and one trilinear lookup whatever the edit count,
// which is what makes Live display fast; building it costs the exact evaluations once per
// edit. Samples are stored as half floats, exactly as the GPU filters them.
class Adf {
public:
	static constexpr int kSide = 8;                      // samples per brick side
	static constexpr int kBrickSamples = kSide * kSide * kSide;
	static constexpr std::int32_t kEmpty = -1, kSolid = -2;
	// Exact leaves store kExactBase + their exact_cells() index in `material`.
	static constexpr std::int32_t kExactBase = 256;

	// An exact leaf's own tape: its octree cell's tape pruned to the leaf.
	struct ExactCell {
		std::uint32_t offset = 0, count = 0; // entries of exact_tape(), as in Octree::Leaf::tape
		bool base = true;                     // whether the base primitive is still in play
	};

	struct Node {
		vec3 lo;
		float size = 0;
		std::int32_t child = -1;     // first of 8 consecutive children, or -1 for a leaf
		std::int32_t brick = kEmpty; // brick slot, or kEmpty / kSolid for a leaf without one
		// Uniform material id (0 .. kExactBase - 1), or -(material brick slot + 1) where it
		// varies, or kExactBase + exact cell index for an exact leaf (whose tape has the
		// material).
		std::int32_t material = 0;
		// Brick leaves: a bound on the gradient of the trilinear field (and, in exact leaves,
		// of the tape's). Empty and solid leaves: the field at the cell centre (for ambient
		// occlusion taps; there is no surface there).
		float value = 0;

		bool exact() const { return brick >= 0 && material >= kExactBase; }
	};

	struct Stats {
		std::size_t nodes = 0, bricks = 0, material_bricks = 0, exact_leaves = 0, bytes = 0;
		double mean_exact_tape = 0;
		std::size_t rebuilt_bricks = 0; // by the last build or update
		float finest_voxel = 0;
		double seconds = 0;             // last build or update
	};

	void build(const Body &body, const Octree &octree, const AdfParams &params = {});
	// Re-samples every cell overlapping `region`, keeping the rest (bricks included). Call it
	// after the octree has taken the new edits, with the region where the field changed:
	// see dirty_region().
	void update(const Body &body, const Octree &octree, const Aabb &region);
	// Where edit `index` can have changed the octree's sampled field.
	static Aabb dirty_region(const Body &body, const Octree &octree, std::size_t index);

	// The reconstructed field: trilinear inside bricks, the tape in exact leaves, the stored
	// centre value elsewhere. `body` and `octree` must be the ones it was built from.
	float distance(const Body &body, const Octree &octree, vec3 p) const;
	// Bricks only, even in exact leaves: cheap, and within about a voxel (for ambient
	// occlusion taps).
	float approx_distance(vec3 p) const;
	// With the material of the nearest sample (or the tape's, in exact leaves).
	Sample sample(const Body &body, const Octree &octree, vec3 p) const;
	// Leaf node containing p, or -1 outside the root.
	int leaf(vec3 p) const;

	struct Step {
		float d;        // brick value at p (+inf in empty cells, -1 in solid ones)
		float exit;     // distance along dir to where the ray leaves the cell
		std::int32_t brick;
		bool exact;     // an exact leaf: d is only within `error` of the field
		float error;    // bound on |brick value - field| in the cell (exact leaves)
		float lipschitz;
		float voxel;    // sample spacing in the cell
	};
	// For tracing: the cell containing p and its brick's value there. In an exact leaf,
	// step by (d - error) while that exceeds the hit epsilon, then evaluate the tape.
	Step step(vec3 p, vec3 dir) const;

	const std::vector<Node> &nodes() const { return nodes_; }
	// Half floats, kBrickSamples per slot, x fastest, then y, then z.
	const std::vector<std::uint16_t> &brick_values() const { return values_; }
	// RGBA8 (material 0, material 1, mix * 255, 255), kBrickSamples per material slot.
	const std::vector<std::uint8_t> &material_values() const { return materials_; }
	const std::vector<ExactCell> &exact_cells() const { return exact_cells_; }
	const std::vector<std::uint32_t> &exact_tape() const { return exact_tape_; }
	std::size_t brick_slots() const { return values_.size() / kBrickSamples; }
	std::size_t material_slots() const { return materials_.size() / (4 * kBrickSamples); }
	// Slots written by the last build or update, for incremental uploads.
	const std::vector<std::uint32_t> &dirty_bricks() const { return dirty_bricks_; }
	const std::vector<std::uint32_t> &dirty_materials() const { return dirty_materials_; }
	const AdfParams &params() const { return params_; }
	Stats stats() const;

private:
	void rebuild(const Body &body, const Octree &octree, const Aabb &region, bool reuse);

	AdfParams params_;
	std::vector<Node> nodes_;
	std::vector<std::uint16_t> values_;
	std::vector<std::uint8_t> materials_;
	std::vector<ExactCell> exact_cells_;
	std::vector<std::uint32_t> exact_tape_;
	std::vector<std::uint32_t> dirty_bricks_, dirty_materials_;
	std::size_t live_bricks_ = 0, live_materials_ = 0, rebuilt_ = 0;
	double seconds_ = 0;
};

} // namespace sdf
