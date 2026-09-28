#include "pieces/hull.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace sdf {

namespace {

struct Face {
	int a, b, c;
	vec3 normal; // unit, outward
	float offset; // normal . p for p on it
};

Face face(const std::vector<vec3> &p, int a, int b, int c, vec3 inside) {
	vec3 n = gl::cross(p[std::size_t(b)] - p[std::size_t(a)], p[std::size_t(c)] - p[std::size_t(a)]);
	const float length = gl::length(n);
	n = length > 0.0f ? n / length : vec3(0.0f);
	Face f{a, b, c, n, gl::dot(n, p[std::size_t(a)])};
	if (gl::dot(n, inside) > f.offset) {
		// (Wound the wrong way: seen from inside.)
		std::swap(f.b, f.c);
		f.normal = -f.normal;
		f.offset = -f.offset;
	}
	return f;
}

} // namespace

ConvexHull convex_hull(const std::vector<vec3> &points, float eps) {
	ConvexHull out;
	const std::size_t n = points.size();
	if (n < 4) {
		return out;
	}
	// A first tetrahedron: the extremes along x, the point furthest from their line, the one
	// furthest from their plane.
	int i0 = 0, i1 = 0;
	for (std::size_t i = 1; i < n; ++i) {
		if (points[i].x < points[std::size_t(i0)].x) {
			i0 = int(i);
		}
		if (points[i].x > points[std::size_t(i1)].x) {
			i1 = int(i);
		}
	}
	if (i0 == i1) {
		// All at one x: the furthest pair along any axis.
		i1 = i0;
		float best = 0.0f;
		for (std::size_t i = 0; i < n; ++i) {
			const float d = gl::length(points[i] - points[std::size_t(i0)]);
			if (d > best) {
				best = d;
				i1 = int(i);
			}
		}
	}
	const vec3 p0 = points[std::size_t(i0)], p1 = points[std::size_t(i1)];
	if (gl::length(p1 - p0) <= eps) {
		return out;
	}
	const vec3 line = gl::normalize(p1 - p0);
	int i2 = -1;
	float far = eps;
	for (std::size_t i = 0; i < n; ++i) {
		const vec3 d = points[i] - p0;
		const float off = gl::length(d - line * gl::dot(d, line));
		if (off > far) {
			far = off;
			i2 = int(i);
		}
	}
	if (i2 < 0) {
		return out;
	}
	const vec3 plane = gl::normalize(gl::cross(p1 - p0, points[std::size_t(i2)] - p0));
	int i3 = -1;
	far = eps;
	for (std::size_t i = 0; i < n; ++i) {
		const float off = std::fabs(gl::dot(points[i] - p0, plane));
		if (off > far) {
			far = off;
			i3 = int(i);
		}
	}
	if (i3 < 0) {
		return out;
	}
	const vec3 inside = (p0 + p1 + points[std::size_t(i2)] + points[std::size_t(i3)]) * 0.25f;
	std::vector<Face> faces{face(points, i0, i1, i2, inside), face(points, i0, i1, i3, inside),
			face(points, i0, i2, i3, inside), face(points, i1, i2, i3, inside)};

	// Each other point: the faces it sees are replaced by a fan from it to their horizon.
	std::vector<char> used(n, 0);
	for (const int i : {i0, i1, i2, i3}) {
		used[std::size_t(i)] = 1;
	}
	for (std::size_t i = 0; i < n; ++i) {
		if (used[i]) {
			continue;
		}
		const vec3 p = points[i];
		std::vector<char> sees(faces.size(), 0);
		bool any = false;
		for (std::size_t f = 0; f < faces.size(); ++f) {
			if (gl::dot(faces[f].normal, p) - faces[f].offset > eps) {
				sees[f] = 1;
				any = true;
			}
		}
		if (!any) {
			continue; // inside (or on it)
		}
		// The horizon: edges of seen faces whose other face is not seen.
		std::map<std::pair<int, int>, int> edges; // directed edge -> count among seen faces
		for (std::size_t f = 0; f < faces.size(); ++f) {
			if (sees[f]) {
				const Face &t = faces[f];
				edges[{t.a, t.b}] += 1;
				edges[{t.b, t.c}] += 1;
				edges[{t.c, t.a}] += 1;
			}
		}
		std::vector<Face> kept;
		for (std::size_t f = 0; f < faces.size(); ++f) {
			if (!sees[f]) {
				kept.push_back(faces[f]);
			}
		}
		for (const auto &[edge, count] : edges) {
			if (edges.count({edge.second, edge.first}) == 0) {
				kept.push_back(face(points, edge.first, edge.second, int(i), inside));
			}
		}
		faces = std::move(kept);
		used[i] = 1;
	}

	// Only the corners the faces use, renumbered.
	std::vector<int> index(n, -1);
	for (const Face &f : faces) {
		std::array<int, 3> t{};
		const int corners[3] = {f.a, f.b, f.c};
		for (int k = 0; k < 3; ++k) {
			int &at = index[std::size_t(corners[k])];
			if (at < 0) {
				at = int(out.points.size());
				out.points.push_back(points[std::size_t(corners[k])]);
			}
			t[std::size_t(k)] = at;
		}
		out.triangles.push_back(t);
	}
	return out;
}

} // namespace sdf
