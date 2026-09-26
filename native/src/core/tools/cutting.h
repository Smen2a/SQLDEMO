#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "compile/octree.h"
#include "tools/tools.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// How wood takes an edge: a chisel's or gouge's stroke worked out against the work before
// it is made, so that a plan shows what the stroke will really do.
//
// - Resistance. A chip costs force in proportion to its cross-section, the wood's hardness
//   (Janka) and how the edge meets the fibres: least cutting along them, about 1.8 times
//   that across them with the edge along them, 4.5 times severing them (into end grain, or
//   chopping across the grain). Each side of the chip still attached to the work adds the
//   shear of a 1.5 mm strip, less along the grain in wood that splits readily. A skewed
//   edge slices, for up to 40% less. A hand pushes about 200 N (less on a flexible blade):
//   where that is not enough the cut stays shallower.
// - Clearance. Bevel down, an edge bites only when the blade is tipped at least 2 degrees
//   past its bevel; flatter, it rides on its bevel and skates. Once it bites it dives at the
//   difference until it levels at its depth. From an open face (an edge of the work or an
//   existing cut) the chip is free in front and the cut starts at its depth at once.
// - Gouges and V-tools with their corners out of the work take a chip whose sides are free;
//   buried, their sides tear.
// - Grain. Where the fibres run down into the wood ahead of the edge (against the grain,
//   uphill), the split running ahead follows them and tears out below the cut; with the
//   grain (downhill) it runs out to the surface and the cut is clean. Across the grain out
//   of an edge, a chip breaks away at the exit.
// - Chopping (the blade at 60 degrees or more): a mallet blow drives a thin slit a few
//   millimetres (less the deeper it is; a tap a third of a firm blow, a heavy one 1.6 times);
//   along the grain the wedge splits the wood. It hollows nothing out, except that within
//   reach of an open face on its bevel side the chip between pops off along the grain.
// - What it cannot do. The chip is everything between the floor and the surface above it:
//   where the surface steps up by a millimetre or more (a wall, a raised part) the edge
//   stops at its face; where the surface rises into a chip thicker than the hand can push,
//   the edge follows it up, but only gently (8 degrees): steeper, it stalls. So no cut runs
//   under the work. Nor does the blade pass through it: wood where the blade's body lies
//   behind the edge (an overhang, the walls of a gap narrower than the chisel) stops it too.
//   Where a rule stops a plan it says which (CutPlan::stop) and where (stop_at). The force
//   is the chip's own section over the edge (in a channel, only what stands over it).
// Chips (tear-out, breakout) are seeded, so a plan and the stroke that makes it agree. They
// follow the grain: splinters whose floor dips from the cut along the fibres and curls back
// up, rounded, never deeper than 1.5 times the cut (and a millimetre; breakout two).
namespace sdf::tools {

// How a wood takes an edge (materials.h); anything that is not wood is cut as ash.
struct Wood {
	float hardness = 5870.0f; // N (Janka)
	float split = 0.7f;
	float tearout = 0.5f;
	static Wood of(const Material &m);
};

// What a plan reads of the work (never changed).
struct Work {
	const Body &body;
	const Octree &octree;
	const MaterialTable &materials;

