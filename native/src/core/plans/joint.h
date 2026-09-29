#pragma once

#include "plans/part.h"
#include "util/pose.h"

#include <string>

// How two parts of a plan go together at a joint: which goes into which, how they lie once
// home, and the line it goes in along. A tenon goes into its mortise (a hole) shoulder
// first on the hole's face; a wedge into its kerf, thin end first, to the kerf's bottom.
namespace sdf::plans {

struct JointPose {
	enum class Kind : std::uint8_t { None, MortiseTenon, Wedge };
	Kind kind = Kind::None;
	// Whether a is the part that goes in (the tenon, the wedge); else b is.
	bool a_moves = false;
	// The moving part's space into its mate's, the moving part home.
	Pose home;
	// The way it goes in (the mate's space), and how far from the mouth (where its leading
	// end meets the mate's face) to home.
	vec3 axis{0.0f};
	float travel = 0.0f;
	// What of the moving part goes in, in its own space: its surface there is what fits.
	Aabb region;
	// A reflection of the moving part's space that maps what goes in onto itself (through
	// the middle of a tenon's section, square to its short side; of a wedge's width). Where
	// the parts were laid out mirrored, the pose that puts them together is a reflection;
	// with this after it, a turn that seats the same.
	Pose mirror;

	// The moving part at the mouth: `travel` short of home.
	Pose mouth() const { return home.shifted(axis * -travel); }
};

// The joint between feature `fa` of part a and `fb` of b (a hole and a tenon, a kerf and a
// taper, either way round); Kind::None for features that make no joint.
JointPose joint_pose(const Part &a, const Feature &fa, const Part &b, const Feature &fb);

// A part's feature by name, or null.
const Feature *feature_named(const Part &p, const std::string &name);

} // namespace sdf::plans
