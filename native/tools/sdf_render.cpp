// Renders a named demo body with the CPU reference renderer, optionally comparing the
// result with another image (a Godot screenshot, for GPU/CPU parity).
//
//   sdf_render DEMO OUT.png [--view shaded|normals|albedo] [--size WxH] [--eye x,y,z]
//              [--target x,y,z] [--fov deg] [--octree | --adf]
//              [--compare IMAGE.png [--threshold N] [--max-mean M] [--max-over F] [--diff DIFF.png]]
//
// DEMO is a name from sdf::demo::named_demo; the camera defaults to the demo's. By
// default it traces the formula field (every edit, in order); --octree traces the pruned
// tapes and --adf the Live display cache, as the Godot shaders do. With
// --compare, prints the difference and exits 1 when the mean channel difference exceeds
// M (0..255) or more than a fraction F of pixels differ by more than N in some channel.

#include "compile/octree.h"
#include "demo/gallery.h"
#include "io/png.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

bool parse_vec3(const char *s, sdf::vec3 &out) {
	return std::sscanf(s, "%f,%f,%f", &out.x, &out.y, &out.z) == 3;
}

int usage(const char *argv0) {
	std::fprintf(stderr,
			"usage: %s DEMO OUT.png [--view shaded|normals|albedo] [--size WxH] [--eye x,y,z] [--target x,y,z]\n"
			"       [--fov deg] [--octree | --adf] [--compare IMAGE.png [--threshold N] [--max-mean M] [--max-over F]\n"
			"       [--diff DIFF.png]]\n",
			argv0);
	return 2;
}

// Channel differences amplified 4x, so small mismatches are visible.
sdf::Image diff_image(const sdf::Image &a, const sdf::Image &b) {
	sdf::Image out(a.width, a.height);
	for (std::size_t i = 0; i < a.rgb.size(); ++i) {
		out.rgb[i] = std::uint8_t(std::min(255, 4 * std::abs(int(a.rgb[i]) - int(b.rgb[i]))));
	}
	return out;
}

} // namespace

int main(int argc, char **argv) {
	if (argc < 3) {
		return usage(argv[0]);
	}
	const std::string demo_name = argv[1], out_path = argv[2];
	sdf::Body body;
	sdf::Camera camera;
	if (!sdf::demo::named_demo(demo_name, body, camera)) {
		std::fprintf(stderr, "unknown demo '%s'\n", demo_name.c_str());
		return 2;
	}
	sdf::RenderSettings rs;
	rs.width = 1280;
	rs.height = 720;
	bool use_octree = false, use_adf = false;
	std::string compare_path, diff_path;
	int threshold = 24;
	double max_mean = 1.0, max_over = 0.005;
	for (int i = 3; i < argc; ++i) {
		const std::string arg = argv[i];
		const char *next = i + 1 < argc ? argv[i + 1] : nullptr;
		const bool has_value = next != nullptr;
		if (arg == "--octree") {
			use_octree = true;
		} else if (arg == "--adf") {
			use_octree = use_adf = true;
		} else if (!has_value) {
			return usage(argv[0]);
		} else if (arg == "--view") {
			if (std::strcmp(next, "normals") == 0) {
				rs.output = sdf::RenderSettings::Output::Normals;
			} else if (std::strcmp(next, "albedo") == 0) {
				rs.output = sdf::RenderSettings::Output::Albedo;
			} else if (std::strcmp(next, "shaded") != 0) {
				return usage(argv[0]);
			}
			++i;
		} else if (arg == "--size") {
			if (std::sscanf(next, "%dx%d", &rs.width, &rs.height) != 2) {
				return usage(argv[0]);
			}
			++i;
		} else if (arg == "--eye" || arg == "--target") {
			if (!parse_vec3(next, arg == "--eye" ? camera.eye : camera.target)) {
				return usage(argv[0]);
			}
			++i;
		} else if (arg == "--fov") {
			camera.fov_deg = float(std::atof(next));
			++i;
		} else if (arg == "--compare") {
			compare_path = argv[++i];
		} else if (arg == "--diff") {
			diff_path = argv[++i];
		} else if (arg == "--threshold") {
			threshold = std::atoi(argv[++i]);
		} else if (arg == "--max-mean") {
			max_mean = std::atof(argv[++i]);
		} else if (arg == "--max-over") {
			max_over = std::atof(argv[++i]);
		} else {
			return usage(argv[0]);
		}
	}

	sdf::Octree octree;
	sdf::Adf adf;
	if (use_octree) {
		octree.build(body);
	}
	if (use_adf) {
		adf.build(body, octree);
	}
	const auto start = std::chrono::steady_clock::now();
	const sdf::Image img = sdf::render(body, sdf::MaterialTable::standard(), camera, rs, use_octree ? &octree : nullptr,
			use_adf ? &adf : nullptr);
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	if (!sdf::write_png(out_path, img)) {
		std::fprintf(stderr, "could not write %s\n", out_path.c_str());
		return 1;
	}
	std::printf("%s %dx%d %.2fs -> %s\n", demo_name.c_str(), img.width, img.height, secs, out_path.c_str());
	if (compare_path.empty()) {
		return 0;
	}

	sdf::Image other;
	if (!sdf::read_png(compare_path, other)) {
		std::fprintf(stderr, "could not read %s\n", compare_path.c_str());
		return 1;
	}
	if (other.width != img.width || other.height != img.height) {
		std::fprintf(stderr, "size mismatch: %dx%d vs %dx%d\n", other.width, other.height, img.width, img.height);
		return 1;
	}
	const sdf::ImageDiff d = sdf::compare(img, other, threshold);
	if (!diff_path.empty()) {
		sdf::write_png(diff_path, diff_image(img, other));
	}
	const bool ok = d.mean_abs <= max_mean && d.fraction_over <= max_over;
	std::printf("%s vs %s: mean %.3f (max %.3f), over %d: %.4f%% (max %.4f%%), largest %d  %s\n", out_path.c_str(),
			compare_path.c_str(), d.mean_abs, max_mean, threshold, d.fraction_over * 100.0, max_over * 100.0, d.max_abs,
			ok ? "ok" : "FAIL");
	return ok ? 0 : 1;
}