	float field(vec3 p) const { return octree.distance(body, p); }
	Wood wood(vec3 p) const;
	vec3 fibre() const; // unit, along the body's grain (either way)
};

// N/mm^2 of chip per N of Janka hardness, cutting along the grain.
constexpr float kCuttingResistance = 0.005f;

// 1 cutting along the fibres, 1.8 across them with the edge along them, 4.5 severing them;
// travel and edge are unit vectors.
float grain_factor(vec3 fibre, vec3 travel, vec3 edge);
// Force per mm^2 of chip cross-section (N/mm^2).
float resistance(const Wood &wood, vec3 fibre, vec3 travel, vec3 edge);

// What stands in a planned cut's way (CutPlan::warnings).
enum CutWarning : unsigned {
	kSkates = 1u << 0,        // below its bevel's clearance mid-face: it rides on its bevel
	kShallow = 1u << 1,       // the force it takes keeps it shallower than asked
	kTearOut = 1u << 2,       // against the grain: the split tears out below the cut
	kCornersBuried = 1u << 3, // a gouge's or V-tool's corners in the work: its sides tear
	kBreaksOut = 1u << 4,     // across the grain out of an edge: a chip breaks away
	kNotStruck = 1u << 5,     // a chop with a tool never struck: pushed by hand, it barely goes in
	kSlitOnly = 1u << 6,      // a chop far from any open face only makes a slit
	kPopsOff = 1u << 7,       // a chop near an open face: the chip between pops off
	kDigsIn = 1u << 8,        // dived in steeply: the edge digs in
	kSplits = 1u << 9,        // chopped along the grain: the wedge splits the wood
	// What stops a stroke short (CutPlan::stop):
	kBlocked = 1u << 10,      // the surface steps up ahead (a wall): chop it or come from the other side
	kBladeMeets = 1u << 11,   // wood where the blade's body goes (an overhang, a ridge)
	kTooWide = 1u << 12,      // the gap is narrower than the chisel: its sides meet the walls
	kStalls = 1u << 13,       // the chip grows thicker than the hand can push
};
// The warnings' names ("skates", "shallow", ...), in bit order.
std::vector<std::string> warning_names(unsigned warnings);

// A chisel's or gouge's stroke, worked out against the work.
struct CutPlan {
	Chisel chisel;
	bool chop = false;
	vec3 start{0.0f}, path{1, 0, 0}, normal{0, 0, 1}; // body space: the path unit, in the surface
	float width = 0.0f;       // swept width (a skewed flat edge sweeps less than its width)
	float length = 0.0f;      // mm along the path it cuts
	bool open = false;        // it starts at its depth (from an open face, or under a sole)
	float height = 0.0f;      // how far its section reaches above the floor (0: to above the plane)
	float lift_depth = -1.0f; // the depth it lifts out from (< 0: the floor's there)
	std::vector<vec2> floor;  // (distance along the path, depth) from 0 to length
	std::vector<Edit> chips;  // tear-out, breakout, a chop's pop-off
	std::vector<float> chips_at; // where along the path each chip is reached
	Edit slit;                // a chop's
	float depth = 0.0f;       // deepest level (a chop: the slit after this blow)
	float blow = 0.0f;        // a chop: how much deeper this blow goes
	float force = 0.0f;       // N the deepest part takes
	float available = 0.0f;   // N the hand gives
	float grain = 0.0f;       // 0 cutting along the fibres .. 1 severing them
	int slope = 0;            // +1 with the grain (downhill), -1 against (uphill), 0 level or across
	float split = 0.5f;       // how readily the wood splits (Wood::split)
	unsigned warnings = 0;
	// Where a rule stopped the stroke short: mm along the path (-1: it was not), which
	// (a CutWarning: blocked, blade meets, too wide, stalls, skates) and, blocked, how high
	// the step ahead stands (mm).
	float stop_at = -1.0f;
	unsigned stop = 0;
	float wall = 0.0f;

	float depth_at(float s) const;
	vec3 point(float s, float depth) const { return start + path * s - normal * depth; }
	// The cut as edits as far as `upto` along the path (a chop: all of it): the floor swept
	// with the edge's profile, the chips reached, and with `finished` the lift-out.
	std::vector<Edit> edits(float upto = 1e9f, bool finished = true) const;
	// Just the floor swept, as far as `upto` (a chop: its slit).
	std::vector<Edit> floor_edits(float upto = 1e9f) const;
	// How long a piece the shaving comes off in before it breaks: cut along the fibres it
	// holds together (a long ribbon); severing them it crumbles into short pieces, the
	// shorter the more readily the wood splits.
	float shaving_piece() const;
	// The edge lifting out forwards at `upto`, if it is in the work there.
	bool lift_out(float upto, Edit &out) const;
};

// Plans a stroke of `chisel` (held at its approach_deg, bevel down) from `start` on the
// work's surface (`normal` out of it) along `path` for `length` mm, `depth` deep at most,
// the edge turned `skew_deg` across the path by the hand (a skew chisel's own is added).
// At 60 degrees or more it is a chop at `start`: one blow (the waste on the path's side),
// `blow` times a firm one (0.3 a tap, 1.6 a heavy blow).
CutPlan plan_cut(const Chisel &chisel, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, float skew_deg, std::uint32_t seed, float blow = 1.0f);

// Tear-out below a pared cut going against the grain (uphill): seeded chips, `scale` times
// the wood's own readiness to tear (a plane's mouth keeps the split short: 0.5).
void add_tear_out(CutPlan &plan, const Work &work, float scale, std::uint32_t seed);

// A stroke making a plan: pushed along its path, never back and never past where it
// stops; a chop is one blow, made at once.
std::unique_ptr<Stroke> planned_stroke(const CutPlan &plan);

} // namespace sdf::tools
