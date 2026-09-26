#pragma once

#include "tools/cutting.h"
#include "tools/tools.h"

#include <cstdint>
#include <memory>

// Shaping and finishing tools: a rasp, a card scraper and a spokeshave.
// - A rasp's teeth each take a tiny bite, so it never tears the grain whichever way it
//   goes; it removes steadily, faster the coarser it is and the harder it is pressed, and
//   slower in harder wood. Its flat face lowers what it is rubbed over (tilted about its
//   stroke, it takes a chamfer off an arris); its half-round face hollows.
// - A card scraper, flexed, takes a whisper (about a hundredth of a millimetre a stroke)
//   and cannot tear out: it is for cleaning up tear-out and tool marks.
// - A spokeshave is a plane with a 40 mm sole. Its sole rests on the work and its blade
//   takes a shaving of the depth it is set to below it: an even shaving on a flat face
//   (where a chisel struggles), following convex curves, bridging hollows shorter than
//   its sole. It is pushed by two hands, and tears out against the grain as a chisel does
//   (less: its mouth keeps the split short).
// Rasps and scrapers work back and forth along the line they were set on, as the saw does:
// every millimetre of travel deepens the pass over the planned length.
namespace sdf::tools {

struct Rasp {
	float length = 200.0f, width = 25.0f, thickness = 6.0f;
	float coarseness = 0.5f; // 1 a wood rasp, 0.5 a cabinet rasp, 0.25 a patternmaker's
	bool round = false;      // working its half-round face (which hollows), not its flat one
	float round_radius = 22.0f;
	float pressure = 1.0f;   // 1: an ordinary hand's worth
	float tilt_deg = 0.0f;   // the face turned about the stroke's line (a chamfer off an arris)

	Body model() const;
	// Depth taken per millimetre of travel in `wood`.
	float removal_per_mm(const Wood &wood) const;
	// The face's cross-section, `height` above its deepest point.
	ToolProfile profile(float height) const;
};

struct CardScraper {
	float width = 60.0f, height = 100.0f, thickness = 0.8f;
	float pressure = 1.0f;
	float feather = 4.0f; // flexed, its cut fades out over this at each side

	Body model() const;
	float removal_per_mm(const Wood &wood) const;
};

struct Spokeshave {
	float blade_width = 50.0f;
	float sole = 40.0f;         // its sole's length, the edge in the middle
	float bed_deg = 45.0f;      // the blade's bed (how steeply it lifts out)
	float hand_force = 250.0f;  // N, two hands

	Body model() const;
};

// A rasp or scraper set on the work at `contact` and worked back and forth along `path`
// over `length` mm from there.
std::unique_ptr<Stroke> rasp_stroke(const Rasp &rasp, const Wood &wood, vec3 contact, vec3 normal, vec3 path,
		float length);
std::unique_ptr<Stroke> scraper_stroke(const CardScraper &scraper, const Wood &wood, vec3 contact, vec3 normal,
		vec3 path, float length);

// A spokeshave's pass along `path` for `length` mm from `start`, its blade set `depth` mm
// below its sole: the floor its sole's rest leaves, the force two hands can put behind it,
// tear-out against the grain. Made with planned_stroke() (cutting.h).
CutPlan plan_spokeshave(const Spokeshave &shave, const Work &work, vec3 start, vec3 normal, vec3 path, float length,
		float depth, std::uint32_t seed);

} // namespace sdf::tools
