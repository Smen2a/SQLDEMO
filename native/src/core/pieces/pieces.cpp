#include "pieces/pieces.h"

#include "util/half.h"
#include "util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf {

namespace {

constexpr float kSqrt2 = 1.4142136f;
constexpr int kN = Adf::kSide, kCells = kN - 1;

int index(int x, int y, int z) {
	return x + kN * (y + kN * z);
}

vec3 corner(const Aabb &box, int c) {
	return vec3(c & 1 ? box.hi.x : box.lo.x, c & 2 ? box.hi.y : box.lo.y, c & 4 ? box.hi.z : box.lo.z);
}

} // namespace

Edit Plane::keep_behind() const {
	Edit e;
	e.prim = Primitive::plane(normal, gl::dot(point, normal));
	e.op = Op::Intersect;
	e.blend = Blend::Hard;
	return e;
}

bool plane_clear(const Body &body, const Octree &octree, const Plane &plane, const Aabb &box, float finest,
		std::size_t *samples) {
	if (box.empty()) {
		return true;
	}
	// Axes in the plane, and the part of it the box can reach.
	const vec3 n = plane.normal;
	const vec3 u = gl::normalize(gl::cross(n, std::fabs(n.x) < 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0)));
	const vec3 v = gl::cross(n, u);
	float u0 = std::numeric_limits<float>::max(), u1 = -u0, v0 = u0, v1 = -u0;
	for (int c = 0; c < 8; ++c) {
		const vec3 rel = corner(box, c) - plane.point;
		u0 = std::min(u0, gl::dot(rel, u));
		u1 = std::max(u1, gl::dot(rel, u));
		v0 = std::min(v0, gl::dot(rel, v));
		v1 = std::max(v1, gl::dot(rel, v));
	}
	const float lipschitz = std::max(body.lipschitz(), 1.0f);
	struct Square {
		float u, v, half;
	};
	std::vector<Square> stack{{0.5f * (u0 + u1), 0.5f * (v0 + v1), 0.5f * std::max(u1 - u0, v1 - v0)}};
	std::size_t count = 0;
	bool clear = true;
	while (!stack.empty() && clear) {
		const Square s = stack.back();
		stack.pop_back();
		const vec3 p = plane.point + u * s.u + v * s.v;
		// Squares that miss the box hold no material.
		const vec3 reach = vec3(std::fabs(u.x) + std::fabs(v.x), std::fabs(u.y) + std::fabs(v.y),
				std::fabs(u.z) + std::fabs(v.z)) * s.half;
		if (!Aabb{p - reach, p + reach}.overlaps(box)) {
			continue;
		}
		const float d = octree.sample(body, p).d;
		++count;
		if (d <= 0.0f) {
			clear = false; // material on the plane
		} else if (d <= lipschitz * s.half * kSqrt2) {
			if (s.half < finest) {
				clear = false; // too thin to tell: take the parts as still joined
			} else {
				const float q = 0.5f * s.half;
				stack.push_back({s.u - q, s.v - q, q});
				stack.push_back({s.u + q, s.v - q, q});
				stack.push_back({s.u - q, s.v + q, q});
				stack.push_back({s.u + q, s.v + q, q});
			}
		}
	}
	if (samples) {
		*samples = count;
	}
	return clear;
}

Aabb clip_box(const Aabb &box, const Plane &plane) {
	Aabb out;
	if (box.empty()) {
		return out;
	}
	float d[8];
	for (int c = 0; c < 8; ++c) {
		d[c] = plane.distance(corner(box, c));
		if (d[c] <= 0.0f) {
			out.include(corner(box, c));
		}
	}
	// Where the plane crosses the box's edges.
	for (int c = 0; c < 8; ++c) {
		for (int axis = 1; axis < 8; axis <<= 1) {
			const int o = c | axis;
			if (o == c || (d[c] <= 0.0f) == (d[o] <= 0.0f)) {
				continue;
			}
			const float t = d[c] / (d[c] - d[o]);
			out.include(corner(box, c) + (corner(box, o) - corner(box, c)) * t);
		}
	}
	return out;
}

