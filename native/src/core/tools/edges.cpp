#include "tools/edges.h"

#include "eval/query.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace sdf::tools {

namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
constexpr float kAbove = 10.0f;    // mm above the point the rays start
constexpr float kStep = 0.1f;      // mm of height a step takes beyond the surface's slope
constexpr float kFit = 3.0f;       // mm round the nearest crossing its line is fitted over
constexpr float kSide = 1.5f;      // mm either side of the line its sides are looked at
constexpr float kFloorTilt = 20.0f * kDeg;

// The surface seen from above at a point of the tangent plane: its height over the plane
// and its normal, or nothing (off the work).
struct Spot {
	bool hit = false;
	float height = 0.0f;
	vec3 normal{0.0f};
};

struct Look {
	const Body &body;
	const Octree &octree;
	Frame frame; // at the point: z out of the surface

	Spot at(float x, float y) const {
		const vec3 from = frame.point({x, y, kAbove});
		const auto hit = raycast(body, octree, from, -frame.z, kAbove + kFloorReach + 30.0f);
		Spot s;
		if (hit) {
			s.hit = true;
			s.height = gl::dot(hit->point - frame.origin, frame.z);
			s.normal = hit->normal;
		}
		return s;
	}
};

// How much height a surface with this normal gains over a millimetre, at most (in any
// direction in the plane); steep faces answer a lot.
float slope(const Spot &s, vec3 up) {
	const float c = std::max(gl::dot(s.normal, up), 0.05f);
	return std::sqrt(std::max(1.0f - c * c, 0.0f)) / c;
}

// Whether two spots a millimetre apart lie either side of an edge.
bool crosses(const Spot &a, const Spot &b, vec3 up, float fold_cos) {
	if (a.hit != b.hit) {
		return true; // one falls off the work
	}
	if (!a.hit) {
		return false;
	}
	const float allowed = std::min(std::max(slope(a, up), slope(b, up)), 3.0f) + kStep;
	if (std::fabs(a.height - b.height) > allowed) {
		return true;
	}
	return gl::dot(a.normal, b.normal) < fold_cos;
}

enum class Kind { None, Step, Fold };

// What lies between two spots `apart` mm either side of a line: a step (a height the
// surfaces' own slopes do not account for, or one side off the work), a fold, or nothing.
Kind between(const Spot &near, const Spot &far, float apart, vec3 up, float fold_cos) {
	if (near.hit != far.hit) {
		return Kind::Step;
	}
	if (!near.hit) {
		return Kind::None;
	}
	const float allowed = 0.5f * apart * (std::min(slope(near, up), 3.0f) + std::min(slope(far, up), 3.0f)) + kStep;
	if (std::fabs(near.height - far.height) > allowed) {
		return Kind::Step;
	}
	return gl::dot(near.normal, far.normal) < fold_cos ? Kind::Fold : Kind::None;
}

// Which side of an edge of this kind a spot is on: the near side's (true) or the far side's.
bool near_side(const Spot &s, const Spot &near, const Spot &far, Kind kind) {
	if (near.hit != far.hit) {
		return s.hit == near.hit;
	}
	if (kind == Kind::Step) {
		return std::fabs(s.height - near.height) <= std::fabs(s.height - far.height);
	}
	return gl::dot(s.normal, near.normal) >= gl::dot(s.normal, far.normal);
}

} // namespace

