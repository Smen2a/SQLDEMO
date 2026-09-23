#include "eval/reference_renderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace sdf {

namespace {

struct Ray {
	vec3 o, d;
};

// Slab test against the body's bounds; returns false on a miss.
bool clip_to_box(const Ray &ray, const Aabb &box, float &t0, float &t1) {
	t0 = 0.0f;
	t1 = 1e9f;
	const float o[3] = {ray.o.x, ray.o.y, ray.o.z};
	const float d[3] = {ray.d.x, ray.d.y, ray.d.z};
	const float lo[3] = {box.lo.x, box.lo.y, box.lo.z};
	const float hi[3] = {box.hi.x, box.hi.y, box.hi.z};
	for (int a = 0; a < 3; ++a) {
		if (std::fabs(d[a]) < 1e-12f) {
			if (o[a] < lo[a] || o[a] > hi[a]) {
				return false;
			}
			continue;
		}
		float ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
		if (ta > tb) {
			std::swap(ta, tb);
		}
		t0 = std::max(t0, ta);
		t1 = std::min(t1, tb);
	}
	return t0 <= t1;
}

class Tracer {
public:
	Tracer(const Body &body, const MaterialTable &materials, const RenderSettings &s, const Octree *octree,
			const Adf *adf)
		: body_(body), materials_(materials), s_(s), octree_(octree), adf_(adf), lipschitz_(body.lipschitz()),
		  bounds_(body.bounds().expanded(1.0f)) {}

	// Linear RGB for one ray. pixel_angle is the angular size of a pixel, for the
	// footprint-scaled hit epsilon.
	vec3 shade(const Ray &ray, float pixel_angle) const {
		float t0, t1;
		if (clip_to_box(ray, bounds_, t0, t1)) {
			float t = t0;
			const int max_steps = octree_ || adf_ ? s_.max_steps * 2 : s_.max_steps;
			for (int i = 0; i < max_steps && t <= t1; ++i) {
				const vec3 p = ray.o + ray.d * t;
				const float eps = std::max(t * pixel_angle * 0.5f, 1e-4f);
				if (adf_) {
					const Adf::Step st = adf_->step(p, ray.d);
					if (st.brick == Adf::kEmpty) {
						t += st.exit + 1e-4f;
						continue;
					}
					float d = st.d;
					if (st.exact) {
						// March on the brick while it guarantees clearance; the tape decides hits.
						if (d - st.error > eps) {
							t += std::min(std::max((d - st.error) / st.lipschitz, eps * 0.5f), st.exit + 1e-4f);
							continue;
						}
						d = octree_->distance(body_, p);
					}
					if (d < eps) {
						return surface(refine(ray, t, d), ray.d, eps);
					}
					t += std::min(std::max(d / st.lipschitz, eps * 0.5f), st.exit + 1e-4f);
					continue;
				}
				if (octree_) {
					const Octree::Step st = octree_->step(body_, p, ray.d);
					if (st.state == Octree::State::Empty) {
						t += st.exit + 1e-4f; // no surface anywhere in this cell
						continue;
					}
					if (st.d < eps) {
						return surface(refine(ray, t, st.d), ray.d, eps);
					}
					t += std::min(std::max(st.d / st.lipschitz, eps * 0.5f), st.exit + 1e-4f);
					continue;
				}
				const float d = distance(p);
				if (d < eps) {
					return surface(refine(ray, t, d), ray.d, eps);
				}
				t += std::max(d / lipschitz_, eps * 0.5f);
			}
		}
		return background(ray.d);
	}

private:
	vec3 background(vec3 dir) const {
		if (s_.output != RenderSettings::Output::Shaded) {
			return vec3(0.0f);
		}
		const float k = gl::clamp(0.5f + 0.5f * dir.z, 0.0f, 1.0f);
		return gl::mix(vec3(0.20f, 0.21f, 0.23f), vec3(0.34f, 0.36f, 0.40f), k);
	}

	// Sphere tracing stops anywhere within the hit epsilon, and where it stops depends on
	// the step sequence. A few signed steps settle onto the surface itself, so shading does
	// not depend on how the ray got there (and the octree and exhaustive paths agree).
	vec3 refine(const Ray &ray, float t, float d) const {
		for (int i = 0; i < 4 && std::fabs(d) > 1e-5f; ++i) {
			t += d;
			d = distance(ray.o + ray.d * t);
		}
		return ray.o + ray.d * t;
	}

