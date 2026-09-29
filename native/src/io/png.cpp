#include "io/png.h"

#include <zlib.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace sdf {

namespace {

void put_u32(std::vector<std::uint8_t> &out, std::uint32_t v) {
	out.push_back(std::uint8_t(v >> 24));
	out.push_back(std::uint8_t(v >> 16));
	out.push_back(std::uint8_t(v >> 8));
	out.push_back(std::uint8_t(v));
}

std::uint32_t get_u32(const std::uint8_t *p) {
	return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put_chunk(std::vector<std::uint8_t> &out, const char type[5], const std::vector<std::uint8_t> &data) {
	put_u32(out, std::uint32_t(data.size()));
	const std::size_t start = out.size();
	out.insert(out.end(), type, type + 4);
	out.insert(out.end(), data.begin(), data.end());
	put_u32(out, std::uint32_t(crc32(0, out.data() + start, uInt(out.size() - start))));
}

int paeth(int a, int b, int c) {
	const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
	return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

} // namespace

bool write_png(const std::string &path, const Image &img) {
	const std::size_t stride = std::size_t(img.width) * 3;
	std::vector<std::uint8_t> raw;
	raw.reserve((stride + 1) * img.height);
	for (int y = 0; y < img.height; ++y) {
		raw.push_back(0); // filter: none
		raw.insert(raw.end(), img.at(0, y), img.at(0, y) + stride);
	}
	uLongf packed_size = compressBound(uLong(raw.size()));
	std::vector<std::uint8_t> packed(packed_size);
	if (compress2(packed.data(), &packed_size, raw.data(), uLong(raw.size()), 9) != Z_OK) {
		return false;
	}
	packed.resize(packed_size);

	std::vector<std::uint8_t> file = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	std::vector<std::uint8_t> header;
	put_u32(header, std::uint32_t(img.width));
	put_u32(header, std::uint32_t(img.height));
	header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit, truecolour, deflate, adaptive, no interlace
	put_chunk(file, "IHDR", header);
	put_chunk(file, "IDAT", packed);
	put_chunk(file, "IEND", {});

	std::ofstream f(path, std::ios::binary);
	f.write(reinterpret_cast<const char *>(file.data()), std::streamsize(file.size()));
	return bool(f);
}

bool read_png(const std::string &path, Image &out) {
	std::ifstream f(path, std::ios::binary);
	const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (file.size() < 8 || std::memcmp(file.data(), "\x89PNG\r\n\x1a\n", 8) != 0) {
		return false;
	}
	int width = 0, height = 0, channels = 0;
	std::vector<std::uint8_t> packed;
	for (std::size_t pos = 8; pos + 12 <= file.size();) {
		const std::uint32_t len = get_u32(&file[pos]);
		const char *type = reinterpret_cast<const char *>(&file[pos + 4]);
		const std::uint8_t *data = &file[pos + 8];
		if (pos + 12 + len > file.size()) {
			return false;
		}
		if (std::memcmp(type, "IHDR", 4) == 0) {
			width = int(get_u32(data));
			height = int(get_u32(data + 4));
			if (data[8] != 8 || data[12] != 0 || (data[9] != 2 && data[9] != 6)) {
				return false;
			}
			channels = data[9] == 2 ? 3 : 4;
		} else if (std::memcmp(type, "IDAT", 4) == 0) {
			packed.insert(packed.end(), data, data + len);
		}
		pos += 12 + len;
	}
	if (!width || !height) {
		return false;
	}
	const std::size_t stride = std::size_t(width) * channels;
	uLongf raw_size = uLongf((stride + 1) * height);
	std::vector<std::uint8_t> raw(raw_size);
	if (uncompress(raw.data(), &raw_size, packed.data(), uLong(packed.size())) != Z_OK) {
		return false;
	}
	std::vector<std::uint8_t> prev(stride, 0), cur(stride);
	out = Image(width, height);
	for (int y = 0; y < height; ++y) {
		const std::uint8_t filter = raw[y * (stride + 1)];
		const std::uint8_t *line = &raw[y * (stride + 1) + 1];
		for (std::size_t i = 0; i < stride; ++i) {
			const int a = i >= std::size_t(channels) ? cur[i - channels] : 0;
			const int b = prev[i];
			const int c = i >= std::size_t(channels) ? prev[i - channels] : 0;
			int pred = 0;
			switch (filter) {
				case 1: pred = a; break;
				case 2: pred = b; break;
				case 3: pred = (a + b) / 2; break;
				case 4: pred = paeth(a, b, c); break;
				default: break;
			}
			cur[i] = std::uint8_t(line[i] + pred);
		}
		for (int x = 0; x < width; ++x) {
			std::memcpy(out.at(x, y), &cur[std::size_t(x) * channels], 3);
		}
		prev.swap(cur);
	}
	return true;
}

} // namespace sdf
