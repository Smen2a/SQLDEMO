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

Plane Separation::behind() const {
	const float inset = std::max(0.5f * gap - 0.005f, 0.0f);
	return {plane.point - plane.normal * inset, plane.normal};
}

Plane Separation::front() const {
	const float inset = std::max(0.5f * gap - 0.005f, 0.0f);
	return {plane.point + plane.normal * inset, -plane.normal};
}

PieceSides measure_sides(const Adf &adf, const Separation &cut, bool smaller_in_front) {
	PieceSides s;
	s.cut = cut;
	Plane behind = cut.behind(), front = cut.front();
	s.volume[0] = volume(adf, &behind, &s.centre[0]);
	s.volume[1] = volume(adf, &front, &s.centre[1]);
	if (smaller_in_front && s.volume[1] > s.volume[0]) {
		s.cut = cut.flipped();
		std::swap(s.volume[0], s.volume[1]);
		std::swap(s.centre[0], s.centre[1]);
		std::swap(behind, front);
	}
	s.hull[0] = hull_points(adf, &behind);
	s.hull[1] = hull_points(adf, &front);
	return s;
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
	std::vector<int> leaves;
	for (std::size_t i = 0; i < nodes.size(); ++i) {
		if (nodes[i].child < 0 && (nodes[i].brick >= 0 || nodes[i].brick == Adf::kSolid)) {
			leaves.push_back(int(i));
		}
	}
	// Each leaf's volume and first moment, then summed in order (the same result whatever
	// the threads do).
	struct Part {
		double volume = 0.0, moment[3] = {0.0, 0.0, 0.0};
		void add(vec3 centre, double v) {
			volume += v;
			moment[0] += v * centre.x;
			moment[1] += v * centre.y;
			moment[2] += v * centre.z;
		}
	};
	std::vector<Part> parts(leaves.size());
	parallel_for(leaves.size(), [&](std::size_t k) {
		const Adf::Node &n = nodes[std::size_t(leaves[k])];
		Part &part = parts[k];
		if (n.brick == Adf::kSolid) {
			const vec3 middle = n.lo + vec3(n.size * 0.5f);
			if (!plane) {
				part.add(middle, double(n.size) * n.size * n.size);
				return;
			}
			float lo = std::numeric_limits<float>::max(), hi = -lo;
			for (int c = 0; c < 8; ++c) {
				const float d = plane->distance(corner({n.lo, n.lo + vec3(n.size)}, c));
				lo = std::min(lo, d);
				hi = std::max(hi, d);
			}
			if (hi <= 0.0f) {
				part.add(middle, double(n.size) * n.size * n.size);
			} else if (lo < 0.0f) {
				// Straddling the plane: count sub-cubes by their centres.
				constexpr int kSub = 16;
				const float s = n.size / float(kSub);
				for (int z = 0; z < kSub; ++z) {
					for (int y = 0; y < kSub; ++y) {
						for (int x = 0; x < kSub; ++x) {
							const vec3 c = n.lo + (vec3(float(x), float(y), float(z)) + 0.5f) * s;
							if (plane->distance(c) < 0.0f) {
								part.add(c, double(s) * s * s);
							}
						}
					}
				}
			}
			return;
		}
		const std::uint16_t *brick = values.data() + std::size_t(n.brick) * Adf::kBrickSamples;
		float v[Adf::kBrickSamples];
		for (int i = 0; i < Adf::kBrickSamples; ++i) {
			v[i] = half_to_float(brick[i]);
		}
		const float voxel = n.size / float(kCells);
		const double cube = double(voxel) * voxel * voxel;
		for (int z = 0; z < kCells; ++z) {
			for (int y = 0; y < kCells; ++y) {
				for (int x = 0; x < kCells; ++x) {
					float sum = 0.0f;
					for (int c = 0; c < 8; ++c) {
						sum += v[index(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1))];
					}
					const vec3 c = n.lo + (vec3(float(x), float(y), float(z)) + 0.5f) * voxel;
					if (sum < 0.0f && (!plane || plane->distance(c) < 0.0f)) {
						part.add(c, cube);
					}
				}
			}
		}
	});
	Part total;
	for (const Part &part : parts) {
		total.volume += part.volume;
		for (int a = 0; a < 3; ++a) {
			total.moment[a] += part.moment[a];
		}
	}
	if (centroid && total.volume > 0.0) {
		*centroid = vec3(float(total.moment[0] / total.volume), float(total.moment[1] / total.volume),
				float(total.moment[2] / total.volume));
	}
	return total.volume;
}

