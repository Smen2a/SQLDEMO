#include "compile/octree.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf {

namespace {

constexpr float kSqrt3Half = 0.8660254f;

bool box_overlaps_cube(const Aabb &b, vec3 lo, float size) {
	return b.lo.x <= lo.x + size && b.hi.x >= lo.x && b.lo.y <= lo.y + size && b.hi.y >= lo.y &&
			b.lo.z <= lo.z + size && b.hi.z >= lo.z;
}

// Distance field value where a reset replaces everything before an edit: the identity of
// the edit's operation, so applying the edit yields just its own primitive.
float reset_value(Op op) {
	return op == Op::Union ? gl::SDF_BIG : -gl::SDF_BIG;
}

struct Interval {
	float lo, hi;
};

Interval neg(Interval a) {
	return {-a.hi, -a.lo};
}

Interval imin(Interval a, Interval b) {
	return {std::min(a.lo, b.lo), std::min(a.hi, b.hi)};
}

Interval imax(Interval a, Interval b) {
	return {std::max(a.lo, b.lo), std::max(a.hi, b.hi)};
}

Interval iabs(Interval a) {
	if (a.lo >= 0.0f) {
		return a;
	}
	if (a.hi <= 0.0f) {
		return neg(a);
	}
	return {0.0f, std::max(-a.lo, a.hi)};
}

// Range of the union-form blend U(a, b) over operand ranges a and b. Every mode satisfies
// U <= min(a, b); the lower bounds follow from each formula (see sdf_blend.glsl).
Interval union_interval(const Edit &e, Interval a, Interval b) {
	const Interval hard = imin(a, b);
	if (blend_inactive(e, a.lo, a.hi, b.lo, b.hi)) {
		return hard;
	}
	const float m = hard.lo;
	float lo = m;
	switch (e.blend) {
		case Blend::Hard:
			break;
		case Blend::Smooth:
			lo = m - 0.25f * e.r;
			break;
		case Blend::SmoothC2:
			lo = m - e.r / 6.0f;
			break;
		case Blend::Round: // r - |(r - a, r - b)| >= r - sqrt(2) (r - m)
			lo = m < e.r ? m - (std::sqrt(2.0f) - 1.0f) * (e.r - m) : m;
			break;
		case Blend::Chamfer: // the bevel plane is increasing in both operands
			lo = std::min(m, (m * (e.r + e.r2) - e.r * e.r2) / std::sqrt(e.r * e.r + e.r2 * e.r2));
			break;
		case Blend::Profile: // U >= min(a, b, rect) and the rectangle's distance >= -min(r, r2)
			lo = std::min(m, -std::min(e.r, e.r2));
			break;
	}
	return {lo, hard.hi};
}

// Range of the field after applying edit e, given ranges of the field before it and of
// the edit's primitive.
Interval apply_interval(const Edit &e, Interval d, Interval ev) {
	switch (e.op) {
		case Op::Union:
			return union_interval(e, d, ev);
		case Op::Subtract:
			return neg(union_interval(e, neg(d), ev));
		case Op::Intersect:
			return neg(union_interval(e, neg(d), neg(ev)));
		case Op::Engrave: {
			const Interval ae = iabs(ev);
			const float k = gl::SDF_SQRT_HALF;
			return imax(d, {(d.lo + e.r - ae.hi) * k, (d.hi + e.r - ae.lo) * k});
		}
		case Op::Groove: {
			const Interval ae = iabs(ev);
			return imax(d, imin({d.lo + e.r, d.hi + e.r}, {e.r2 - ae.hi, e.r2 - ae.lo}));
		}
		case Op::Tongue: {
			const Interval ae = iabs(ev);
			return imin(d, imax({d.lo - e.r, d.hi - e.r}, {ae.lo - e.r2, ae.hi - e.r2}));
		}
		case Op::Paint:
			return d;
	}
	return d;
}

bool changes_material(const Body &body, const Edit &e) {
	return e.op == Op::Paint || (e.op == Op::Union && e.material != body.base_material);
}

} // namespace

