#pragma once

#include <cstdint>
#include <cstring>

namespace sdf {

// IEEE 754 binary16, as GPUs store FORMAT_RH / R16F texels. Rounds to nearest, ties to even.
inline std::uint16_t float_to_half(float f) {
	std::uint32_t x;
	std::memcpy(&x, &f, sizeof x);
	const std::uint32_t sign = (x >> 16) & 0x8000u;
	const std::uint32_t mag = x & 0x7FFFFFFFu;
	if (mag >= 0x7F800000u) { // inf, nan
		return std::uint16_t(sign | 0x7C00u | (mag > 0x7F800000u ? 0x200u : 0u));
	}
	if (mag >= 0x477FF000u) { // rounds past the largest half
		return std::uint16_t(sign | 0x7C00u);
	}
	if (mag < 0x38800000u) { // below 2^-14: subnormal half, or zero
		if (mag < 0x33000000u) {
			return std::uint16_t(sign);
		}
		const std::uint32_t mant = (mag & 0x7FFFFFu) | 0x800000u;
		const int shift = 126 - int(mag >> 23);
		std::uint32_t h = mant >> shift;
		const std::uint32_t rem = mant & ((1u << shift) - 1u), halfway = 1u << (shift - 1);
		if (rem > halfway || (rem == halfway && (h & 1u))) {
			++h;
		}
		return std::uint16_t(sign | h);
	}
	std::uint32_t h = (mag - 0x38000000u) >> 13;
	const std::uint32_t rem = mag & 0x1FFFu;
	if (rem > 0x1000u || (rem == 0x1000u && (h & 1u))) {
		++h;
	}
	return std::uint16_t(sign | h);
}

inline float half_to_float(std::uint16_t h) {
	const std::uint32_t sign = std::uint32_t(h & 0x8000u) << 16;
	const std::uint32_t e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
	std::uint32_t bits;
	if (e == 0) {
		const float f = float(m) * (1.0f / 16777216.0f); // m * 2^-24
		return sign ? -f : f;
	}
	if (e == 31) {
		bits = sign | 0x7F800000u | (m << 13);
	} else {
		bits = sign | ((e + 112u) << 23) | (m << 13);
	}
	float f;
	std::memcpy(&f, &bits, sizeof f);
	return f;
}

} // namespace sdf