std::vector<vec3> hull_points(const Adf &adf, const Plane *plane, int count, float merge) {
	// Brick leaves with any part behind the plane, and their bounds.
	struct Leaf {
		const Adf::Node *node;
		vec3 centre, half;
	};
	std::vector<Leaf> leaves;
	for (const Adf::Node &n : adf.nodes()) {
		if (n.child >= 0 || n.brick < 0) {
			continue;
		}
		if (plane) {
			float lo = std::numeric_limits<float>::max();
			for (int c = 0; c < 8; ++c) {
				lo = std::min(lo, plane->distance(corner({n.lo, n.lo + vec3(n.size)}, c)));
			}
			if (lo >= 0.0f) {
				continue;
			}
		}
		leaves.push_back({&n, n.lo + vec3(n.size * 0.5f), vec3(n.size * 0.5f)});
	}
	// A leaf's surface points (zero crossings along its brick's sample edges, behind the
	// plane), worked out the first time a direction needs them.
	const std::vector<std::uint16_t> &values = adf.brick_values();
	std::vector<std::vector<vec3>> crossings(leaves.size());
	std::vector<char> known(leaves.size(), 0);
	auto points_of = [&](std::size_t k) -> const std::vector<vec3> & {
		if (known[k]) {
			return crossings[k];
		}
		known[k] = 1;
		const Adf::Node &n = *leaves[k].node;
		const std::uint16_t *brick = values.data() + std::size_t(n.brick) * Adf::kBrickSamples;
		float v[Adf::kBrickSamples];
		for (int i = 0; i < Adf::kBrickSamples; ++i) {
			v[i] = half_to_float(brick[i]);
		}
		const float voxel = n.size / float(kCells);
		for (int z = 0; z < kN; ++z) {
			for (int y = 0; y < kN; ++y) {
				for (int x = 0; x < kN; ++x) {
					const float a = v[index(x, y, z)];
					const int step[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
					for (const auto &st : step) {
						const int x1 = x + st[0], y1 = y + st[1], z1 = z + st[2];
						if (x1 >= kN || y1 >= kN || z1 >= kN) {
							continue;
						}
						const float b = v[index(x1, y1, z1)];
						if ((a < 0.0f) == (b < 0.0f)) {
							continue;
						}
						const float t = a / (a - b);
						const vec3 p = n.lo + (vec3(float(x), float(y), float(z)) +
													  vec3(float(st[0]), float(st[1]), float(st[2])) * t) *
													  voxel;
						if (!plane || plane->distance(p) < 0.0f) {
							crossings[k].push_back(p);
						}
					}
				}
			}
		}
		return crossings[k];
	};
	if (count <= 0) {
		std::vector<vec3> all;
		for (std::size_t k = 0; k < leaves.size(); ++k) {
			const std::vector<vec3> &p = points_of(k);
			all.insert(all.end(), p.begin(), p.end());
		}
		return all;
	}
	// The furthest point along each of `count` directions spread evenly over the sphere
	// (a Fibonacci lattice): the hull's extremes, whatever the shape. A leaf's bounds cap
	// how far its points reach along a direction, so only leaves whose cap beats the best
	// point found so far are looked into (the most promising first).
	const float golden = 2.3999632f; // pi * (3 - sqrt(5))
	std::vector<vec3> best;
	std::vector<float> reach(leaves.size());
	for (int k = 0; k < count; ++k) {
		const float y = 1.0f - 2.0f * (float(k) + 0.5f) / float(count);
		const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
		const vec3 dir(r * std::cos(golden * float(k)), y, r * std::sin(golden * float(k)));
		const vec3 spread(std::fabs(dir.x), std::fabs(dir.y), std::fabs(dir.z));
		std::size_t first = 0;
		for (std::size_t i = 0; i < leaves.size(); ++i) {
			reach[i] = gl::dot(leaves[i].centre, dir) + gl::dot(leaves[i].half, spread);
			first = reach[i] > reach[first] ? i : first;
		}
		float far = -std::numeric_limits<float>::max();
		vec3 found(0.0f);
		auto look = [&](std::size_t i) {
			for (const vec3 &p : points_of(i)) {
				const float d = gl::dot(p, dir);
				if (d > far) {
					far = d;
					found = p;
				}
			}
		};
		if (leaves.empty()) {
			break;
		}
		look(first);
		for (std::size_t i = 0; i < leaves.size(); ++i) {
			if (i != first && reach[i] > far) {
				look(i);
			}
		}
		if (far > -std::numeric_limits<float>::max()) {
			best.push_back(found);
		}
	}
	// Outermost first (from the extremes' centroid), dropping any within `merge` of a kept one.
	vec3 centre(0.0f);
	for (const vec3 &p : best) {
		centre = centre + p;
	}
	if (!best.empty()) {
		centre = centre / float(best.size());
	}
	std::sort(best.begin(), best.end(), [&](const vec3 &a, const vec3 &b) {
		return gl::length(a - centre) > gl::length(b - centre);
	});
	std::vector<vec3> out;
	for (const vec3 &p : best) {
		if (std::none_of(out.begin(), out.end(), [&](const vec3 &q) { return gl::length(p - q) < merge; })) {
			out.push_back(p);
		}
	}
	return out;
}

} // namespace sdf
