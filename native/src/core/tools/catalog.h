#pragma once

#include "tools/tools.h"

#include <string>
#include <vector>

// The workshop's edge tools, by variant: what a woodworker's rack would hold. Each is a
// Chisel (tools.h) whose dimensions, bevel and handling follow the real tool:
//   bench chisels (bevel edge)  25-27 degree bevel, hand or light mallet: general paring and
//                               light chopping
//   paring chisel               long, thin and flexible, 20 degrees: fine paring by hand,
//                               never struck
//   mortise chisel              thick and rigid, 32 degrees: chopped with a heavy mallet
//   skew chisel                 its edge angled 30 degrees across the blade: slicing cuts
//   carving gouges              Sheffield sweeps: #3 shallow (smoothing), #7 scooping, #11
//                               a U-shaped veiner; a V-tool (60 degrees) for lines
// Sweep radii follow the Sheffield list at 12 mm: #3 about 1.8 widths, #7 about 0.6, #11
// half the width (a U).
namespace sdf::tools {

struct ChiselVariant {
	std::string id;     // e.g. "bench_12"
	std::string family; // "chisel" or "gouge"
	std::string label;  // e.g. "Bench chisel 12 mm"
	Chisel chisel;
};

const std::vector<ChiselVariant> &chisel_catalog();
// The variant with `id`, or null.
const ChiselVariant *find_chisel(const std::string &id);

} // namespace sdf::tools
