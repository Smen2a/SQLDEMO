#pragma once

#include "body/body.h"

#include <cstdint>
#include <vector>

namespace sdf {

// Where a piece that came away lies, for cutting it out of the body it came from, and the
// body out of it: an octree of cubes, each the island's (its material and the air close to
// it), the rest's (everything else's material, and the air close to that), or free (air,
// clear of both). Beyond the octree's cube everything is the rest's. Island and rest cubes
// never touch: every path from one to the other crosses free air, which is what makes the
// two genuinely apart (pieces/parts.h builds one, and proves that as it does).
//
// An Edit with Op::Keep applies one: the body keeps one side and drops the other. Where it
// drops, the field becomes max(d, kFloor - d): at least kFloor / 2, so that no surface is
// left there, and the further from zero the deeper the dropped material was (rays through
// it stay fast). Where it keeps, and in free air, the field is untouched. Kept and dropped
// cubes only meet free ones, and free cubes are built where the field is at least
// kFloor / 2, where max(d, kFloor - d) = d: the field stays continuous.
class Region {
public:
	enum Label : std::uint8_t { Free = 0, Island = 1, Rest = 2 };
	static constexpr float kFloor = 0.1f;

	struct Node {
		vec3 lo;
		float size = 0.0f;
		std::int32_t child = -1; // first of 8 consecutive children (child c at corner c), or -1
		Label label = Free;      // a leaf's
		std::uint8_t labels = 0; // bits (1 << label) of the leaves at and below it
	};

	// From a tree whose leaves are labelled (node 0 the root); fills in `labels`.
	explicit Region(std::vector<Node> nodes);

	Label label(vec3 p) const;
	// Bits (1 << label) of the labels in the cube (lo, size), the rest's beyond the root.
	unsigned labels(vec3 lo, float size) const;
	// The field after keeping `side` (Island or Rest) and dropping the other's.
	float apply(vec3 p, float d, Label side) const {
		const Label l = label(p);
		return l != Free && l != side ? std::max(d, kFloor - d) : d;
	}
	// The root cube's bounds.
	const Aabb &box() const { return box_; }
	const std::vector<Node> &nodes() const { return nodes_; }
	std::size_t leaves() const;
	float finest() const; // the smallest leaf's size

private:
	unsigned labels(int node, vec3 lo, vec3 hi) const;

	std::vector<Node> nodes_;
	Aabb box_;
};

} // namespace sdf
