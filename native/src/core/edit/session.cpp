#include "edit/session.h"

#include <chrono>

namespace sdf {

namespace {

double ms_since(std::chrono::steady_clock::time_point t) {
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// Whether edit `index` adds solid beyond the octree's root cube (which then has to grow).
bool grows_past_root(const Body &body, const Octree &octree, std::size_t index) {
	const Edit &e = body.edits()[index];
	if (e.op != Op::Union && e.op != Op::Tongue) {
		return false;
	}
	const Octree::Node &root = octree.nodes()[0];
	const Aabb grown = body.edit_box(index).expanded(body.edit_influence(index));
	const vec3 hi = root.lo + vec3(root.size);
	return grown.lo.x < root.lo.x || grown.lo.y < root.lo.y || grown.lo.z < root.lo.z || grown.hi.x > hi.x ||
			grown.hi.y > hi.y || grown.hi.z > hi.z;
}

} // namespace

void EditSession::reset(const Body &body, const AdfParams &params) {
	const auto start = std::chrono::steady_clock::now();
	body_ = body;
	octree_.build(body_);
	last_.octree_ms = ms_since(start);
	const auto adf_start = std::chrono::steady_clock::now();
	adf_.build(body_, octree_, params);
	last_.adf_ms = ms_since(adf_start);
	last_.rebuilt_bricks = adf_.stats().rebuilt_bricks;
	steps_.clear();
	redo_.clear();
	stroke_ = 0;
}

bool EditSession::set_stroke(const std::vector<Edit> &edits) {
	for (const Edit &e : edits) {
		if (!Body::accepts(e)) {
			return false;
		}
	}
	apply(body_.edits().size() - stroke_, edits);
	stroke_ = edits.size();
	return true;
}

bool EditSession::extend_stroke(const std::vector<Edit> &edits) {
	for (const Edit &e : edits) {
		if (!Body::accepts(e)) {
			return false;
		}
	}
	apply(body_.edits().size(), edits);
	stroke_ += edits.size();
	return true;
}

void EditSession::commit() {
	if (stroke_ > 0) {
		steps_.push_back(stroke_);
		stroke_ = 0;
		redo_.clear();
	}
}

void EditSession::cancel() {
	if (stroke_ > 0) {
		apply(body_.edits().size() - stroke_, {});
		stroke_ = 0;
	}
}

bool EditSession::undo() {
	cancel();
	if (steps_.empty()) {
		return false;
	}
	const std::size_t n = steps_.back(), keep = body_.edits().size() - n;
	steps_.pop_back();
	redo_.emplace_back(body_.edits().begin() + std::ptrdiff_t(keep), body_.edits().end());
	apply(keep, {});
	return true;
}

bool EditSession::redo() {
	cancel();
	if (redo_.empty()) {
		return false;
	}
	const std::vector<Edit> step = std::move(redo_.back());
	redo_.pop_back();
	apply(body_.edits().size(), step);
	steps_.push_back(step.size());
	return true;
}

void EditSession::apply(std::size_t keep, const std::vector<Edit> &add) {
	if (keep == body_.edits().size() && add.empty()) {
		return;
	}
	const auto start = std::chrono::steady_clock::now();
	// Where the edits going away reached, before they go.
	Aabb removed;
	for (std::size_t i = keep; i < body_.edits().size(); ++i) {
		removed.include(Adf::dirty_region(body_, octree_, i));
	}
	while (body_.edits().size() > keep) {
		body_.pop();
	}
	Aabb added;
	bool grows = false;
	for (const Edit &e : add) {
		body_.add(e);
		const std::size_t i = body_.edits().size() - 1;
		added.include(Adf::dirty_region(body_, octree_, i));
		grows = grows || grows_past_root(body_, octree_, i);
	}

	if (removed.empty()) {
		for (std::size_t i = keep; i < body_.edits().size(); ++i) {
			octree_.add_edit(body_, std::uint32_t(i)); // grows the root itself if it must
		}
	} else if (grows) {
		octree_.build(body_);
	} else {
		Aabb both = removed;
		both.include(added);
		octree_.update_region(body_, both.expanded(octree_.value_margin()));
	}
	last_.octree_ms = ms_since(start);

	const auto adf_start = std::chrono::steady_clock::now();
	Aabb region = removed;
	region.include(added);
	adf_.update(body_, octree_, Adf::Change{region, std::uint32_t(keep), removed});
	last_.adf_ms = ms_since(adf_start);
	last_.rebuilt_bricks = adf_.stats().rebuilt_bricks;
}

} // namespace sdf
