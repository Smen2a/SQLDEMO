#pragma once

#include "tools/cutting.h"
#include "tools/layout.h"
#include "tools/tools.h"

#include <functional>
#include <memory>
#include <vector>

// Faces rubbed over the work: a sanding block's, a rasp's, a card scraper's burr. Such a face
// is flat and stiff: it rests on the highest points under it and takes those down first,
// never reaching into a hollow narrower than itself. It takes material off only where it
// has been, in proportion to how long each point has been under it, and faster where it
// bears on less (the hand's pressure on a smaller area: a narrow edge, a bump). Body space,
// millimetres.
namespace sdf::tools {

// A rectangle in a plane, turned: [lo, hi] along `u` and across it (the unit u turned a
// quarter turn anticlockwise), from `origin`. Plane x, y.
struct Region {
	vec2 origin{0.0f}, u{1.0f, 0.0f};
	vec2 lo{0.0f}, hi{0.0f};

	vec2 local(vec2 p) const {
		const vec2 d = p - origin;
		return {d.x * u.x + d.y * u.y, -d.x * u.y + d.y * u.x};
	}
	vec2 plane(vec2 l) const { return origin + vec2(u.x * l.x - u.y * l.y, u.y * l.x + u.x * l.y); }
	bool contains(vec2 p) const {
		const vec2 l = local(p);
		return l.x >= lo.x && l.x <= hi.x && l.y >= lo.y && l.y <= hi.y;
	}
	float area() const { return (hi.x - lo.x) * (hi.y - lo.y); }
};

// The surface's height over a plane (plane z), seen along -z and sampled on a grid over a
// rectangle of the plane. Read from the work before a stroke (a stroke never reads the body
// as it goes: it may be changing on another thread), and lowered as the stroke cuts.
class HeightMap {
public:
	HeightMap() = default;
	// Over [lo, hi] (plane x, y), `step` mm apart (coarser if that takes more than `most`
	// samples either way).
	HeightMap(const Body &body, const Octree &octree, const Frame &plane, vec2 lo, vec2 hi, float step, int most = 128);

	// The highest point over a region (at least the sample nearest its middle); -inf where
	// there is no work.
	float highest(const Region &r) const;
	// The share of the samples over [lo, hi] (plane x, y; at least the nearest) at `level`
	// or above. Samples off the work bear on nothing.
	float share_above(vec2 lo, vec2 hi, float level) const;
	// The middle height of the work over a region (half the samples on the work are at or
	// above it); -inf where there is no work.
	float median(const Region &r) const;
	// Cut down to `floor` over a region.
	void lower(const Region &r, float floor);
	// The volume (mm^3) above the lowest of the floors over each sample: what cuts down to
	// those floors over those regions would take out of the surface as mapped.
	float removed(const std::vector<std::pair<Region, float>> &cuts) const;
	int samples() const { return nx_ * ny_; }

private:
	template <typename F> void over(const Region &r, F &&f) const;

	vec2 lo_{0.0f}, step_{1.0f};
	int nx_ = 0, ny_ = 0;
	std::vector<float> h_; // -inf: no work there
};

// A face and how it is rubbed.
struct RubFace {
	float length = 70.0f, width = 40.0f; // mm: along its stroke (x) and across it (y)
	// Depth taken per mm of cutting travel over a point that stays under the face, bearing
	// on all of it (this times the pace).
	float rate = 1e-5f;
	int cuts = 0;          // 0 both ways; +1 only moving +x (a push, its handle at -x); -1 only -x
	bool line = false;     // held to its line (y = 0), between `from` and `to` along it
	float from = 0.0f, to = 0.0f;
	float grain = 0.25f;   // mm: its dust's
	bool thrown = false;   // its dust is thrown the way it goes (a rasp's); else it spills from under it
	// The cut of a patch: [lo, hi] of `frame` (the plane, turned along the patch) taken down
	// to `floor` (frame z), `depth` mm below where the face rests there (the highest point)
	// and `typical` below most of the ground under it (0 when that is below the floor).
	std::function<Edit(const Frame &frame, vec2 lo, vec2 hi, float floor, float depth, float typical)> pass;
	// The area (mm^2) a line face's cut takes across its line `depth` deep, per mm along it
	// (a half-round face's is not width x depth); unset: the patch's width x depth.
	std::function<float(float depth)> section;
	// Marked lines it is held to (tools/layout.h): a patch cuts no deeper than the floors, and
	// only on the waste side of the sides and ends.
	Limits limits;
};

// The plane a flat face set on the work at `plane` is held in: turned to lie along the
// work's own face under it (`half` its size either way, plane x, y), the plane most of 45
// points there lie on (a bump, a groove or the rounded end of a board does not tip it; a
// normal taken from the pointer a degree or two off is put right). Only where 40% of the
// points lie on one plane, and by at most 10 degrees: where the work under it is curved,
// or it was set at an angle on purpose, it is left as it was set.
Frame settle(const Work &work, const Frame &plane, vec2 half);

// How far a free face's stroke looks round where it was set for the work it may reach (mm).
constexpr float kRubReach = 250.0f;
// Patches a stroke keeps apart; beyond, the two that make the most compact rectangle merge.
constexpr int kMostPatches = 12;

// A face set on the work at `plane` (its origin the contact, z out of the work, x the
// stroke's direction and the face's length), rubbed as move_to() takes it. Where it goes is
// kept as patches: rectangles of the ground its face covered, lying along the way it moved
// over them, each cut down below the highest point under it by what the rubbing there has
// taken. A patch grows while the face works back and forth along one line (drifting across
// it by up to half the face); turning off it, or onto higher ground once it has cut, starts
// another, which rests on the ground as the patches before it left it.
std::unique_ptr<Stroke> rub_stroke(const RubFace &face, const Work &work, const Frame &plane, float pace = 1.0f);

} // namespace sdf::tools
