#include "eval/image.h"

#include <algorithm>
#include <cstdlib>

namespace sdf {

namespace {

// 5x7 glyphs, one row per entry, bit 4 = leftmost column.
struct Glyph {
	char c;
	std::uint8_t rows[7];
};

const Glyph kFont[] = {
	{'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
	{'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
	{'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
	{'J', {7, 2, 2, 2, 2, 18, 12}},      {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
	{'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 17, 25, 21, 19, 17, 17}}, {'O', {14, 17, 17, 17, 17, 17, 14}},
	{'P', {30, 17, 17, 30, 16, 16, 16}}, {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
	{'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}},
	{'V', {17, 17, 17, 17, 17, 10, 4}},  {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
	{'Y', {17, 17, 17, 10, 4, 4, 4}},    {'Z', {31, 1, 2, 4, 8, 16, 31}},     {'0', {14, 17, 19, 21, 25, 17, 14}},
	{'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {31, 2, 4, 2, 1, 17, 14}},
	{'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},   {'6', {6, 8, 16, 30, 17, 17, 14}},
	{'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},
	{':', {0, 12, 12, 0, 12, 12, 0}},    {'.', {0, 0, 0, 0, 0, 12, 12}},      {'-', {0, 0, 0, 31, 0, 0, 0}},
	{'+', {0, 4, 4, 31, 4, 4, 0}},       {'/', {0, 1, 2, 4, 8, 16, 0}},       {'(', {2, 4, 8, 8, 8, 4, 2}},
	{')', {8, 4, 2, 2, 2, 4, 8}},        {' ', {0, 0, 0, 0, 0, 0, 0}},
};

const Glyph *find_glyph(char c) {
	for (const Glyph &g : kFont) {
		if (g.c == c) {
			return &g;
		}
	}
	return nullptr;
}

} // namespace

void blit(Image &dst, const Image &src, int x0, int y0) {
	for (int y = 0; y < src.height; ++y) {
		for (int x = 0; x < src.width; ++x) {
			const int dx = x0 + x, dy = y0 + y;
			if (dx >= 0 && dy >= 0 && dx < dst.width && dy < dst.height) {
				std::copy_n(src.at(x, y), 3, dst.at(dx, dy));
			}
		}
	}
}

void fill_rect(Image &img, int x0, int y0, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
	for (int y = std::max(y0, 0); y < std::min(y0 + h, img.height); ++y) {
		for (int x = std::max(x0, 0); x < std::min(x0 + w, img.width); ++x) {
			std::uint8_t *p = img.at(x, y);
			p[0] = r;
			p[1] = g;
			p[2] = b;
		}
	}
}

int text_width(const std::string &text, int scale) {
	return text.empty() ? 0 : int(text.size()) * 6 * scale - scale;
}

void draw_text(Image &img, int x, int y, const std::string &text, int scale, std::uint8_t r, std::uint8_t g,
		std::uint8_t b) {
	for (char c : text) {
		if (const Glyph *glyph = find_glyph(c)) {
			for (int row = 0; row < 7; ++row) {
				for (int col = 0; col < 5; ++col) {
					if (glyph->rows[row] & (16 >> col)) {
						fill_rect(img, x + col * scale, y + row * scale, scale, scale, r, g, b);
					}
				}
			}
		}
		x += 6 * scale;
	}
}

ImageDiff compare(const Image &a, const Image &b, int threshold) {
	ImageDiff d;
	if (a.width != b.width || a.height != b.height) {
		d.mean_abs = 255;
		d.fraction_over = 1;
		d.max_abs = 255;
		return d;
	}
	std::size_t over = 0;
	double sum = 0;
	const std::size_t pixels = std::size_t(a.width) * a.height;
	for (std::size_t i = 0; i < pixels; ++i) {
		int worst = 0;
		for (int c = 0; c < 3; ++c) {
			const int diff = std::abs(int(a.rgb[i * 3 + c]) - int(b.rgb[i * 3 + c]));
			sum += diff;
			worst = std::max(worst, diff);
		}
		d.max_abs = std::max(d.max_abs, worst);
		over += worst > threshold;
	}
	d.mean_abs = sum / double(pixels * 3);
	d.fraction_over = double(over) / double(pixels);
	return d;
}

} // namespace sdf
