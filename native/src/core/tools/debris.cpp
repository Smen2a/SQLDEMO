#include "tools/debris.h"

#include <algorithm>
#include <cmath>

namespace sdf::tools {

float Debris::shaving_volume() const {
	float v = 0.0f;
	for (const ShavingSample &s : shaving) {
		v += s.thickness * s.width * step;
	}
	return v;
}

float Debris::chip_volume() const {
	float v = 0.0f;
	for (const Chip &c : chips) {
		v += c.volume;
	}
	return v;
}

float Debris::dust_volume() const {
	float v = 0.0f;
	for (const Dust &d : dust) {
		v += d.volume;
	}
	return v;
}

float depth_below_surface(const Body &body, const Octree &octree, vec3 floor, vec3 up, float most) {
	// Up from the floor to the surface, a step as long as the distance allows (the field is
	// 1-Lipschitz, so no step crosses it), then halved in on the crossing.
	float t = 0.0f;
	float d = octree.distance(body, floor);
	if (d >= 0.0f) {
		return 0.0f;
	}
	while (t < most) {
		const float step = std::max(-d, 0.01f);
		const float next = std::min(t + step, most);
		const float dn = octree.distance(body, floor + up * next);
		if (dn >= 0.0f) {
			float lo = t, hi = next;
			for (int i = 0; i < 12; ++i) {
				const float mid = 0.5f * (lo + hi);
				(octree.distance(body, floor + up * mid) < 0.0f ? lo : hi) = mid;
			}
			return 0.5f * (lo + hi);
		}
		t = next;
		d = dn;
	}
	return most;
}

float material_fraction(const Body &body, const Octree &octree, const Frame &plane, vec2 lo, vec2 hi, float depth,
		int nx, int ny) {
	int inside = 0;
	for (int j = 0; j < ny; ++j) {
		for (int i = 0; i < nx; ++i) {
			const vec2 at(lo.x + (hi.x - lo.x) * (float(i) + 0.5f) / float(nx),
					lo.y + (hi.y - lo.y) * (float(j) + 0.5f) / float(ny));
			inside += octree.distance(body, plane.point({at.x, at.y, -depth})) < 0.0f;
		}
	}
	return float(inside) / float(std::max(nx * ny, 1));
}

bool measure_chip(const Body &body, const Octree &octree, const Edit &cut, const std::vector<Edit> &taken, vec3 along,
		vec3 up, Chip &out, int samples) {
	const Aabb box = cut.prim.bounds();
	if (box.empty()) {
		return false;
	}
	const vec3 size = box.size();
	const float volume = std::max(size.x * size.y * size.z, 1e-6f);
	const float h = std::max(0.1f, std::cbrt(volume / float(std::max(samples, 1))));
	const vec3 x = gl::normalize(along - up * gl::dot(along, up)), z = gl::normalize(up), y = gl::cross(z, x);
	vec3 lo(1e30f), hi(-1e30f);
	int count = 0;
	for (float pz = box.lo.z + 0.5f * h; pz < box.hi.z; pz += h) {
		for (float py = box.lo.y + 0.5f * h; py < box.hi.y; py += h) {
			for (float px = box.lo.x + 0.5f * h; px < box.hi.x; px += h) {
				const vec3 p(px, py, pz);
				if (cut.prim.eval(p) >= 0.0f) {
					continue;
				}
				bool before = false;
				for (const Edit &e : taken) {
					if (e.prim.eval(p) < 0.0f) {
						before = true;
						break;
					}
				}
				if (before || octree.distance(body, p) >= 0.0f) {
					continue;
				}
				const vec3 local(gl::dot(p, x), gl::dot(p, y), gl::dot(p, z));
				lo = gl::min(lo, local);
				hi = gl::max(hi, local);
				++count;
			}
		}
	}
	if (count == 0) {
		return false;
	}
	lo = lo - vec3(0.5f * h);
	hi = hi + vec3(0.5f * h);
	const vec3 mid = (lo + hi) * 0.5f;
	out.frame = Frame{x * mid.x + y * mid.y + z * mid.z, x, y, z};
	out.size = hi - lo;
	out.volume = float(count) * h * h * h;
	return true;
}

} // namespace sdf::tools
