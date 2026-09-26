#pragma once

#include "body/body.h"

#include <cstdint>
#include <vector>

namespace sdf {

struct OctreeParams {
	int max_depth = 12;
	int leaf_budget = 12;    // subdivide surface leaves whose tape is longer than this
	float min_size = 0.5f;   // never split cells below this edge length (mm)
	// Nor below this fraction of the smallest feature in the cell's tape: chipped stone does
	// not need the sub-millimetre cells a fine V-tool line does.
	float feature_fraction = 0.5f;
	// Within this distance of the surface, pruned tapes reproduce the body's field values
	// exactly (not just its sign) — enough for normals and ambient occlusion.
	float value_margin = 1.0f;
};

// A body's field compiled into an adaptive octree of cubic cells, each holding a pruned
// "tape": only the edits that can shape the surface inside it, in order. Within a cell the
// tape's zero set equals the full body's, and within value_margin of the surface so do its
// values; further out it stays a valid distance bound for its own cell. Tracing must
// therefore clamp steps at cell exits (see step()).
//
// An edit is dropped from a cell when its bounds, expanded by its own influence, by the
// widest value_reach() of any later edit touching the cell, and by value_margin, miss the
// cell. The middle term is what keeps this exact: hard min/max depend only on the signs of
// their operands, but a later blend (or groove depth) reads actual values, which a dropped
// neighbour could otherwise have fed.
class Octree {
public:
	enum class State : std::int8_t { Surface = 0, Empty = 1, Solid = -1 };

	struct Leaf {
		std::vector<std::uint32_t> tape; // edit indices; kResetBit marks a distance reset
		bool base = true;                // whether the base primitive is still in play
		State state = State::Surface;
		float lipschitz = 1.0f;          // bound on |grad| of this tape's field
		float feature = 1e9f;            // smallest feature_size() among the tape's edits
	};

	struct Node {
		vec3 lo;
		float size = 0;
		std::int32_t child = -1; // first of 8 consecutive children, or -1
		std::int32_t leaf = -1;  // index into leaves() when this is a leaf
		std::int32_t depth = 0;
	};

	static constexpr std::uint32_t kResetBit = 0x80000000u;

	void build(const Body &body, const OctreeParams &params = {});
	// Updates the cells touched by edit `index`, which must be the body's newest edit.
	void add_edit(const Body &body, std::uint32_t index);
	// Re-prunes every leaf overlapping `region` from the body's whole edit list: after edits
	// were removed or replaced, with the region they (and their replacements) reached,
	// expanded by value_margin(). Leaves keep any splits.
	void update_region(const Body &body, const Aabb &region) { rebuild_region(body, region); }

	Sample sample(const Body &body, vec3 p) const;
	float distance(const Body &body, vec3 p) const { return sample(body, p).d; }

	struct Step {
		float d;      // field value at p from the cell's tape (+inf in empty cells)
		float exit;   // distance along dir to where the ray leaves the cell
		State state;
		float lipschitz; // divide d by this for a safe step
	};
	// For tracing: the cell containing p, its tape's value at p and where dir leaves it.
	// Empty cells report +inf unless want_distance asks for their (still valid) value.
	Step step(const Body &body, vec3 p, vec3 dir, bool want_distance = false) const;

	// A tape pruned further to the cube (lo, size) inside the cell it came from (a leaf's,
	// or one already pruned from it): exact there, as leaf tapes are in their cells.
	Leaf prune_within(const Body &body, const Leaf &from, vec3 lo, float size) const {
		return prune(body, from, lo, size);
	}
	// A tape (entries as in Leaf::tape) evaluated at p.
	static Sample eval_tape(const Body &body, bool base, const std::uint32_t *tape, std::size_t count, vec3 p);
	// The distance after applying tape entries to `d`, the field before them at p (the
	// material is left out: for cuts, which keep it).
	static float continue_tape(const Body &body, float d, const std::uint32_t *tape, std::size_t count, vec3 p);

	const std::vector<Node> &nodes() const { return nodes_; }
	const std::vector<Leaf> &leaves() const { return leaves_; }
	float value_margin() const { return params_.value_margin; }
	// Index into nodes() of the leaf cell containing p, or -1 outside the root.
	int leaf_node(vec3 p) const;

	struct Stats {
		std::size_t nodes = 0, leaves = 0, surface_leaves = 0, tape_entries = 0, max_tape = 0;
		double mean_surface_tape = 0;
	};
	Stats stats() const;

private:
	Leaf prune(const Body &body, const Leaf &parent, vec3 lo, float size) const;
	void build_node(const Body &body, int node, const Leaf &from);
	void refine(const Body &body, int node);
	void rebuild_region(const Body &body, const Aabb &region);
	Sample eval_leaf(const Body &body, const Leaf &leaf, vec3 p) const;

	OctreeParams params_;
	std::vector<Node> nodes_;
	std::vector<Leaf> leaves_;
};

} // namespace sdf