Octree::Leaf Octree::prune(const Body &body, const Leaf &parent, vec3 lo, float size) const {
	const vec3 centre = lo + vec3(size * 0.5f);
	const float radius = size * kSqrt3Half;
	const float margin = params_.value_margin;
	const std::size_t n = parent.tape.size();

	// Each primitive's range over the cell: value at the centre +- its Lipschitz bound times
	// the cell's half-diagonal. Computed once, used by both passes.
	std::vector<char> touches(n, 0);
	std::vector<Interval> range(n);
	for (std::size_t i = 0; i < n; ++i) {
		const std::uint32_t index = parent.tape[i] & ~kResetBit;
		const Edit &e = body.edits()[index];
		touches[i] = e.op == Op::Intersect || (parent.tape[i] & kResetBit) ||
				box_overlaps_cube(body.edit_box(index).expanded(body.edit_influence(index)), lo, size);
		if (touches[i]) {
			const float ec = e.prim.eval(centre), es = e.prim.lipschitz() * radius;
			range[i] = {ec - es, ec + es};
		}
	}
	// A value-dependent edit (blend, guide op) only reads the field near its own
	// primitive's surface; if that surface is provably further than its reach from this
	// cell, it behaves exactly like its hard form here.
	auto reads_values_here = [&](std::size_t i, const Edit &e) {
		const float reach = e.value_reach() + margin;
		return e.value_reach() > 0.0f && range[i].lo < reach && range[i].hi > -reach;
	};

	// Backward pass.
	//  later_reach[i]: the widest value_reach of any edit after position i that reads values
	//    in this cell. Dropping edit i is only safe if it cannot feed those.
	//  superseded[i]: a hard cut whose region a later hard cut provably contains throughout
	//    this cell — finishing cuts superseding roughing cuts, the common case in a long
	//    carving session. Ops in between that are monotone in their operands (every hard
	//    op, and round / chamfer / smooth blends) keep the later cut winning everywhere the
	//    earlier one mattered, except that an active blend can reach up to its support past
	//    the earlier cut's wall — so each one raises the margin the later cut must win by.
	//    Profiles, guide ops and material changes stop the search: the first two are not
	//    monotone, and the last would expose a different material.
	std::vector<float> later_reach(n + 1, 0.0f);
	std::vector<char> superseded(n, 0);
	float deepest_later = gl::SDF_BIG; // least upper bound of a later hard cut's distance
	for (std::size_t i = n; i-- > 0;) {
		later_reach[i] = later_reach[i + 1];
		if (!touches[i]) {
			continue; // cannot affect this cell; the forward pass drops it
		}
		const Edit &e = body.edits()[parent.tape[i] & ~kResetBit];
		const bool active = reads_values_here(i, e);
		if (active) {
			later_reach[i] = std::max(later_reach[i], e.value_reach());
		}
		const bool hard_cut = e.op == Op::Subtract && (e.blend == Blend::Hard || !active);
		const bool monotone = (e.op == Op::Union || e.op == Op::Subtract || e.op == Op::Intersect) &&
				e.blend != Blend::Profile && !changes_material(body, e);
		if (hard_cut) {
			if (range[i].lo >= deepest_later && !(parent.tape[i] & kResetBit)) {
				superseded[i] = 1;
			}
			deepest_later = std::min(deepest_later, range[i].hi);
		} else if (monotone || (e.op != Op::Paint && !active && !changes_material(body, e))) {
			if (active) {
				deepest_later += e.value_reach();
			}
		} else if (e.op != Op::Paint) {
			deepest_later = gl::SDF_BIG;
		}
	}

	// Forward pass: interval arithmetic along the tape. The running field's range over
	// the cell is tracked through each op's own rules, so a steep but inactive edit never
	// widens it.
	Leaf out;
	out.base = parent.base;
	Interval d{gl::SDF_BIG, gl::SDF_BIG};
	float lip = 0.0f;
	if (out.base) {
		const float bc = body.base.eval(centre), bs = body.base.lipschitz() * radius;
		d = {bc - bs, bc + bs};
		lip = body.base.lipschitz();
	}
	bool material_touched = false;

	for (std::size_t i = 0; i < n; ++i) {
		const std::uint32_t entry = parent.tape[i];
		const std::uint32_t index = entry & ~kResetBit;
		const Edit &e = body.edits()[index];
		bool reset = (entry & kResetBit) != 0;
		if (!reset) {
			// (A reset edit dominated its whole parent cell, so it always touches this one.)
			if (superseded[i]) {
				continue;
			}
			// Not just "does it touch this cell": an edit that stops short of the cell can
			// still feed the values a later blend reads here, hence later_reach.
			const float reach = body.edit_influence(index) + later_reach[i + 1] + margin;
			if (e.op != Op::Intersect && !box_overlaps_cube(body.edit_box(index).expanded(reach), lo, size)) {
				continue;
			}
			if (!touches[i]) {
				const float ec = e.prim.eval(centre), es = e.prim.lipschitz() * radius;
				range[i] = {ec - es, ec + es};
			}
			if (e.op == Op::Union || e.op == Op::Subtract || e.op == Op::Intersect) {
				// Union form: result = +-U(a, b). Subtract: a = -d, b = e. Intersect: a = -d, b = -e.
				const Interval a = e.op == Op::Union ? d : neg(d);
				const Interval b = e.op == Op::Intersect ? neg(range[i]) : range[i];
				if (blend_inactive(e, a.lo, a.hi, b.lo, b.hi)) {
					if (b.lo >= a.hi) {
						continue; // U = a everywhere: the edit changes nothing here
					}
					// U = b everywhere: the edit's primitive alone defines this cell. A cut
					// may only discard history that cannot have changed the material.
					if (b.hi < a.lo && (e.op == Op::Union || !material_touched)) {
						reset = true;
					}
				}
			}
		}

		if (reset) {
			out.tape.clear();
			out.base = false;
			out.feature = 1e9f;
			const float sentinel = reset_value(e.op);
			d = {sentinel, sentinel};
			lip = 0.0f;
			material_touched = false;
		}
		out.tape.push_back(index | (reset ? kResetBit : 0u));
		lip = std::max(lip, e.lipschitz());
		out.feature = std::min(out.feature, e.feature_size());
		d = apply_interval(e, d, range[i]);
		if (e.op == Op::Union && reset) {
			material_touched = e.material != body.base_material;
		} else if (changes_material(body, e)) {
			material_touched = true;
		}
	}

	out.lipschitz = std::max(lip, 1.0f);
	if (d.lo > 0.0f) {
		out.state = State::Empty;
	} else if (d.hi < 0.0f) {
		out.state = State::Solid;
	}
	return out;
}