	// Without an octree this is the formula field itself (every edit, in order): the ground
	// truth. Body::sample's per-point culling is sign-exact but only a bound in value near
	// bound-type primitives, which shading effects that read values (AO) would pick up.
	float distance(vec3 p) const {
		if (adf_) {
			return adf_->distance(body_, *octree_, p);
		}
		return octree_ ? octree_->distance(body_, p) : body_.sample_exhaustive(p).d;
	}

	// Tetrahedral normal step. Inside an ADF brick it spans half a voxel, which smooths the
	// trilinear field's gradient across voxel faces; elsewhere it is as fine as precision
	// allows.
	float normal_step(vec3 p, float eps) const {
		if (adf_) {
			const int node = adf_->leaf(p);
			if (node >= 0 && adf_->nodes()[std::size_t(node)].brick >= 0 && !adf_->nodes()[std::size_t(node)].exact()) {
				return std::max(eps, 0.5f * adf_->nodes()[std::size_t(node)].size / float(Adf::kSide - 1));
			}
		}
		return std::max(eps, 2e-3f);
	}

	vec3 normal(vec3 p, float h) const {
		const vec3 k0(1, -1, -1), k1(-1, -1, 1), k2(-1, 1, -1), k3(1, 1, 1);
		return gl::normalize(k0 * distance(p + k0 * h) + k1 * distance(p + k1 * h) + k2 * distance(p + k2 * h) +
				k3 * distance(p + k3 * h));
	}

	vec3 surface(vec3 p, vec3 view, float eps) const {
		const vec3 n = normal(p, normal_step(p, eps));
		if (s_.output == RenderSettings::Output::Normals) {
			return n * 0.5f + vec3(0.5f);
		}
		const Sample s = adf_ ? adf_->sample(body_, *octree_, p)
							  : octree_ ? octree_->sample(body_, p) : body_.sample_exhaustive(p);
		vec3 albedo = materials_.albedo(s.m0, p, body_.grain_origin, body_.grain_axis);
		const Material &m0 = materials_[std::uint16_t(s.m0)];
		float specular = m0.specular, shininess = m0.shininess;
		if (s.t > 0.0f) {
			const Material &m1 = materials_[std::uint16_t(s.m1)];
			albedo = gl::mix(albedo, materials_.albedo(s.m1, p, body_.grain_origin, body_.grain_axis), s.t);
			specular = gl::mix(specular, m1.specular, s.t);
			shininess = gl::mix(shininess, m1.shininess, s.t);
		}
		if (s_.output == RenderSettings::Output::Albedo) {
			return albedo;
		}

		const vec3 l = s_.light_dir;
		const float diffuse = std::max(gl::dot(n, l), 0.0f);
		const vec3 lifted = p + n * (2.0f * eps + 1e-3f);
		const float shadow = (s_.shadows && diffuse > 0.0f) ? soft_shadow(lifted, l) : 1.0f;
		const float ao = s_.ambient_occlusion ? occlusion(p, n) : 1.0f;
		const float sky = 0.55f + 0.45f * n.z;
		const vec3 h = gl::normalize(l - view);
		const float spec = specular * std::pow(std::max(gl::dot(n, h), 0.0f), shininess) * shadow * diffuse;

		// A little unoccluded bounce light keeps narrow grooves reading as dark wood rather
		// than holes.
		const vec3 bounce = s_.sky_colour * 0.18f;
		return albedo * (s_.light_colour * (diffuse * shadow) + s_.sky_colour * (sky * ao) + bounce) +
				s_.light_colour * spec;
	}

