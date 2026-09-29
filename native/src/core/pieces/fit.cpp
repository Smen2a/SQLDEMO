#include "pieces/fit.h"

#include "util/parallel.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sdf {

namespace {

constexpr float kStep = 0.05f;        // mm: finite differences
constexpr std::size_t kChunk = 256;   // points a worker takes at a time
constexpr float kFacing = 0.9f;       // end on: the part's and the mate's normals this near opposite

vec3 gradient(const Distance &f, vec3 p) {
	const vec3 g(f(p + vec3(kStep, 0, 0)) - f(p - vec3(kStep, 0, 0)), f(p + vec3(0, kStep, 0)) - f(p - vec3(0, kStep, 0)),
			f(p + vec3(0, 0, kStep)) - f(p - vec3(0, 0, kStep)));
	const float l = gl::length(g);
	return l > 1e-12f ? g / l : vec3(0.0f);
}

} // namespace

std::vector<SurfacePoint> surface_points(const Distance &distance, const Aabb &region, float spacing) {
	const float h = std::max(spacing, 0.1f);
	const vec3 size = region.size();
	const int nx = int(std::floor(size.x / h)) + 1, ny = int(std::floor(size.y / h)) + 1,
			  nz = int(std::floor(size.z / h)) + 1;
	std::vector<std::vector<SurfacePoint>> slabs(std::size_t(std::max(nz, 0)));
	parallel_for(slabs.size(), [&](std::size_t k) {
		for (int j = 0; j < ny; ++j) {
			for (int i = 0; i < nx; ++i) {
				const vec3 p = region.lo + vec3(float(i), float(j), float(k)) * h;
				const float d = distance(p);
				if (std::fabs(d) > 0.5f * h) {
					continue;
				}
				// Onto the surface: two steps along the gradient.
				vec3 q = p;
				vec3 n(0.0f);
				float e = d;
				for (int step = 0; step < 2; ++step) {
					n = gradient(distance, q);
					if (gl::dot(n, n) == 0.0f) {
						break;
					}
					q = q - n * e;
					e = distance(q);
				}
				if (gl::dot(n, n) > 0.0f && std::fabs(e) < 0.05f) {
					slabs[k].push_back({q, n});
				}
			}
		}
	});
	std::vector<SurfacePoint> out;
	for (const auto &slab : slabs) {
		out.insert(out.end(), slab.begin(), slab.end());
	}
	return out;
}

