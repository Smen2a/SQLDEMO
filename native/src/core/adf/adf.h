#pragma once

#include "body/body.h"
#include "compile/octree.h"

#include <memory>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdf {

class AdfSampler;

struct AdfParams {
	float max_voxel = 1.0f;    // coarsest sample spacing (mm)
	float min_voxel = 0.04f;   // finest
	float tolerance = 0.01f;   // allowed |reconstruction - exact| near the surface (mm)
	// Cells that miss the tolerance (creases, mostly) stop refining once they are this
	// small and are evaluated exactly instead, if their tape is short enough to be cheap.
	float exact_cell = 1.0f;
	// Updates stop at exact cells this large (7 = max_voxel * 7, the largest bricks: they
	// never split for creases), so an edit lands in milliseconds; refine() then takes the
	// creases down to exact_cell in the background, for cheaper drawing.
	float update_exact_cell = 7.0f;
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
		// How far the leaf's brick strays from the tape near the surface: twice the largest
		// difference measured at the centres of the voxels there, plus filtering precision.
		// Rays march on the brick until it is within this of the hit epsilon. (The worst case,
		// L * voxel * sqrt(3), is some 30x wider at creases and would send far more steps to
		// the tape; an underestimate costs microns of overshoot, which the hit's settling steps
		// take back.)
		float error = 0;
	};

	struct Node {
		vec3 lo;
		float size = 0;
		std::int32_t child = -1;     // first of 8 consecutive children, or -1 for a leaf
		std::int32_t brick = kEmpty; // brick slot, or kEmpty / kSolid for a leaf without one
		// Uniform material id (0 .. kExactBase - 1), or -(material brick slot + 1) where it
		// varies, or kExactBase + exact cell index for an exact leaf (whose tape has the
		// material). Empty and solid leaves: the material at the cell centre (what a cut
		// into a solid cell shows before the ADF is updated: see the Live shader's overlay).
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
		// Of those, bricks the last update's cuts were folded into (their old samples cut,
		// not re-sampled), and bricks whose samples a fold left as they were (kept, not
		// counted in rebuilt_bricks).
		std::size_t folded_bricks = 0, unchanged_bricks = 0;
		float finest_voxel = 0;
		double seconds = 0;             // last build or update
	};

	void build(const Body &body, const Octree &octree, const AdfParams &params = {});
	// Where builds and updates take their samples from (see AdfSampler): the CPU, spread over
	// params.threads, when null (the default).
	void set_sampler(std::shared_ptr<AdfSampler> sampler) { sampler_ = std::move(sampler); }

	// What changed in the body since the ADF was last built or updated.
	struct Change {
		Aabb region;                    // where the field may differ: every added and removed edit's dirty_region()
		std::uint32_t first_changed = 0; // edits from here on are new; those before it are untouched
		Aabb removed;                   // where removed (or replaced) edits reached
	};
	// Re-samples the cells in `change.region` that a changed edit reaches, keeping the rest
	// (bricks included): cells whose pruned tape holds no edit from first_changed on are
	// kept unless a removed edit reached them. Call it after the octree has been updated.
	void update(const Body &body, const Octree &octree, const Change &change);
	// The same after appending edits only (from the edit count at the last build or update).
	void update(const Body &body, const Octree &octree, const Aabb &region);
	// Updates leave the creases they make in coarse exact leaves (update_exact_cell):
	// refine() takes those down to exact_cell, as a build would, keeping everything else.
	// The field is the same either way (exact leaves evaluate their tapes); finer leaves
	// only draw faster.
	void refine(const Body &body, const Octree &octree);
	bool needs_refine() const { return !coarse_.empty(); }
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
	// Leaf node containing p, or -1 outside the root: from the grid cell containing p down.
	int leaf(vec3 p) const;

	// A dense grid over the root cube, kGridSide blocks a side: each block's deepest node
	// covering all of it (the block's node at depth kGridLevels, or a leaf above it), so a
	// lookup descends a few levels from there rather than a dozen from the root.
	static constexpr int kGridLevels = 6;
	static constexpr int kGridSide = 1 << kGridLevels;
	struct GridCell {
		std::int32_t node = 0;
		std::int32_t depth = 0;
	};
	// x fastest, then y, then z.
	const std::vector<GridCell> &grid() const { return grid_; }
	// Blocks per millimetre: grid coordinates are (p - root lo) * grid_scale(). One multiply,
	// correctly rounded on CPUs and GPUs alike, so both pick the same block.
	float grid_scale() const { return float(kGridSide) / nodes_[0].size; }

	struct Step {
		float d;        // brick value at p (+inf in empty cells, -1 in solid ones)
		float exit;     // distance along dir to where the ray leaves the cell
		std::int32_t brick;
		bool exact;     // an exact leaf: d is only within `error` of the field
		float error;    // its ExactCell::error (exact leaves), else 0
		float lipschitz;
		float voxel;    // sample spacing in the cell
	};
	// For tracing: the cell containing p and its brick's value there. In an exact leaf,
	// step by (d - error) while that exceeds the hit epsilon, then evaluate the tape.
	Step step(vec3 p, vec3 dir) const;

	// The surface normal at p (a converged hit), as the Live shader computes it: tetrahedral
	// differences within the leaf containing p, never descending again. An exact leaf
	// differences its tape with h = clamp(eps, 2e-3, kExactNormalReach): its tape is exact
	// that close outside it too, as pruning keeps every edit within the value margin of the
	// cell. A brick leaf differences its brick half a voxel apart (or eps, if larger), the
	// stencil shifted to stay inside the leaf. `fallback` elsewhere (not on the surface).
	vec3 normal(const Body &body, vec3 p, float eps, vec3 fallback) const;
	static constexpr float kExactNormalReach = 0.25f;

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
	enum class Pass { Build, Update, Refine };
	void rebuild(const Body &body, const Octree &octree, const Change &change, bool reuse, Pass pass);
	void build_grid();

	AdfParams params_;
	std::vector<Node> nodes_;
	std::vector<std::uint16_t> values_;
	std::vector<std::uint8_t> materials_;
	std::vector<ExactCell> exact_cells_;
	std::vector<std::uint32_t> exact_tape_;
	std::vector<std::uint32_t> dirty_bricks_, dirty_materials_;
	std::vector<GridCell> grid_;
	std::size_t live_bricks_ = 0, live_materials_ = 0, rebuilt_ = 0, folded_ = 0, unchanged_ = 0;
	std::size_t edits_ = 0; // the body's edit count when last built or updated
	std::shared_ptr<AdfSampler> sampler_;
	Aabb coarse_; // where updates may have left coarse exact leaves
	double seconds_ = 0;
};

} // namespace sdf
