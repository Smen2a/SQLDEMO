#include "tools/smoothing.h"

#include "util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sdf::tools {

namespace {

constexpr float kSqrt3 = 1.7320508f;
// How much faster than its own curvature a level set outside the surface may move (see
// update()); Params::max_step allows for it.
constexpr float kMaxExtension = 1.5f;

int floor_div(int a, int b) {
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

float box_distance(vec3 lo, vec3 hi, vec3 p) {
	return gl::length(gl::max(gl::max(lo - p, p - hi), 0.0f));
}

// f(0) .. f(n - 1) spread over the hardware's threads (this runs on the edit worker, which
// is otherwise idle while it waits), at least 8 items to a thread.
template <class F>
void parallel_for(std::size_t n, F &&f) {
	sdf::parallel_for(n, f, 0, 8);
}

} // namespace

SmoothingGrid::SmoothingGrid(const Body &body, const Octree &octree, const Params &params)
	: body_(body), octree_(octree), params_(params), h_(params.spacing) {}

std::int64_t SmoothingGrid::key(int x, int y, int z) {
	constexpr std::int64_t kOffset = 1 << 20, kMask = (1 << 21) - 1;
	return ((std::int64_t(x) + kOffset) & kMask) | (((std::int64_t(y) + kOffset) & kMask) << 21) |
			(((std::int64_t(z) + kOffset) & kMask) << 42);
}

SmoothingGrid::Work *SmoothingGrid::find(int x, int y, int z) {
	const auto it = blocks_.find(key(x, y, z));
	return it == blocks_.end() ? nullptr : it->second.get();
}

vec3 SmoothingGrid::sample_at(const Layer::Coord &c, int i) const {
	return vec3(float(kB * c[0] + i % kB), float(kB * c[1] + (i / kB) % kB), float(kB * c[2] + i / (kB * kB))) * h_;
}

float SmoothingGrid::field(vec3 p) const {
	// The octree's tapes, inside its root cube; outside, where it only bounds the distance
	// (by the cube's), every edit.
	const Octree::Node &root = octree_.nodes()[0];
	const vec3 rel = p - root.lo;
	if (std::min(rel.x, std::min(rel.y, rel.z)) >= 0.0f && std::max(rel.x, std::max(rel.y, rel.z)) <= root.size) {
		return octree_.sample(body_, p).d;
	}
	return body_.sample(p).d;
}

std::unique_ptr<SmoothingGrid::Work> SmoothingGrid::sample_block(int x, int y, int z, float band, float &beyond) const {
	// Only blocks the surface may come within reach of: w needs phi out to the weight band,
	// plus the B-spline's reach and the flow's neighbours, from wherever the surface has
	// receded to.
	const float span = float(kB - 1) * h_;
	const vec3 lo = vec3(float(x), float(y), float(z)) * (float(kB) * h_);
	beyond = std::fabs(field(lo + vec3(span * 0.5f))) - body_.lipschitz() * span * 0.5f * kSqrt3;
	if (beyond > band) {
		return nullptr;
	}
	auto b = std::make_unique<Work>();
	b->coord = {x, y, z};
	for (int i = 0; i < kN; ++i) {
		b->F[i] = b->phi[i] = field(sample_at(b->coord, i));
		b->w[i] = 0.0f;
		b->amount[i] = 0.0f;
	}
	// Linear through the 3^3 neighbours: second differences vanish along every axis and
	// diagonal pair (the block's edges count as not flat).
	for (int i = 0; i < kN; ++i) {
		const int ix = i % kB, iy = (i / kB) % kB, iz = i / (kB * kB);
		bool flat = ix > 0 && iy > 0 && iz > 0 && ix < kB - 1 && iy < kB - 1 && iz < kB - 1;
		for (int dz = -1; dz <= 1 && flat; ++dz) {
			for (int dy = -1; dy <= 1 && flat; ++dy) {
				for (int dx = -1; dx <= 1 && flat; ++dx) {
					const int j = i + dx + kB * (dy + kB * dz), k = i - dx - kB * (dy + kB * dz);
					flat = std::fabs(b->F[j] + b->F[k] - 2.0f * b->F[i]) < 1e-5f;
				}
			}
		}
		b->flat[i] = flat;
	}
	return b;
}

void SmoothingGrid::press(vec3 centre, float reach, float amount) {
	presses_.push_back({centre, reach, amount});
}

void SmoothingGrid::allocate_pressed() {
	// Every block within reach of a press (and its B-spline's) not yet sampled, sampled in
	// parallel: the F samples are most of the cost of new ground.
	const float side = float(kB) * h_, span = float(kB - 1) * h_;
	const float band = params_.weight_band + params_.weight_taper + 2.0f * h_ * kSqrt3 + max_offset_;
	std::vector<Layer::Coord> wanted;
	std::unordered_map<std::int64_t, char> seen;
	for (const Press &press : presses_) {
		const float alloc = press.reach + 2.0f * h_;
		const float c[3] = {press.centre.x, press.centre.y, press.centre.z};
		int b0[3], b1[3];
		for (int a = 0; a < 3; ++a) {
			b0[a] = int(std::floor((c[a] - alloc) / side));
			b1[a] = int(std::floor((c[a] + alloc) / side));
		}
		for (int z = b0[2]; z <= b1[2]; ++z) {
			for (int y = b0[1]; y <= b1[1]; ++y) {
				for (int x = b0[0]; x <= b1[0]; ++x) {
					const std::int64_t k = key(x, y, z);
					if (blocks_.count(k) || seen.count(k)) {
						continue;
					}
					const auto far = far_.find(k);
					if (far != far_.end() && far->second > band) {
						continue;
					}
					const vec3 lo = vec3(float(x), float(y), float(z)) * side;
					if (box_distance(lo, lo + vec3(span), press.centre) > alloc) {
						continue;
					}
					seen[k] = 1;
					wanted.push_back({x, y, z});
				}
			}
		}
	}
	std::vector<std::unique_ptr<Work>> sampled(wanted.size());
	std::vector<float> beyond(wanted.size());
	parallel_for(wanted.size(), [&](std::size_t n) {
		sampled[n] = sample_block(wanted[n][0], wanted[n][1], wanted[n][2], band, beyond[n]);
	});
	for (std::size_t n = 0; n < wanted.size(); ++n) {
		const std::int64_t k = key(wanted[n][0], wanted[n][1], wanted[n][2]);
		if (sampled[n]) {
			far_.erase(k);
			blocks_.emplace(k, std::move(sampled[n]));
			++allocated_;
		} else {
			far_[k] = beyond[n];
		}
	}
}

Aabb SmoothingGrid::update() {
	last_ = {};
	allocate_pressed();
	last_.allocated = allocated_;
	allocated_ = 0;

	// The presses: w rises where the tool reached, and every sample it bore on fully gets its
	// share of the flow.
	const float side = float(kB) * h_, span = float(kB - 1) * h_;
	const float w_end = params_.weight_band + params_.weight_taper;
	std::vector<Work *> touched;
	for (auto &[k, b] : blocks_) {
		const vec3 lo = vec3(float(b->coord[0]), float(b->coord[1]), float(b->coord[2])) * side;
		for (const Press &press : presses_) {
			if (box_distance(lo, lo + vec3(span), press.centre) < press.reach) {
				touched.push_back(b.get());
				break;
			}
		}
	}
	std::vector<Aabb> touched_changed(touched.size());
	parallel_for(touched.size(), [&](std::size_t n) {
		Work &b = *touched[n];
		const vec3 lo = vec3(float(b.coord[0]), float(b.coord[1]), float(b.coord[2])) * side;
		for (const Press &press : presses_) {
			if (box_distance(lo, lo + vec3(span), press.centre) >= press.reach) {
				continue;
			}
			const float full = press.reach - params_.reach_taper;
			for (int i = 0; i < kN; ++i) {
				const vec3 p = sample_at(b.coord, i);
				const vec3 d = p - press.centre;
				const float r2 = gl::dot(d, d);
				if (r2 >= press.reach * press.reach) {
					continue;
				}
				const float r = std::sqrt(r2);
				const float depth = std::min(std::fabs(b.F[i]), std::fabs(b.phi[i]));
				const float w = std::clamp((press.reach - r) / params_.reach_taper, 0.0f, 1.0f) *
						std::clamp((w_end - depth) / params_.weight_taper, 0.0f, 1.0f);
				if (w > b.w[i]) {
					b.w[i] = w;
					b.dirty = true;
					// On a face, phi~ is F whatever w is.
					if (!b.flat[i] || b.phi[i] != b.F[i]) {
						touched_changed[n].include(p);
					}
				}
				if (r < full && press.amount > 0.0f) {
					const float q = 1.0f - (r / full) * (r / full);
					b.amount[i] += press.amount * q * q;
					b.pending = true;
				}
			}
		}
	});
	presses_.clear();
	for (const Aabb &box : touched_changed) {
		changed_.include(box);
	}

	std::vector<Work *> pending;
	float most = 0.0f;
	for (Work *b : touched) {
		if (b->pending) {
			pending.push_back(b);
			for (int i = 0; i < kN; ++i) {
				most = std::max(most, b->amount[i]);
			}
		}
	}
	if (most > 0.0f) {
		const int steps = int(std::ceil(most / (params_.max_step * h_ * h_)));
		last_.substeps = std::size_t(steps);
		const float inv_h = 1.0f / h_, inv_h2 = inv_h * inv_h;
		const float outside_end = params_.flow_outside + params_.flow_outside_taper;
		std::vector<float> deltas(pending.size() * kN);
		// Each block's neighbours, for its apron.
		std::vector<std::array<const Work *, 27>> around(pending.size());
		for (std::size_t n = 0; n < pending.size(); ++n) {
			const Work &b = *pending[n];
			for (int z = -1; z <= 1; ++z) {
				for (int y = -1; y <= 1; ++y) {
					for (int x = -1; x <= 1; ++x) {
						around[n][std::size_t((x + 1) + 3 * ((y + 1) + 3 * (z + 1)))] =
								x || y || z ? find(b.coord[0] + x, b.coord[1] + y, b.coord[2] + z) : &b;
					}
				}
			}
		}
		std::vector<Aabb> flowed_changed(pending.size());
		std::vector<float> flowed_offset(pending.size(), 0.0f);
		std::vector<std::size_t> flowed_count(pending.size(), 0);
		for (int step = 0; step < steps; ++step) {
			// Every block's step from the same phi (Jacobi), then all applied.
			parallel_for(pending.size(), [&](std::size_t n) {
				constexpr int kA = kB + 2; // with a one-sample apron
				float a[kA * kA * kA];
				const Work &b = *pending[n];
				// phi with its apron from the neighbouring blocks (the block's own edge where there
				// is none: out at the band's edge, where the flow has faded out anyway).
				for (int z = -1; z <= kB; ++z) {
					for (int y = -1; y <= kB; ++y) {
						for (int x = -1; x <= kB; ++x) {
							const int bx = floor_div(x, kB), by = floor_div(y, kB), bz = floor_div(z, kB);
							const Work *src = around[n][std::size_t((bx + 1) + 3 * ((by + 1) + 3 * (bz + 1)))];
							int lx = x - kB * bx, ly = y - kB * by, lz = z - kB * bz;
							if (!src) {
								src = &b;
								lx = std::clamp(x, 0, kB - 1);
								ly = std::clamp(y, 0, kB - 1);
								lz = std::clamp(z, 0, kB - 1);
							}
							a[(x + 1) + kA * ((y + 1) + kA * (z + 1))] = src->phi[lx + kB * (ly + kB * lz)];
						}
					}
				}
				auto at = [&](int x, int y, int z) { return a[(x + 1) + kA * ((y + 1) + kA * (z + 1))]; };
				float *delta = &deltas[n * kN];
				for (int i = 0; i < kN; ++i) {
					delta[i] = 0.0f;
					const float amount = b.amount[i];
					const float p = b.phi[i];
					if (amount <= 0.0f || p >= outside_end) {
						continue;
					}
					const int x = i % kB, y = (i / kB) % kB, z = i / (kB * kB);
					const float px = (at(x + 1, y, z) - at(x - 1, y, z)) * 0.5f * inv_h;
					const float py = (at(x, y + 1, z) - at(x, y - 1, z)) * 0.5f * inv_h;
					const float pz = (at(x, y, z + 1) - at(x, y, z - 1)) * 0.5f * inv_h;
					const float g2 = px * px + py * py + pz * pz;
					if (g2 < 1e-12f) {
						continue;
					}
					const float pxx = (at(x + 1, y, z) - 2.0f * p + at(x - 1, y, z)) * inv_h2;
					const float pyy = (at(x, y + 1, z) - 2.0f * p + at(x, y - 1, z)) * inv_h2;
					const float pzz = (at(x, y, z + 1) - 2.0f * p + at(x, y, z - 1)) * inv_h2;
					const float pxy = (at(x + 1, y + 1, z) - at(x + 1, y - 1, z) - at(x - 1, y + 1, z) + at(x - 1, y - 1, z)) *
							0.25f * inv_h2;
					const float pxz = (at(x + 1, y, z + 1) - at(x + 1, y, z - 1) - at(x - 1, y, z + 1) + at(x - 1, y, z - 1)) *
							0.25f * inv_h2;
					const float pyz = (at(x, y + 1, z + 1) - at(x, y + 1, z - 1) - at(x, y - 1, z + 1) + at(x, y - 1, z - 1)) *
							0.25f * inv_h2;
					// The level set's curvature kappa, convex parts only: sandpaper takes down what
					// stands proud.
					const float num = pxx * (py * py + pz * pz) + pyy * (px * px + pz * pz) + pzz * (px * px + py * py) -
							2.0f * (px * py * pxy + px * pz * pxz + py * pz * pyz);
					const float kappa_g = std::max(num, 0.0f) / g2; // kappa |grad phi|: second differences of phi
					const float kappa = kappa_g / std::sqrt(g2);
					// Level sets outside the surface move nearly at the speed of the surface beneath
					// them (the level-set method's velocity extension: for an edge, curving one way,
					// the surface's curvature under the level set at phi is kappa / (1 - phi kappa)),
					// so they keep their spacing as the surface recedes from them. Capped: moving
					// slower only spreads them apart, which is safe, and the explicit steps stay
					// stable. Inside, each moves by its own curvature, which is at least the
					// surface's there: they spread too, and old creases deep inside round off as
					// fast as they should.
					const float extension = p > 0.0f ? std::min(1.0f / std::max(1.0f - p * kappa, 1e-3f), kMaxExtension) : 1.0f;
					// Capped where phi is degenerate (a vanishing gradient): the explicit step is
					// stable for second differences, which kappa |grad phi| is, not beyond.
					const float speed = std::min(kappa_g * extension, 2.0f * inv_h);
					const float taper = std::clamp((outside_end - p) / params_.flow_outside_taper, 0.0f, 1.0f);
					delta[i] = amount / float(steps) * taper * speed;
				}
			});
			parallel_for(pending.size(), [&](std::size_t n) {
				Work &b = *pending[n];
				const float *delta = &deltas[n * kN];
				for (int i = 0; i < kN; ++i) {
					if (delta[i] > 0.0f) {
						b.phi[i] += delta[i];
						flowed_offset[n] = std::max(flowed_offset[n], b.phi[i] - b.F[i]);
						// The surface has come nearer: the weight band follows it (the flow only runs
						// where the tool pressed fully, so only the band limits w here).
						const float depth = std::min(std::fabs(b.F[i]), std::fabs(b.phi[i]));
						b.w[i] = std::max(b.w[i], std::clamp((w_end - depth) / params_.weight_taper, 0.0f, 1.0f));
						b.dirty = true;
						flowed_changed[n].include(sample_at(b.coord, i));
						flowed_count[n] += step == 0;
					}
				}
			});
		}
		for (std::size_t n = 0; n < pending.size(); ++n) {
			changed_.include(flowed_changed[n]);
			max_offset_ = std::max(max_offset_, flowed_offset[n]);
			last_.flowed += flowed_count[n];
		}
	}
	for (Work *b : pending) {
		std::fill(std::begin(b->amount), std::end(b->amount), 0.0f);
		b->pending = false;
	}
	if (changed_.empty()) {
		return {};
	}
	// The B-spline reaches 1.5 samples, and the flow's neighbours one more.
	const Aabb out = changed_.expanded(2.0f * h_ + 1e-3f);
	changed_ = Aabb();
	return out;
}

void SmoothingGrid::measure(Work &b) {
	// A sample's neighbours along each axis, from the next blocks at the edges.
	const Work *prev[3] = {find(b.coord[0] - 1, b.coord[1], b.coord[2]), find(b.coord[0], b.coord[1] - 1, b.coord[2]),
			find(b.coord[0], b.coord[1], b.coord[2] - 1)};
	const Work *next[3] = {find(b.coord[0] + 1, b.coord[1], b.coord[2]), find(b.coord[0], b.coord[1] + 1, b.coord[2]),
			find(b.coord[0], b.coord[1], b.coord[2] + 1)};
	const int stride[3] = {1, kB, kB * kB};
	float gradient = 0.0f, ramp = 0.0f, offset = 0.0f, w_hi = 0.0f;
	for (int i = 0; i < kN; ++i) {
		const int l[3] = {i % kB, (i / kB) % kB, i / (kB * kB)};
		float g2 = 0.0f, w2 = 0.0f, near_offset = std::fabs(b.phi[i] - b.F[i]);
		for (int ax = 0; ax < 3; ++ax) {
			float dphi = 0.0f, dw = 0.0f;
			for (int side : {-1, 1}) {
				const Work *src = &b;
				int j = i + side * stride[ax];
				if (l[ax] + side < 0 || l[ax] + side >= kB) {
					src = side < 0 ? prev[ax] : next[ax];
					j = i - side * (kB - 1) * stride[ax];
				}
				if (!src) {
					continue;
				}
				dphi = std::max(dphi, std::fabs(src->phi[j] - b.phi[i]));
				dw = std::max(dw, std::fabs(src->w[j] - b.w[i]));
				near_offset = std::max(near_offset, std::fabs(src->phi[j] - src->F[j]));
			}
			g2 += dphi * dphi;
			w2 += dw * dw;
		}
		// A B-spline's derivative is a weighted average of its samples' differences. Where w
		// ramps, phi~ - F is the offset near by plus, at a crease of F, up to a quarter sample
		// of the B-spline rounding it. Only where the field is read for its value (see
		// Params::measured_depth).
		if (b.phi[i] > -params_.measured_depth) {
			gradient = std::max(gradient, std::sqrt(g2) / h_);
			ramp = std::max(ramp, (near_offset + 0.25f * h_) * std::sqrt(w2) / h_);
		}
		offset = std::max(offset, std::fabs(b.phi[i] - b.F[i]));
		w_hi = std::max(w_hi, b.w[i]);
	}
	b.gradient = gradient;
	b.ramp = ramp;
	b.offset = offset;
	b.w_hi = w_hi;
}

std::shared_ptr<const Layer> SmoothingGrid::layer() {
	// Measure what changed, and the blocks whose differences reach into it.
	std::vector<Work *> remeasure;
	for (auto &[k, b] : blocks_) {
		if (b->dirty || !b->shared) {
			remeasure.push_back(b.get());
			for (int ax = 0; ax < 3; ++ax) {
				for (int side : {-1, 1}) {
					Layer::Coord c = b->coord;
					c[std::size_t(ax)] += side;
					if (Work *next = find(c[0], c[1], c[2])) {
						remeasure.push_back(next);
					}
				}
			}
		}
	}
	std::sort(remeasure.begin(), remeasure.end());
	remeasure.erase(std::unique(remeasure.begin(), remeasure.end()), remeasure.end());
	parallel_for(remeasure.size(), [&](std::size_t n) { measure(*remeasure[n]); });
	std::vector<std::pair<Layer::Coord, std::shared_ptr<const Layer::Block>>> list;
	list.reserve(blocks_.size());
	Layer::Measures m;
	// Where w~ is 0 the layer is the identity: only blocks with some weight, or next to one
	// (the B-spline reaches 1.5 samples across), bound anything.
	auto weighs = [&](const Work &b) {
		if (b.w_hi > 0.0f) {
			return true;
		}
		for (int ax = 0; ax < 3; ++ax) {
			for (int side : {-1, 1}) {
				Layer::Coord c = b.coord;
				c[std::size_t(ax)] += side;
				const Work *next = find(c[0], c[1], c[2]);
				if (next && next->w_hi > 0.0f) {
					return true;
				}
			}
		}
		return false;
	};
	for (auto &[k, b] : blocks_) {
		if (b->dirty || !b->shared) {
			auto block = std::make_shared<Layer::Block>();
			std::copy(std::begin(b->phi), std::end(b->phi), block->phi);
			std::copy(std::begin(b->w), std::end(b->w), block->w);
			block->finish();
			b->shared = std::move(block);
			b->dirty = false;
		}
		list.emplace_back(b->coord, b->shared);
		if (weighs(*b)) {
			m.gradient = std::max(m.gradient, b->gradient);
			m.ramp = std::max(m.ramp, b->ramp);
			m.max_offset = std::max(m.max_offset, b->offset);
		}
	}
	return std::make_shared<Layer>(h_, list, m);
}

} // namespace sdf::tools
