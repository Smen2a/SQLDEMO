#pragma once

#include "tools/cutting.h"
#include "tools/layout.h"
#include "tools/tools.h"

#include <cstdint>
#include <memory>

// Shaping and finishing tools: a rasp, a card scraper, a spokeshave and the planes.
// - A rasp's teeth each take a tiny bite on the push, so it never tears the grain; it
//   removes steadily, faster the coarser it is and the harder it is pressed, and slower in
//   harder wood. Its face is stiff: it rests on the highest points under it and takes them
//   down first, only where it has been, faster where it bears on less (an edge, a bump:
//   tools/rubbing.h). Its flat face lowers what it is rubbed over (tilted about its stroke,
//   it takes a chamfer off an arris); its half-round face hollows.
// - A card scraper, flexed and pushed, takes a whisper: a hundredth of a millimetre from
//   each point its burr passes over, however long the stroke, and it cannot tear out. It is
//   for cleaning up tear-out and tool marks. Its burr is a line across the stroke, resting
//   on what stands highest under it (tools/rubbing.h).
// - A spokeshave is a plane with a 40 mm sole. Its sole rests on the work and its blade
//   takes a shaving of the depth it is set to below it: an even shaving on a flat face
//   (where a chisel struggles), following convex curves, bridging hollows shorter than
//   its sole, riding on the highest part across its blade. It is pushed by two hands, as
//   deep as they can push the chip's own section (deeper on a narrow edge), and no deeper
//   than its mouth passes. Its toe stops at a rise it cannot ride (a step of a millimetre
//   within two). It tears out against the grain as a chisel does (less: its mouth keeps
//   the split short).
// - The planes are spokeshaves with longer soles (catalog.h): a block plane (a 150 mm sole,
//   its iron narrower than the sole) takes a full-width shaving and bridges hollows, and
//   its sole's sides keep its iron off a wall; with a chamfer fence it is held across an
//   arris as it was set (at 45 degrees, or on the plane between two gauge lines) rather
//   than settled flat, and each pass widens the chamfer evenly; a shoulder plane's iron
//   runs flush with its sides, so it cuts right into a rebate's inside corner.
// Rasps and scrapers work back and forth along the line they were set on, as the saw does.
namespace sdf::tools {

struct Rasp {
	float length = 200.0f, width = 25.0f, thickness = 6.0f;
	float coarseness = 0.5f; // 1 a wood rasp, 0.5 a cabinet rasp, 0.25 a patternmaker's
	bool round = false;      // working its half-round face (which hollows), not its flat one
	float round_radius = 22.0f;
	float pressure = 1.0f;   // 1: an ordinary hand's worth
	float tilt_deg = 0.0f;   // the face turned about the stroke's line (a chamfer off an arris)

	Body model() const;
	// Depth taken per millimetre pushed over a point in `wood`, bearing with all its face:
	// 3.35e-4 mm for a cabinet rasp in oak (a 150 mm push takes 0.05 mm), in proportion
	// to its coarseness and the pressure, less in harder wood.
	float removal_per_mm(const Wood &wood) const;
	// The face's cross-section, `height` above its deepest point.
	ToolProfile profile(float height) const;
};

struct CardScraper {
	float width = 60.0f, height = 100.0f, thickness = 0.8f;
	float pressure = 1.0f;
	float feather = 4.0f; // flexed, its cut fades out over this at each side

	Body model() const;
	// Depth taken from a point each time the burr is pushed over it: 0.01 mm in oak, in
	// proportion to the pressure, less in harder wood.
	float per_pass(const Wood &wood) const;
};

// A spokeshave, or one of the planes: the same tool with another sole.
struct Spokeshave {
	enum class Kind { Spokeshave, BlockPlane, ShoulderPlane }; // (its model)
	Kind kind = Kind::Spokeshave;
	float blade_width = 50.0f;
	float sole = 40.0f;         // its sole's length, the edge in the middle
	float sole_width = 50.0f;   // its sole across, the blade in the middle (a spokeshave's bears
	                            // where its blade is; a block plane's is wider than its iron; a
	                            // shoulder plane's no wider)
	float bed_deg = 45.0f;      // the blade's bed (how steeply it lifts out)
	float hand_force = 250.0f;  // N, two hands
	float mouth = 0.8f;         // mm: the thickest shaving its mouth passes
	bool fence = false;         // a chamfer fence: held across an arris as it is set, not settled

	Body model() const;
};

// A rasp or scraper set on the work at `contact` and worked back and forth along `path`
// over `length` mm from there, cutting on the push (along `path`; the handle is behind),
// at its rate times the pace (tools/rubbing.h). Reads the work (its shape under the line,
// its wood) when it is set.
std::unique_ptr<Stroke> rasp_stroke(const Rasp &rasp, const Work &work, vec3 contact, vec3 normal, vec3 path,
		float length, float pace = 1.0f, const Limits &limits = {});
std::unique_ptr<Stroke> scraper_stroke(const CardScraper &scraper, const Work &work, vec3 contact, vec3 normal,
		vec3 path, float length, float pace = 1.0f, const Limits &limits = {});

// A plane is started with its iron over the near end of the work: set on within this (mm)
// of that end, its pass begins there.
constexpr float kPlaneStart = 20.0f;

// A spokeshave's or plane's pass along `path` for `length` mm from `start`, its blade set
// `depth` mm below its sole (held flat on the work: tools/rubbing.h settle(); with a fence,
// as it was set): the floor its sole's rest leaves (the highest across the sole), the force two hands can put behind the chip, where its toe is stopped (and
// why), tear-out against the grain. Made with planned_stroke() (cutting.h).
// `continuing`: it goes on from a pass steered its way (steered_stroke(): `start` over the
// edge): not started again at the work's end.
CutPlan plan_spokeshave(const Spokeshave &shave, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, std::uint32_t seed, bool continuing = false);

} // namespace sdf::tools
