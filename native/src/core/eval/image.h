#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sdf {

// 8-bit sRGB, row-major, no padding.
struct Image {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgb;

	Image() = default;
	Image(int w, int h, std::uint8_t fill = 0) : width(w), height(h), rgb(std::size_t(w) * h * 3, fill) {}
	std::uint8_t *at(int x, int y) { return &rgb[(std::size_t(y) * width + x) * 3]; }
	const std::uint8_t *at(int x, int y) const { return &rgb[(std::size_t(y) * width + x) * 3]; }
};

void blit(Image &dst, const Image &src, int x, int y);
void fill_rect(Image &img, int x, int y, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b);
// Draws text in a 5x7 pixel font magnified by `scale`. Supports A-Z, 0-9 and " :.-+/()".
void draw_text(Image &img, int x, int y, const std::string &text, int scale, std::uint8_t r, std::uint8_t g,
		std::uint8_t b);
int text_width(const std::string &text, int scale);

struct ImageDiff {
	double mean_abs = 0;      // mean absolute channel difference, 0..255
	double fraction_over = 0; // fraction of pixels whose largest channel difference exceeds the threshold
	int max_abs = 0;
};
ImageDiff compare(const Image &a, const Image &b, int threshold);

} // namespace sdf
