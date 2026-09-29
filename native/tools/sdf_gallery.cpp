// Renders the demo galleries with the CPU reference renderer.
//   sdf_gallery OUT_DIR [scale] [samples_per_axis]

#include "demo/gallery.h"
#include "io/png.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s OUT_DIR [scale] [samples_per_axis]\n", argv[0]);
		return 2;
	}
	const std::string dir = argv[1];
	sdf::demo::GalleryOptions o;
	o.scale = argc > 2 ? std::atoi(argv[2]) : 1;
	o.samples_per_axis = argc > 3 ? std::atoi(argv[3]) : 1;

	struct Job {
		const char *name;
		sdf::Image (*fn)(const sdf::demo::GalleryOptions &);
	};
	const Job jobs[] = {
		{"blend_gallery", sdf::demo::blend_gallery},
		{"material_gallery", sdf::demo::material_gallery},
		{"carved_panel", sdf::demo::carved_panel},
	};
	for (const Job &j : jobs) {
		const auto start = std::chrono::steady_clock::now();
		const sdf::Image img = j.fn(o);
		const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		const std::string path = dir + "/" + j.name + ".png";
		if (!sdf::write_png(path, img)) {
			std::fprintf(stderr, "could not write %s\n", path.c_str());
			return 1;
		}
		std::printf("%-18s %4dx%-4d %6.2fs  %s\n", j.name, img.width, img.height, secs, path.c_str());
	}
	return 0;
}