void Octree::build(const Body &body, const OctreeParams &params) {
	params_ = params;
	nodes_.clear();
	leaves_.clear();

	const Aabb b = body.bounds().expanded(1.0f);
	const vec3 extent = b.size();
	const float size = std::max(extent.x, std::max(extent.y, extent.z));
	Node root;
	root.lo = b.centre() - vec3(size * 0.5f);
	root.size = size;
	nodes_.push_back(root);

	Leaf all;
	all.base = true;
	all.tape.reserve(body.edits().size());
	for (std::uint32_t i = 0; i < body.edits().size(); ++i) {
		all.tape.push_back(i);
	}
	build_node(body, 0, all);
}

void Octree::build_node(const Body &body, int node, const Leaf &from) {
	Leaf leaf = prune(body, from, nodes_[node].lo, nodes_[node].size);
	nodes_[node].leaf = int(leaves_.size());
	leaves_.push_back(std::move(leaf));
	refine(body, node);
}

// Splits a leaf whose tape is over budget, re-pruning its tape into the eight children —
// but only when that actually shortens the tapes. Where many wide blends overlap, the
// tape cannot shrink however small the cells get, and splitting would only multiply work.
void Octree::refine(const Body &body, int node) {
	const Node n = nodes_[node];
	const Leaf &leaf = leaves_[n.leaf];
	const float floor = std::max(params_.min_size, params_.feature_fraction * leaf.feature);
	if (leaf.state != State::Surface || int(leaf.tape.size()) <= params_.leaf_budget ||
			n.depth >= params_.max_depth || n.size * 0.5f < floor) {
		return;
	}
	const Leaf parent = leaf; // copy: leaves_ is about to grow
	const float half = n.size * 0.5f;
	Leaf kids[8];
	vec3 kid_lo[8];
	std::size_t surface = 0, surface_tape = 0;
	for (int c = 0; c < 8; ++c) {
		kid_lo[c] = n.lo + vec3(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1)) * half;
		kids[c] = prune(body, parent, kid_lo[c], half);
		if (kids[c].state == State::Surface) {
			++surface;
			surface_tape += kids[c].tape.size();
		}
	}
	if (surface > 0 && int(parent.tape.size()) > 4 * params_.leaf_budget &&
			double(surface_tape) / double(surface) > 0.8 * double(parent.tape.size())) {
		return;
	}
	const int first = int(nodes_.size());
	const int parent_slot = n.leaf;
	nodes_[node].child = first;
	nodes_[node].leaf = -1;
	for (int c = 0; c < 8; ++c) {
		Node child;
		child.lo = kid_lo[c];
		child.size = half;
		child.depth = n.depth + 1;
		// The first child takes over the parent's leaf slot; the rest are appended.
		if (c == 0) {
			child.leaf = parent_slot;
			leaves_[parent_slot] = std::move(kids[c]);
		} else {
			child.leaf = int(leaves_.size());
			leaves_.push_back(std::move(kids[c]));
		}
		nodes_.push_back(child);
	}
	for (int c = 0; c < 8; ++c) {
		refine(body, first + c);
	}
}

