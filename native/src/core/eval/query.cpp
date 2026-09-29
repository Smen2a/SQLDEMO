#include "eval/query.h"

#include <algorithm>
#include <cmath>

namespace sdf {

std::optional<Hit> raycast(const Body &body, const Octree &octree, vec3 origin, vec3 dir, float t_max, float epsilon) {
	if (octree.nodes().empty()) {
		return std::nullopt;
	}
	// Clip to the root cube: outside it there is no surface.
	const Octree::Node &root = octree.nodes()[0];
	float t0 = 0.0f, t1 = t_max;
	const float o[3] = {origin.x, origin.y, origin.z}, d[3] = {dir.x, dir.y, dir.z};
	const float lo[3] = {root.lo.x, root.lo.y, root.lo.z};
	for (int a = 0; a < 3; ++a) {
		if (std::fabs(d[a]) < 1e-12f) {
			if (o[a] < lo[a] || o[a] > lo[a] + root.size) {
				return std::nullopt;
			}
			continue;
		}
		float ta = (lo[a] - o[a]) / d[a], tb = (lo[a] + root.size - o[a]) / d[a];
		if (ta > tb) {
			std::swap(ta, tb);
		}
		t0 = std::max(t0, ta);
		t1 = std::min(t1, tb);
	}
	if (t0 > t1) {
		return std::nullopt;
	}
	float t = t0;
	for (int i = 0; i < 1024 && t <= t1; ++i) {
		const Octree::Step st = octree.step(body, origin + dir * t, dir);
		if (st.state == Octree::State::Empty) {
			t += st.exit + 1e-4f;
			continue;
		}
		if (st.d < epsilon) {
			// Settle onto the surface, then read its normal and material there.
			float s = st.d;
			for (int k = 0; k < 6 && std::fabs(s) > epsilon * 1e-2f; ++k) {
				t += s;
				s = octree.distance(body, origin + dir * t);
			}
			Hit hit;
			hit.t = t;
			hit.point = origin + dir * t;
			const float h = std::max(epsilon, 1e-3f);
			const vec3 k0(1, -1, -1), k1(-1, -1, 1), k2(-1, 1, -1), k3(1, 1, 1);
			const vec3 g = k0 * octree.distance(body, hit.point + k0 * h) + k1 * octree.distance(body, hit.point + k1 * h) +
					k2 * octree.distance(body, hit.point + k2 * h) + k3 * octree.distance(body, hit.point + k3 * h);
			hit.normal = gl::dot(g, g) > 1e-24f ? gl::normalize(g) : -dir;
			hit.sample = octree.sample(body, hit.point);
			return hit;
		}
		t += std::min(std::max(st.d / st.lipschitz, epsilon * 0.5f), st.exit + 1e-4f);
	}
	return std::nullopt;
}

} // namespace sdf
