#include "test.h"

#include "demo/gallery.h"
#include "io/png.h"

#include <cstdlib>

using namespace sdf;

// Renders each demo scene with the CPU reference renderer and compares it with the
// committed image in tests/golden/. Set SDF_UPDATE_GOLDEN=1 to rewrite the references
// after an intended visual change (then look at them before committing).
//
// Tolerances absorb compiler and libm differences at silhouettes: a handful of pixels
// may differ strongly, but the image as a whole must not move.

namespace {

void check_golden(const char *name, const Image &img) {
	const std::string path = std::string(SDF_REPO_ROOT) + "/native/tests/golden/" + name + ".png";
	if (std::getenv("SDF_UPDATE_GOLDEN")) {
		CHECK(write_png(path, img));
		std::printf("    wrote %s\n", path.c_str());
		return;
	}
	Image ref;
	if (!read_png(path, ref)) {
		t::fail(__FILE__, __LINE__, std::string("missing golden ") + path + " (run with SDF_UPDATE_GOLDEN=1)");
		return;
	}
	const ImageDiff d = compare(img, ref, 24);
	std::printf("    %-16s mean %.3f  over %.4f%%  max %d\n", name, d.mean_abs, d.fraction_over * 100, d.max_abs);
	CHECK_LE(d.mean_abs, 0.5);
	CHECK_LE(d.fraction_over, 0.002);
	if (d.mean_abs > 0.5 || d.fraction_over > 0.002) {
		write_png(std::string(SDF_REPO_ROOT) + "/out/" + name + ".actual.png", img);
	}
}

} // namespace

TEST(golden_blend_gallery) {
	check_golden("blend_gallery", demo::blend_gallery({}));
}

TEST(golden_material_gallery) {
	check_golden("material_gallery", demo::material_gallery({}));
}

TEST(golden_carved_panel) {
	check_golden("carved_panel", demo::carved_panel({}));
}
