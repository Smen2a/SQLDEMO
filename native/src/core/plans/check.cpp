#include "plans/check.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace sdf::plans {

namespace {

constexpr float kArris = 1.5f;      // mm: a point this near two of the blank's faces is on an arris
constexpr float kAllowance = 0.75f; // mm more allowed there (the drawing's arrises are eased)
constexpr float kOwn = 0.3f;        // mm: a point this near a feature's cut is on its face
constexpr float kStep = 0.05f;      // mm: finite differences

float get(vec3 v, int axis) {
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

struct Grid {
	vec3 lo{0.0f};
	float h = 1.0f;
	int nx = 0, ny = 0, nz = 0;
	std::size_t size() const { return std::size_t(nx) * std::size_t(ny) * std::size_t(nz); }
	std::size_t index(int i, int j, int k) const { return std::size_t(i) + std::size_t(nx) * (std::size_t(j) + std::size_t(ny) * std::size_t(k)); }
	vec3 point(int i, int j, int k) const { return lo + vec3(float(i), float(j), float(k)) * h; }
};

struct UnionFind {
	std::vector<std::uint32_t> parent;
	explicit UnionFind(std::size_t n) : parent(n) {
		for (std::size_t i = 0; i < n; ++i) {
			parent[i] = std::uint32_t(i);
		}
	}
	std::uint32_t find(std::uint32_t a) {
		while (parent[a] != a) {
			parent[a] = parent[parent[a]];
			a = parent[a];
		}
		return a;
	}
	void unite(std::uint32_t a, std::uint32_t b) {
		a = find(a);
		b = find(b);
		if (a != b) {
			parent[std::max(a, b)] = std::min(a, b);
		}
	}
};

// The wood's outward normal: central differences.
vec3 gradient(const std::function<float(vec3)> &f, vec3 p) {
	const vec3 g(f(p + vec3(kStep, 0, 0)) - f(p - vec3(kStep, 0, 0)), f(p + vec3(0, kStep, 0)) - f(p - vec3(0, kStep, 0)),
			f(p + vec3(0, 0, kStep)) - f(p - vec3(0, 0, kStep)));
	const float l = gl::length(g);
	return l > 1e-12f ? g / l : vec3(0.0f);
}

vec3 unit(vec3 v) {
	const float l = gl::length(v);
	return l > 1e-12f ? v / l : vec3(0.0f);
}

// How far off a point of the drawing's surface may be: more along the blank's arrises.
float allowed(vec3 q, vec3 size, float tolerance) {
	int near = 0;
	for (int a = 0; a < 3; ++a) {
		const float v = get(q, a), m = get(size, a);
		near += v < kArris || v > m - kArris ? 1 : 0;
	}
	return near >= 2 ? tolerance + kAllowance : tolerance;
}

// The feature whose cut a point of the drawing's surface lies on, or -1 (the blank).
int owner_at(const Body &drawn, const std::vector<int> &owners, vec3 x) {
	int best = -1;
	float nearest = kOwn;
	for (std::size_t e = 0; e < drawn.edits().size() && e < owners.size(); ++e) {
		if (owners[e] < 0) {
			continue;
		}
		const float d = std::fabs(drawn.edits()[e].prim.eval(x));
		if (d < nearest) {
			nearest = d;
			best = owners[e];
		}
	}
	return best;
}

struct Sample {
	std::size_t cell;
	vec3 p;       // on the wood's surface (proud) or the drawing's (short)
	float by;     // how far off
	float weight; // its share of the surface: 1 - its grid point's distance from it, in spacings
};

} // namespace

Check check_part(const Part &part, const std::function<float(vec3)> &wood, const Aabb &wood_bounds, float tolerance,
		float spacing) {
	const auto start = std::chrono::steady_clock::now();
	Check out;
	std::vector<int> owners;
	const vec3 s = part.size;
	// Its cuts run far past the blank, so that from inside one the nearest face is its own.
	const Body drawn = part_solid(part, 0, &owners, std::max(s.x, std::max(s.y, s.z)) + 10.0f);
	const float h = std::max(spacing, 0.1f);

	Aabb box;
	box.include(vec3(0.0f));
	box.include(s);
	if (!wood_bounds.empty()) {
		box.include(wood_bounds);
	}
	box = box.expanded(2.0f * h);
	Grid g;
	g.lo = box.lo;
	g.h = h;
	const vec3 extent = box.size();
	g.nx = int(std::ceil(extent.x / h)) + 1;
	g.ny = int(std::ceil(extent.y / h)) + 1;
	g.nz = int(std::ceil(extent.z / h)) + 1;
	const std::size_t n = g.size();
	out.samples = int(n);

	// Both fields over the grid; wood beyond the drawing (proud) and the drawing's inside
	// with no wood (short), deeper than allowed, counted as volume.
	std::vector<float> da(n), di(n);
	for (int k = 0; k < g.nz; ++k) {
		for (int j = 0; j < g.ny; ++j) {
			for (int i = 0; i < g.nx; ++i) {
				const vec3 p = g.point(i, j, k);
				const std::size_t c = g.index(i, j, k);
				da[c] = wood(p);
				di[c] = drawn.distance(p);
			}
		}
	}
	enum : std::uint8_t { kProudVolume = 1, kProudSample = 2, kShortVolume = 4, kShortSample = 8 };
	std::vector<std::uint8_t> flag(n, 0);
	std::vector<float> share(n, 0.0f); // how much of the cell round a grid point is off, 0 to 1
	std::vector<Sample> proud, short_;
	const float cell = h * h * h;
	auto part_of_cell = [&](float d) { return std::clamp(0.5f + d / h, 0.0f, 1.0f); }; // of a cell, beyond d
	for (int k = 0; k < g.nz; ++k) {
		for (int j = 0; j < g.ny; ++j) {
			for (int i = 0; i < g.nx; ++i) {
				const vec3 p = g.point(i, j, k);
				const std::size_t c = g.index(i, j, k);
				const float a = da[c], d = di[c];
				const float allow = allowed(p, s, tolerance);
				// Wood (inside the wood's surface) beyond the drawing by more than allowed;
				// the drawing's inside, deeper than allowed, with no wood.
				const float beyond = std::min(part_of_cell(-a), part_of_cell(d - allow));
				const float gone = std::min(part_of_cell(a), part_of_cell(-d - allow));
				if (beyond > 0.0f) {
					flag[c] |= kProudVolume;
					share[c] = beyond;
					out.proud += beyond * cell;
				} else if (gone > 0.0f) {
					flag[c] |= kShortVolume;
					share[c] = gone;
					out.short_ += gone * cell;
				}
				// Near the wood's surface: that surface, how far beyond the drawing. (Where the
				// wood's field only dips towards zero in the air, as it does past a cut run out
				// beyond a face or along the seam where two cuts barely overlap, the point it
				// comes to is no surface, with no wood behind it: left out.)
				if (std::fabs(a) < h) {
					const vec3 grad = gradient(wood, p);
					const vec3 q = p - grad * a;
					const float off = drawn.distance(q);
					if (off > tolerance && std::fabs(wood(q)) < 0.25f * h && wood(q - unit(grad) * (0.5f * h)) < 0.0f) {
						const vec3 on = q - unit(drawn.normal(q, kStep)) * off;
						if (off > allowed(on, s, tolerance)) {
							flag[c] |= kProudSample;
							proud.push_back({c, q, off, 1.0f - std::fabs(a) / h});
						}
					}
				}
				// Near the drawing's surface: that surface, how far the wood is short of it.
				if (std::fabs(d) < h) {
					const vec3 q = p - unit(drawn.normal(p, kStep)) * d;
					const float off = wood(q);
					if (off > allowed(q, s, tolerance)) {
						flag[c] |= kShortSample;
						short_.push_back({c, q, off, 1.0f - std::fabs(d) / h});
					}
				}
			}
		}
	}

	// Each kind's cells joined with their neighbours into spots.
	auto spots = [&](Spot::Kind kind, std::uint8_t mask, std::vector<Sample> &samples) {
		UnionFind sets(n);
		for (int k = 0; k < g.nz; ++k) {
			for (int j = 0; j < g.ny; ++j) {
				for (int i = 0; i < g.nx; ++i) {
					const std::size_t c = g.index(i, j, k);
					if (!(flag[c] & mask)) {
						continue;
					}
					for (int dk = 0; dk <= 1; ++dk) {
						for (int dj = dk == 0 ? 0 : -1; dj <= 1; ++dj) {
							for (int dx = (dk == 0 && dj == 0) ? 1 : -1; dx <= 1; ++dx) {
								const int x = i + dx, y = j + dj, z = k + dk;
								if (x < 0 || y < 0 || x >= g.nx || y >= g.ny || z >= g.nz) {
									continue;
								}
								const std::size_t o = g.index(x, y, z);
								if (flag[o] & mask) {
									sets.unite(std::uint32_t(c), std::uint32_t(o));
								}
							}
						}
					}
				}
			}
		}
		std::unordered_map<std::uint32_t, std::vector<const Sample *>> by_root;
		for (const Sample &sample : samples) {
			by_root[sets.find(std::uint32_t(sample.cell))].push_back(&sample);
		}
		std::unordered_map<std::uint32_t, float> volume;
		const std::uint8_t deep = kind == Spot::Kind::Proud ? kProudVolume : kShortVolume;
		for (std::size_t c = 0; c < n; ++c) {
			if (flag[c] & deep) {
				volume[sets.find(std::uint32_t(c))] += share[c] * cell;
			}
		}
		for (auto &[root, members] : by_root) {
			if (members.size() < 2) {
				continue; // (a lone point: a crease's noise)
			}
			Spot spot;
			spot.kind = kind;
			for (const Sample *m : members) {
				spot.most = std::max(spot.most, m->by);
			}
			// Where it is worst: of the places near the worst (a spot is often as far off all
			// along a face), the one nearest their middle, not a corner.
			const float near_most = spot.most - std::max(0.05f, 0.1f * spot.most);
			vec3 middle(0.0f);
			int count = 0;
			for (const Sample *m : members) {
				if (m->by >= near_most) {
					middle = middle + m->p;
					++count;
				}
			}
			middle = middle / float(std::max(count, 1));
			// (Proud: not where the drawing's nearest surface is ambiguous, midway between
			// two, as on the middle line of a mortise not yet chopped.)
			std::vector<const Sample *> near;
			for (const Sample *m : members) {
				if (m->by >= near_most) {
					near.push_back(m);
				}
			}
			std::sort(near.begin(), near.end(), [&](const Sample *a, const Sample *b) {
				return gl::length(a->p - middle) < gl::length(b->p - middle);
			});
			const Sample *worst = near.front();
			if (kind == Spot::Kind::Proud) {
				for (const Sample *m : near) {
					if (gl::length(unit(drawn.normal(m->p, kStep))) > 0.5f) {
						worst = m;
						break;
					}
				}
			}
			for (const Sample *m : members) {
				spot.area += m->weight * h * h; // (the grid points within a spacing of a surface, each weighted by nearness, cover it once)
			}
			spot.volume = volume.count(root) ? volume[root] : 0.0f;
			vec3 at = worst->p;
			if (kind == Spot::Kind::Proud) {
				at = at - unit(drawn.normal(at, kStep)) * drawn.distance(at);
			}
			spot.at = at;
			spot.normal = unit(drawn.normal(at, kStep));
			spot.feature = owner_at(drawn, owners, at);
			const std::size_t stride = (members.size() + Spot::kDots - 1) / Spot::kDots;
			for (std::size_t m = 0; m < members.size(); m += stride) {
				spot.dots.push_back(members[m]->p);
				spot.by.push_back(members[m]->by);
			}
			out.spots.push_back(std::move(spot));
		}
	};
	spots(Spot::Kind::Proud, kProudVolume | kProudSample, proud);
	spots(Spot::Kind::Short, kShortVolume | kShortSample, short_);
	std::sort(out.spots.begin(), out.spots.end(), [](const Spot &a, const Spot &b) { return a.most > b.most; });
	out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return out;
}

} // namespace sdf::plans