void Octree::add_edit(const Body &body, std::uint32_t index) {
	const Edit &e = body.edits()[index];
	const float influence = body.edit_influence(index);
	const Aabb root{nodes_[0].lo, nodes_[0].lo + vec3(nodes_[0].size)};
	const Aabb grown = body.edit_box(index).expanded(influence);
	const bool grows_outside = (e.op == Op::Union || e.op == Op::Tongue) &&
			(grown.lo.x < root.lo.x || grown.lo.y < root.lo.y || grown.lo.z < root.lo.z || grown.hi.x > root.hi.x ||
					grown.hi.y > root.hi.y || grown.hi.z > root.hi.z);
	if (grows_outside) {
		build(body, params_); // solid growing past the root cube
		return;
	}
	if (e.value_reach() > 0.0f || e.op == Op::Intersect) {
		// A value-dependent edit can make earlier edits relevant again in the cells it
		// touches (they now need a wider margin), and leaf tapes have already forgotten
		// them. Re-prune those cells from the full edit list.
		rebuild_region(body, e.op == Op::Intersect ? Aabb::infinite() : grown.expanded(params_.value_margin));
		return;
	}
	// Hard edits only ever need their own margin: append to the leaves they touch.
	const Aabb reach_box = grown.expanded(params_.value_margin);
	std::vector<int> stack = {0};
	while (!stack.empty()) {
		const int node = stack.back();
		stack.pop_back();
		const Node n = nodes_[node];
		if (!box_overlaps_cube(reach_box, n.lo, n.size)) {
			continue;
		}
		if (n.child >= 0) {
			for (int c = 0; c < 8; ++c) {
				stack.push_back(n.child + c);
			}
			continue;
		}
		Leaf with_edit = leaves_[n.leaf];
		with_edit.tape.push_back(index);
		with_edit.state = State::Surface;
		leaves_[n.leaf] = prune(body, with_edit, n.lo, n.size);
		refine(body, node);
	}
}

// Re-prunes every leaf overlapping `region` from the complete edit list. Correct for any
// edit, and still local: only the touched leaves are recomputed.
void Octree::rebuild_region(const Body &body, const Aabb &region) {
	Leaf all;
	all.base = true;
	all.tape.reserve(body.edits().size());
	for (std::uint32_t i = 0; i < body.edits().size(); ++i) {
		all.tape.push_back(i);
	}
	std::vector<int> stack = {0};
	while (!stack.empty()) {
		const int node = stack.back();
		stack.pop_back();
		const Node n = nodes_[node];
		if (!box_overlaps_cube(region, n.lo, n.size)) {
			continue;
		}
		if (n.child >= 0) {
			for (int c = 0; c < 8; ++c) {
				stack.push_back(n.child + c);
			}
			continue;
		}
		leaves_[n.leaf] = prune(body, all, n.lo, n.size);
		refine(body, node);
	}
}