Fit fit_along(const std::vector<SurfacePoint> &part, const Distance &mate, const Pose &start, vec3 axis, float travel,
		float step, const FitLimits &limits) {
	const auto begin = std::chrono::steady_clock::now();
	Fit fit;
	fit.points = int(part.size());
	const vec3 a = gl::normalize(axis);
	step = std::max(step, 0.05f);
	// Each point at the mouth, in the mate's space, and whether it faces along the way (an
	// end: a shoulder, the tenon's end) or across it (a side: a cheek).
	const std::size_t n = part.size();
	std::vector<vec3> at(n), normal(n);
	std::vector<std::uint8_t> end(n);
	for (std::size_t i = 0; i < n; ++i) {
		at[i] = start.apply(part[i].p);
		normal[i] = start.turn(part[i].n);
		end[i] = std::fabs(gl::dot(normal[i], a)) > limits.end ? 1 : 0;
	}
	const std::size_t chunks = (n + kChunk - 1) / kChunk;
	struct Partial {
		float interference = 0.0f;
		bool seated = false;
	};
	std::vector<Partial> partials(chunks);
	// A point of the part meets the mate end on where the mate there faces it: the mate's
	// normal square to the way and against the part's.
	auto end_on = [&](vec3 q, vec3 n_part) {
		const vec3 m = gradient(mate, q);
		return std::fabs(gl::dot(m, a)) > limits.end && gl::dot(m, n_part) < -kFacing;
	};
	// How far the mate's wood runs into the part's side at a point inside it, across the way:
	// out of the mate's wood the way its nearest surface lies, where that is across the way;
	// else (near the mouth, the nearest surface is the face the side is just under) back
	// along the side's own normal.
	auto crush = [&](vec3 q, vec3 n_part, float d) {
		const vec3 m = gradient(mate, q);
		const vec3 across = m - a * gl::dot(m, a);
		vec3 out = gl::length(across) > 0.5f ? gl::normalize(across) : n_part * -1.0f;
		out = gl::normalize(out - a * gl::dot(out, a));
		float s = 0.0f;
		for (int k = 0; k < 12 && d < 0.0f && s < 2.0f * limits.drive + limits.band; ++k) {
			s += std::max(-d, 0.01f);
			d = mate(q + out * s);
		}
		return s;
	};
	const int steps = std::max(1, int(std::ceil(travel / step - 1e-4f)));
	bool bound = false;
	for (int s = 0; s <= steps; ++s) {
		const float t = std::min(travel, float(s) * step);
		const vec3 move = a * t;
		const bool in = t >= std::min(limits.mouth, 0.5f * travel);
		parallel_for(chunks, [&](std::size_t c) {
			Partial r;
			const std::size_t lo = c * kChunk, hi = std::min(n, lo + kChunk);
			for (std::size_t i = lo; i < hi; ++i) {
				const vec3 q = at[i] + move;
				const float d = mate(q);
				if (end[i]) {
					r.seated = r.seated || (in && d < limits.seated && end_on(q, normal[i]));
				} else if (d < 0.0f) {
					r.interference = std::max(r.interference, crush(q, normal[i], d));
				}
			}
			partials[c] = r;
		});
		FitStep st;
		st.t = t;
		for (const Partial &r : partials) {
			st.interference = std::max(st.interference, r.interference);
			st.seated = st.seated || r.seated;
		}
		if (st.interference > limits.drive) {
			bound = true; // (it stops short of here)
			break;
		}
		fit.steps.push_back(st);
		fit.stops_at = t;
		fit.most = std::max(fit.most, st.interference);
		if (st.seated) {
			fit.seated = true;
			break;
		}
	}
	fit.home = !bound && fit.stops_at >= travel - 0.5f * step;
	// The clearance where it stops: its side's nearest approach to the mate's side (the
	// mate's surface there across the way, not a face it passes over end on).
	if (!fit.steps.empty()) {
		const vec3 move = a * fit.stops_at;
		std::vector<float> nearest(chunks, limits.band);
		parallel_for(chunks, [&](std::size_t c) {
			float r = limits.band;
			const std::size_t lo = c * kChunk, hi = std::min(n, lo + kChunk);
			for (std::size_t i = lo; i < hi; ++i) {
				if (end[i]) {
					continue;
				}
				const vec3 q = at[i] + move;
				const float d = mate(q);
				if (d >= 0.0f && d < r && std::fabs(gl::dot(gradient(mate, q), a)) <= limits.end) {
					r = d;
				}
			}
			nearest[c] = r;
		});
		fit.clearance = *std::min_element(nearest.begin(), nearest.end());
		fit.steps.back().clearance = fit.clearance;
	}
	if (!fit.home) {
		fit.kind = Fit::Kind::WontGo;
	} else if (fit.most > limits.snug) {
		fit.kind = Fit::Kind::Drives;
	} else if (fit.clearance > limits.loose) {
		fit.kind = Fit::Kind::Loose;
	} else {
		fit.kind = Fit::Kind::Snug;
	}
	fit.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
	return fit;
}

const char *fit_name(Fit::Kind kind) {
	switch (kind) {
		case Fit::Kind::Loose:
			return "loose";
		case Fit::Kind::Snug:
			return "snug";
		case Fit::Kind::Drives:
			return "drives";
		case Fit::Kind::WontGo:
			return "won't go";
	}
	return "";
}

} // namespace sdf
