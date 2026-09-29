#pragma once

// A deliberately tiny test harness: TEST registers a function, CHECK records a failure
// without aborting the test, so one run reports every broken property.

#include "body/body.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace t {

struct Case {
	const char *name;
	void (*fn)();
};

std::vector<Case> &registry();
void fail(const char *file, int line, const std::string &what);

struct Register {
	Register(const char *name, void (*fn)()) { registry().push_back({name, fn}); }
};

// Deterministic randomness so a failure reproduces.
struct Rng {
	std::mt19937 gen;
	explicit Rng(unsigned seed) : gen(seed) {}
	float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen); }
	sdf::vec3 vec(float lo, float hi) { return {uniform(lo, hi), uniform(lo, hi), uniform(lo, hi)}; }
	sdf::vec3 unit() {
		for (;;) {
			sdf::vec3 v = vec(-1, 1);
			const float l = sdf::gl::length(v);
			if (l > 0.1f && l <= 1.0f) {
				return v / l;
			}
		}
	}
	sdf::vec4 rotation() { return sdf::quat_axis_angle(unit(), uniform(0, 6.2831853f)); }
};

// Largest |f(p + h dir) - f(p)| / h over random p in a cube of half-size `extent` around
// `centre`. A Lipschitz bound L must hold for every pair, so any sample above L is a
// genuine violation (up to float rounding, which h keeps small relative to the result).
inline float sampled_lipschitz(const std::function<float(sdf::vec3)> &f, sdf::vec3 centre, float extent,
		int samples, Rng &rng, float h = 1e-2f) {
	float worst = 0.0f;
	for (int i = 0; i < samples; ++i) {
		const sdf::vec3 p = centre + rng.vec(-extent, extent);
		const sdf::vec3 q = p + rng.unit() * h;
		worst = std::max(worst, std::fabs(f(q) - f(p)) / h);
	}
	return worst;
}

} // namespace t

#define TEST(name)                                                                         \
	static void name();                                                                    \
	static t::Register name##_register(#name, name);                                       \
	static void name()

#define CHECK(cond)                                                                        \
	do {                                                                                   \
		if (!(cond)) {                                                                     \
			t::fail(__FILE__, __LINE__, #cond);                                            \
		}                                                                                  \
	} while (0)

#define CHECK_NEAR(a, b, tol)                                                              \
	do {                                                                                   \
		const double a_ = (a), b_ = (b);                                                   \
		if (!(std::fabs(a_ - b_) <= (tol))) {                                              \
			t::fail(__FILE__, __LINE__,                                                    \
					std::string(#a " ~ " #b ": ") + std::to_string(a_) + " vs " + std::to_string(b_)); \
		}                                                                                  \
	} while (0)

#define CHECK_LE(a, b)                                                                     \
	do {                                                                                   \
		const double a_ = (a), b_ = (b);                                                   \
		if (!(a_ <= b_)) {                                                                 \
			t::fail(__FILE__, __LINE__,                                                    \
					std::string(#a " <= " #b ": ") + std::to_string(a_) + " vs " + std::to_string(b_)); \
		}                                                                                  \
	} while (0)