int Octree::leaf_node(vec3 p) const {
	if (nodes_.empty()) {
		return -1;
	}
	const Node &root = nodes_[0];
	const vec3 rel = p - root.lo;
	if (rel.x < 0 || rel.y < 0 || rel.z < 0 || rel.x > root.size || rel.y > root.size || rel.z > root.size) {
		return -1;
	}
	int node = 0;
	while (nodes_[node].child >= 0) {
		const Node &n = nodes_[node];
		const float half = n.size * 0.5f;
		const int c = int(p.x >= n.lo.x + half) | (int(p.y >= n.lo.y + half) << 1) | (int(p.z >= n.lo.z + half) << 2);
		node = n.child + c;
	}
	return node;
}

Sample Octree::eval_leaf(const Body &body, const Leaf &leaf, vec3 p) const {
	float d = leaf.base ? body.base.eval(p) : gl::SDF_BIG;
	vec3 mat(float(body.base_material), float(body.base_material), 0.0f);
	for (std::uint32_t entry : leaf.tape) {
		const Edit &e = body.edits()[entry & ~kResetBit];
		if (entry & kResetBit) {
			d = reset_value(e.op);
		}
		const vec4 r = gl::sdf_apply_edit(d, mat, e.prim.eval(p), int(e.op), int(e.blend), e.r, e.r2, int(e.shape),
				float(e.material));
		d = r.x;
		mat = vec3(r.y, r.z, r.w);
	}
	return {d, mat.x, mat.y, mat.z};
}

Sample Octree::sample(const Body &body, vec3 p) const {
	const int node = leaf_node(p);
	if (node < 0) {
		// Outside the root cube the body is at least as far away as the cube.
		const Node &root = nodes_[0];
		const vec3 outside = gl::max(gl::max(root.lo - p, p - (root.lo + vec3(root.size))), 0.0f);
		return {std::max(gl::length(outside), 1e-3f), float(body.base_material), float(body.base_material), 0.0f};
	}
	return eval_leaf(body, leaves_[nodes_[node].leaf], p);
}

Octree::Step Octree::step(const Body &body, vec3 p, vec3 dir, bool want_distance) const {
	const int node = leaf_node(p);
	if (node < 0) {
		return {gl::SDF_BIG, 0.0f, State::Empty, 1.0f};
	}
	const Node &n = nodes_[node];
	// Exit distance through the cube along dir.
	float exit = std::numeric_limits<float>::max();
	const float o[3] = {p.x, p.y, p.z}, d[3] = {dir.x, dir.y, dir.z};
	const float lo[3] = {n.lo.x, n.lo.y, n.lo.z};
	for (int a = 0; a < 3; ++a) {
		if (d[a] > 1e-12f) {
			exit = std::min(exit, (lo[a] + n.size - o[a]) / d[a]);
		} else if (d[a] < -1e-12f) {
			exit = std::min(exit, (lo[a] - o[a]) / d[a]);
		}
	}
	const Leaf &leaf = leaves_[n.leaf];
	if (leaf.state == State::Empty && !want_distance) {
		return {gl::SDF_BIG, std::max(exit, 0.0f), State::Empty, leaf.lipschitz};
	}
	return {eval_leaf(body, leaf, p).d, std::max(exit, 0.0f), leaf.state, leaf.lipschitz};
}

Octree::Stats Octree::stats() const {
	Stats s;
	s.nodes = nodes_.size();
	for (const Node &n : nodes_) {
		if (n.leaf < 0) {
			continue;
		}
		const Leaf &l = leaves_[n.leaf];
		++s.leaves;
		s.tape_entries += l.tape.size();
		s.max_tape = std::max(s.max_tape, l.tape.size());
		if (l.state == State::Surface) {
			++s.surface_leaves;
			s.mean_surface_tape += double(l.tape.size());
		}
	}
	if (s.surface_leaves) {
		s.mean_surface_tape /= double(s.surface_leaves);
	}
	return s;
}

} // namespace sdf
