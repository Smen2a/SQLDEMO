#include "body/region.h"

#include <algorithm>

namespace sdf {

Region::Region(std::vector<Node> nodes) : nodes_(std::move(nodes)) {
	if (nodes_.empty()) {
		return;
	}
	box_ = {nodes_[0].lo, nodes_[0].lo + vec3(nodes_[0].size)};
	// Children always follow their parents: fill the bits in from the back.
	for (std::size_t i = nodes_.size(); i-- > 0;) {
		Node &n = nodes_[i];
		if (n.child < 0) {
			n.labels = std::uint8_t(1u << n.label);
			continue;
		}
		n.labels = 0;
		for (int c = 0; c < 8; ++c) {
			n.labels |= nodes_[std::size_t(n.child + c)].labels;
		}
	}
}

Region::Label Region::label(vec3 p) const {
	if (nodes_.empty() || p.x < box_.lo.x || p.y < box_.lo.y || p.z < box_.lo.z || p.x > box_.hi.x || p.y > box_.hi.y ||
			p.z > box_.hi.z) {
		return Rest;
	}
	int node = 0;
	while (nodes_[std::size_t(node)].child >= 0) {
		const Node &n = nodes_[std::size_t(node)];
		const float half = n.size * 0.5f;
		node = n.child + int(p.x >= n.lo.x + half) + 2 * int(p.y >= n.lo.y + half) + 4 * int(p.z >= n.lo.z + half);
	}
	return nodes_[std::size_t(node)].label;
}

unsigned Region::labels(vec3 lo, float size) const {
	const vec3 hi = lo + vec3(size);
	if (nodes_.empty()) {
		return 1u << Rest;
	}
	unsigned bits = 0;
	if (lo.x < box_.lo.x || lo.y < box_.lo.y || lo.z < box_.lo.z || hi.x > box_.hi.x || hi.y > box_.hi.y ||
			hi.z > box_.hi.z) {
		bits |= 1u << Rest; // part of it lies beyond the root
	}
	return bits | labels(0, lo, hi);
}

unsigned Region::labels(int node, vec3 lo, vec3 hi) const {
	const Node &n = nodes_[std::size_t(node)];
	const vec3 nhi = n.lo + vec3(n.size);
	if (nhi.x < lo.x || nhi.y < lo.y || nhi.z < lo.z || n.lo.x > hi.x || n.lo.y > hi.y || n.lo.z > hi.z) {
		return 0;
	}
	if (n.child < 0 || (n.lo.x >= lo.x && n.lo.y >= lo.y && n.lo.z >= lo.z && nhi.x <= hi.x && nhi.y <= hi.y &&
								 nhi.z <= hi.z)) {
		return n.labels;
	}
	unsigned bits = 0;
	for (int c = 0; c < 8 && bits != n.labels; ++c) {
		bits |= labels(n.child + c, lo, hi);
	}
	return bits;
}

std::size_t Region::leaves() const {
	return std::size_t(std::count_if(nodes_.begin(), nodes_.end(), [](const Node &n) { return n.child < 0; }));
}

float Region::finest() const {
	float f = nodes_.empty() ? 0.0f : nodes_[0].size;
	for (const Node &n : nodes_) {
		f = n.child < 0 ? std::min(f, n.size) : f;
	}
	return f;
}

} // namespace sdf