	// Penumbra estimate from the closest approach along the shadow ray (Quilez).
	float soft_shadow(vec3 ro, vec3 rd) const {
		float res = 1.0f, t = 0.02f;
		for (int i = 0; i < 160 && t < 400.0f; ++i) {
			const vec3 p = ro + rd * t;
			if (p.x < bounds_.lo.x || p.y < bounds_.lo.y || p.z < bounds_.lo.z || p.x > bounds_.hi.x ||
					p.y > bounds_.hi.y || p.z > bounds_.hi.z) {
				break;
			}
			float h, lip = lipschitz_, limit = 4.0f;
			if (adf_) {
				const Adf::Step st = adf_->step(p, rd);
				h = st.brick == Adf::kEmpty ? adf_->approx_distance(p) : st.d - st.error;
				lip = st.lipschitz;
				limit = std::max(st.exit + 1e-3f, 0.02f);
			} else if (octree_) {
				const Octree::Step st = octree_->step(body_, p, rd, true);
				h = st.d;
				lip = st.lipschitz;
				limit = std::max(st.exit + 1e-3f, 0.02f);
			} else {
				h = distance(p);
			}
			if (h < 1e-4f) {
				return 0.0f;
			}
			res = std::min(res, 10.0f * h / t);
			t += gl::clamp(h / lip, 0.02f, std::min(limit, 4.0f));
		}
		return gl::smoothstep(0.0f, 1.0f, res);
	}

	float occlusion(vec3 p, vec3 n) const {
		float occ = 0.0f, weight = 1.0f;
		const float dist[5] = {0.3f, 0.9f, 1.8f, 3.0f, 4.5f};
		for (float h : dist) {
			occ += (h - (adf_ ? adf_->approx_distance(p + n * h) : distance(p + n * h))) * weight;
			weight *= 0.8f;
		}
		return gl::clamp(1.0f - 0.25f * occ, 0.0f, 1.0f);
	}

	const Body &body_;
	const MaterialTable &materials_;
	const RenderSettings &s_;
	const Octree *octree_;
	const Adf *adf_;
	float lipschitz_;
	Aabb bounds_;
};

std::uint8_t encode(float value, bool gamma) {
	float c = gl::clamp(value, 0.0f, 1.0f);
	if (gamma) {
		c = std::pow(c, 1.0f / 2.2f);
	}
	return std::uint8_t(std::lround(c * 255.0f));
}

} // namespace

Image render(const Body &body, const MaterialTable &materials, const Camera &camera, const RenderSettings &s,
		const Octree *octree, const Adf *adf) {
	Image img(s.width, s.height);
	const Tracer tracer(body, materials, s, octree, adf);

	const vec3 forward = gl::normalize(camera.target - camera.eye);
	const vec3 right = gl::normalize(gl::cross(forward, camera.up));
	const vec3 up = gl::cross(right, forward);
	const float tan_half = std::tan(gl::radians(camera.fov_deg) * 0.5f);
	const float aspect = float(s.width) / float(s.height);
	const float pixel_angle = 2.0f * tan_half / float(s.height);
	const int n = std::max(s.samples_per_axis, 1);
	const bool gamma = s.output == RenderSettings::Output::Shaded;

	std::atomic<int> next_row{0};
	auto worker = [&]() {
		for (int y = next_row++; y < s.height; y = next_row++) {
			for (int x = 0; x < s.width; ++x) {
				vec3 sum(0.0f);
				for (int sy = 0; sy < n; ++sy) {
					for (int sx = 0; sx < n; ++sx) {
						const float fx = (float(x) + (float(sx) + 0.5f) / float(n)) / float(s.width);
						const float fy = (float(y) + (float(sy) + 0.5f) / float(n)) / float(s.height);
						const vec3 dir = gl::normalize(forward + right * ((2.0f * fx - 1.0f) * tan_half * aspect) +
								up * ((1.0f - 2.0f * fy) * tan_half));
						sum += tracer.shade({camera.eye, dir}, pixel_angle / float(n));
					}
				}
				const vec3 c = sum / float(n * n);
				std::uint8_t *px = img.at(x, y);
				px[0] = encode(c.x, gamma);
				px[1] = encode(c.y, gamma);
				px[2] = encode(c.z, gamma);
			}
		}
	};

	const int threads = s.threads > 0 ? s.threads : int(std::max(1u, std::thread::hardware_concurrency()));
	std::vector<std::thread> pool;
	for (int i = 1; i < threads; ++i) {
		pool.emplace_back(worker);
	}
	worker();
	for (std::thread &t : pool) {
		t.join();
	}
	return img;
}

} // namespace sdf