double volume(const Adf &adf, const Plane *plane, vec3 *centroid) {
	const std::vector<Adf::Node> &nodes = adf.nodes();
	const std::vector<std::uint16_t> &values = adf.brick_values();
	double total = 0.0, moment[3] = {0.0, 0.0, 0.0};
	auto add = [&](vec3 centre, double v) {
		total += v;
		moment[0] += v * centre.x;
		moment[1] += v * centre.y;
		moment[2] += v * centre.z;
	};
	for (const Adf::Node &n : nodes) {
		if (n.child >= 0) {
			continue;
		}
		const vec3 middle = n.lo + vec3(n.size * 0.5f);
		if (n.brick == Adf::kSolid) {
			if (!plane) {
				add(middle, double(n.size) * n.size * n.size);
				continue;
			}
			float lo = std::numeric_limits<float>::max(), hi = -lo;
			for (int c = 0; c < 8; ++c) {
				const float d = plane->distance(corner({n.lo, n.lo + vec3(n.size)}, c));
				lo = std::min(lo, d);
				hi = std::max(hi, d);
			}
			if (hi <= 0.0f) {
				add(middle, double(n.size) * n.size * n.size);
			} else if (lo < 0.0f) {
				// Straddling the plane: count sub-cubes by their centres.
				constexpr int kSub = 16;
				const float s = n.size / float(kSub);
				for (int z = 0; z < kSub; ++z) {
					for (int y = 0; y < kSub; ++y) {
						for (int x = 0; x < kSub; ++x) {
							const vec3 c = n.lo + (vec3(float(x), float(y), float(z)) + 0.5f) * s;
							if (plane->distance(c) < 0.0f) {
								add(c, double(s) * s * s);
							}
						}
					}
				}
			}
			continue;
		}
		if (n.brick < 0) {
			continue;
		}
		const std::uint16_t *brick = values.data() + std::size_t(n.brick) * Adf::kBrickSamples;
		const float voxel = n.size / float(kCells);
		const double cube = double(voxel) * voxel * voxel;
		for (int z = 0; z < kCells; ++z) {
			for (int y = 0; y < kCells; ++y) {
				for (int x = 0; x < kCells; ++x) {
					float sum = 0.0f;
					for (int c = 0; c < 8; ++c) {
						sum += half_to_float(brick[index(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1))]);
					}
					const vec3 c = n.lo + (vec3(float(x), float(y), float(z)) + 0.5f) * voxel;
					if (sum < 0.0f && (!plane || plane->distance(c) < 0.0f)) {
						add(c, cube);
					}
				}
			}
		}
	}
	if (centroid && total > 0.0) {
		*centroid = vec3(float(moment[0] / total), float(moment[1] / total), float(moment[2] / total));
	}
	return total;
}

std::vector<vec3> hull_points(const Adf &adf, const Plane *plane, int count, float merge) {
	// Zero crossings along the bricks' sample edges.
	std::vector<vec3> points;
	const std::vector<std::uint16_t> &values = adf.brick_values();
	for (const Adf::Node &n : adf.nodes()) {
		if (n.child >= 0 || n.brick < 0) {
			continue;
		}
		const std::uint16_t *brick = values.data() + std::size_t(n.brick) * Adf::kBrickSamples;
		const float voxel = n.size / float(kCells);
		for (int z = 0; z < kN; ++z) {
			for (int y = 0; y < kN; ++y) {
				for (int x = 0; x < kN; ++x) {
					const float a = half_to_float(brick[index(x, y, z)]);
					const int step[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
					for (const auto &s : step) {
						const int x1 = x + s[0], y1 = y + s[1], z1 = z + s[2];
						if (x1 >= kN || y1 >= kN || z1 >= kN) {
							continue;
						}
						const float b = half_to_float(brick[index(x1, y1, z1)]);
						if ((a < 0.0f) == (b < 0.0f)) {
							continue;
						}
						const float t = a / (a - b);
						const vec3 p = n.lo + (vec3(float(x), float(y), float(z)) + vec3(float(s[0]), float(s[1]), float(s[2])) * t) * voxel;
						if (!plane || plane->distance(p) < 0.0f) {
							points.push_back(p);
						}
					}
				}
			}
		}
	}
	if (int(points.size()) <= count || count <= 0) {
		return points;
	}
	// The furthest point along each of `count` directions spread evenly over the sphere
	// (a Fibonacci lattice): the hull's extremes, whatever the shape.
	std::vector<int> best(std::size_t(count), 0);
	const float golden = 2.3999632f; // pi * (3 - sqrt(5))
	parallel_for(std::size_t(count), [&](std::size_t k) {
		const float y = 1.0f - 2.0f * (float(k) + 0.5f) / float(count);
		const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
		const vec3 dir(r * std::cos(golden * float(k)), y, r * std::sin(golden * float(k)));
		float far = -std::numeric_limits<float>::max();
		for (std::size_t i = 0; i < points.size(); ++i) {
			const float d = gl::dot(points[i], dir);
			if (d > far) {
				far = d;
				best[k] = int(i);
			}
		}
	});
	std::sort(best.begin(), best.end());
	best.erase(std::unique(best.begin(), best.end()), best.end());
	// Outermost first (from the extremes' centroid), dropping any within `merge` of a kept one.
	vec3 centre(0.0f);
	for (int i : best) {
		centre = centre + points[std::size_t(i)];
	}
	centre = centre / float(best.size());
	std::sort(best.begin(), best.end(), [&](int a, int b) {
		return gl::length(points[std::size_t(a)] - centre) > gl::length(points[std::size_t(b)] - centre);
	});
	std::vector<vec3> out;
	out.reserve(best.size());
	for (int i : best) {
		const vec3 &p = points[std::size_t(i)];
		if (std::none_of(out.begin(), out.end(), [&](const vec3 &q) { return gl::length(p - q) < merge; })) {
			out.push_back(p);
		}
	}
	return out;
}

} // namespace sdf
