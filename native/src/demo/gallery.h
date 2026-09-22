#pragma once

#include "body/body.h"
#include "body/materials.h"
#include "eval/image.h"
#include "eval/reference_renderer.h"

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

Image blend_gallery(const GalleryOptions &o);
Image material_gallery(const GalleryOptions &o);
Image carved_panel(const GalleryOptions &o);

} // namespace sdf::demo
