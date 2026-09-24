#pragma once

#include "adf/adf.h"
#include "body/body.h"
#include "compile/octree.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace sdf {

// A body being edited live: its edit list with a stroke in progress, unlimited undo and
// redo, and its octree and ADF kept up to date incrementally.
//
// A stroke is the group of edits one use of a tool makes. While the tool moves,
// set_stroke() replaces the stroke in progress (the last edits in the list); commit()
// turns it into an undo step.
class EditSession {
public:
	struct Timing {
		double octree_ms = 0, adf_ms = 0;
		std::size_t rebuilt_bricks = 0;
	};

	// Starts over from `body` (full builds). Without an ADF (bodies drawn from their exact
	// tapes, like tools) only the octree is kept.
	void reset(const Body &body, const AdfParams &params = {}, bool with_adf = true);
	bool has_adf() const { return with_adf_; }
	// Builds the ADF for the current body, or drops it, keeping the edit history.
	void set_adf(bool with_adf);
	// Where the ADF takes its samples from from now on (see Adf::set_sampler).
	void set_adf_sampler(std::shared_ptr<AdfSampler> sampler);
	// Edits leave the creases they make in coarse ADF cells, to land fast; refine() takes
	// them down to the finest (Adf::refine), for cheaper drawing, when there is time.
	bool needs_refine() const { return with_adf_ && adf_.needs_refine(); }
	void refine();
	// Replaces the stroke in progress with `edits`. Returns false and changes nothing if
	// the body would reject any of them (Body::accepts).
	bool set_stroke(const std::vector<Edit> &edits);
	// Appends `edits` to the stroke in progress: only cells they reach are re-sampled.
	bool extend_stroke(const std::vector<Edit> &edits);
	// Drops the stroke's last `drop` edits (at most all of them), then appends `edits`.
	// With `changed` (not empty) and as many edits as it drops, the new edits replace the
	// old ones in place, promising they are the same field outside `changed`: only there
	// are the octree and ADF brought along (a smoothing layer growing under a moving tool).
	bool revise_stroke(std::size_t drop, const std::vector<Edit> &edits, const Aabb &changed = {});
	void commit();  // the stroke in progress, if it has edits, becomes an undo step
	void cancel();  // drops the stroke in progress
	bool undo();    // removes the last step (dropping any stroke in progress first)
	bool redo();
	bool can_undo() const { return !steps_.empty(); }
	bool can_redo() const { return !redo_.empty(); }
	std::size_t stroke_edits() const { return stroke_; }
	std::size_t steps() const { return steps_.size(); }

	const Body &body() const { return body_; }
	const Octree &octree() const { return octree_; }
	const Adf &adf() const { return adf_; }
	const Timing &last() const { return last_; }

private:
	// Keeps the first `keep` edits and appends `add`, bringing the octree and ADF along.
	void apply(std::size_t keep, const std::vector<Edit> &add);
	// Replaces the edits from `first` on with `with` (as many), which differ only in `changed`.
	void replace(std::size_t first, const std::vector<Edit> &with, const Aabb &changed);

	Body body_;
	Octree octree_;
	Adf adf_;
	std::vector<std::size_t> steps_;      // edits per committed step, oldest first
	std::vector<std::vector<Edit>> redo_; // undone steps, most recent last
	std::size_t stroke_ = 0;              // edits in the stroke in progress (the last ones)
	bool with_adf_ = true;
	AdfParams params_;
	std::shared_ptr<AdfSampler> sampler_;
	Timing last_;
};

} // namespace sdf
