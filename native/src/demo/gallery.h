#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "eval/image.h"
#include "eval/reference_renderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sdf::demo {

struct GalleryOptions {
	int scale = 1;            // multiplies every pixel dimension
	int samples_per_axis = 1; // supersampling for final-quality images
	int threads = 0;
};

// One labelled panel of a gallery image.
struct Tile {
	std::string label;
	Body body;
	Camera camera;
};

// Every blend mode applied to the same union (a boss rising from a block: concave
// corner) and subtract (a chiselled channel: convex arrises).
std::vector<Tile> blend_tiles();
// Wood figure revealed by cutting, material mixing, a hard-edged inlay, stone and steel.
std::vector<Tile> material_tiles();
// A relief-carved ash panel: V-tool outlines, gouged petals, border groove.
Body carved_panel_body();
Camera carved_panel_camera();

// A plain board to work on, 160 x 100 x 25 mm, in the given wood.
Body board(std::uint16_t material);

// A demo body and a camera that frames it, by name, shared by the tools and the Godot node:
// "carved_panel", "blend_<i>" and "material_<i>" (gallery tiles), "sphere" (a walnut ball
// fluted with round-edged gouge cuts), "session" (an oak panel under 300 random strokes)
// and "board", "board_oak", "board_walnut" (blanks for the workshop). Returns false for an
// unknown name.
bool named_demo(const std::string &name, Body &body, Camera &camera);
// Random curved strokes (V-tool, gouge and flat in turn) into the top face of body's base.
// Some may be too tight for their tool, which Body::add rejects.
std::vector<Edit> random_strokes(const Body &body, int count, std::uint32_t seed);

Image blend_gallery(const GalleryOptions &o);
Image material_gallery(const GalleryOptions &o);
Image carved_panel(const GalleryOptions &o);

} // namespace sdf::demo