EdgeLock find_edge(const Body &body, const Octree &octree, vec3 point, vec3 normal, float reach, float fold_deg) {
	EdgeLock e;
	const vec3 n = gl::normalize(normal);
	const vec3 any = std::fabs(n.x) < 0.9f ? vec3(1, 0, 0) : vec3(0, 1, 0);
	const Look look{body, octree, Frame::at(point, n, any)};
	const float fold_cos = std::cos(fold_deg * kDeg);

	// The height map, a millimetre apart.
	const int r = std::max(1, int(std::round(reach)));
	const int size = 2 * r + 1;
	std::vector<Spot> map(std::size_t(size * size));
	for (int j = 0; j < size; ++j) {
		for (int i = 0; i < size; ++i) {
			map[std::size_t(j * size + i)] = look.at(float(i - r), float(j - r));
		}
	}
	// Crossings between neighbours: their midpoints in the plane.
	std::vector<vec2> crossings;
	for (int j = 0; j < size; ++j) {
		for (int i = 0; i < size; ++i) {
			const Spot &a = map[std::size_t(j * size + i)];
			if (i + 1 < size && crosses(a, map[std::size_t(j * size + i + 1)], n, fold_cos)) {
				crossings.push_back({float(i - r) + 0.5f, float(j - r)});
			}
			if (j + 1 < size && crosses(a, map[std::size_t((j + 1) * size + i)], n, fold_cos)) {
				crossings.push_back({float(i - r), float(j - r) + 0.5f});
			}
		}
	}
	// The nearest, within reach, and the line through those round it.
	const vec2 *nearest = nullptr;
	for (const vec2 &c : crossings) {
		if (gl::length(c) <= reach && (!nearest || gl::length(c) < gl::length(*nearest))) {
			nearest = &c;
		}
	}
	if (!nearest) {
		return e;
	}
	vec2 centre{0.0f};
	int count = 0;
	for (const vec2 &c : crossings) {
		if (gl::length(c - *nearest) <= kFit) {
			centre = centre + c;
			++count;
		}
	}
	centre = centre / float(count);
	float xx = 0.0f, xy = 0.0f, yy = 0.0f;
	for (const vec2 &c : crossings) {
		if (gl::length(c - *nearest) <= kFit) {
			const vec2 d = c - centre;
			xx += d.x * d.x;
			xy += d.x * d.y;
			yy += d.y * d.y;
		}
	}
	// The major axis of the spread.
	const float angle = 0.5f * std::atan2(2.0f * xy, xx - yy);
	vec2 dir{std::cos(angle), std::sin(angle)};
	vec2 perp{-dir.y, dir.x};
	if (gl::dot(-centre, perp) < 0.0f) {
		perp = -perp; // towards the point
	}

	// Where the line crosses, exactly: bisected along the perpendicular at two places on it,
	// between its near and far sides.
	Kind kind = Kind::None;
	auto cross_at = [&](vec2 on) -> std::optional<vec2> {
		const vec2 in = on + perp * kSide, out = on - perp * kSide;
		const Spot near = look.at(in.x, in.y), far = look.at(out.x, out.y);
		const Kind k = between(near, far, 2.0f * kSide, n, fold_cos);
		if (k == Kind::None) {
			return std::nullopt; // no edge across here
		}
		kind = k;
		float lo = -kSide, hi = kSide; // far .. near
		for (int i = 0; i < 14; ++i) {
			const float mid = 0.5f * (lo + hi);
			const vec2 p = on + perp * mid;
			(near_side(look.at(p.x, p.y), near, far, k) ? hi : lo) = mid;
		}
		return on + perp * (0.5f * (lo + hi));
	};
	const vec2 foot = centre + dir * gl::dot(-centre, dir); // nearest the point on the fitted line
	const auto a = cross_at(foot - dir * 1.5f);
	const auto b = cross_at(foot + dir * 1.5f);
	if (a && b && gl::length(*b - *a) > 1.0f) {
		dir = gl::normalize(*b - *a);
		perp = {-dir.y, dir.x};
		const vec2 mid = (*a + *b) * 0.5f;
		if (gl::dot(-mid, perp) < 0.0f) {
			perp = -perp;
		}
		centre = mid;
	} else if (const auto c = cross_at(foot)) {
		centre = *c;
	} else {
		centre = foot;
	}
	const vec2 on = centre + dir * gl::dot(-centre, dir);
	e.distance = std::fabs(gl::dot(-centre, perp));
	if (e.distance > reach) {
		return e;
	}

	// Its sides.
	const vec2 in = on + perp * kSide, out = on - perp * kSide;
	const Spot near = look.at(in.x, in.y), far = look.at(out.x, out.y);
	if (kind == Kind::None) {
		kind = between(near, far, 2.0f * kSide, n, fold_cos);
	}
	e.found = true;
	e.point = look.frame.point({on.x, on.y, near.hit ? near.height : 0.0f});
	e.direction = gl::normalize(look.frame.direction({dir.x, dir.y, 0.0f}));
	e.across = gl::normalize(look.frame.direction({perp.x, perp.y, 0.0f}));
	if (!far.hit) {
		e.step = -1e3f; // off the work: a drop
	} else if (!near.hit) {
		e.step = 1e3f;
	} else if (kind == Kind::Fold) {
		e.fold = true; // the surface turns: no step to speak of
	} else {
		e.step = far.height - near.height;
		e.floor = e.step < -kStep && -e.step <= kFloorReach && gl::dot(far.normal, n) >= std::cos(kFloorTilt);
	}
	return e;
}

} // namespace sdf::tools
