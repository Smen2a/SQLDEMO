#include "pieces/parts.h"

#include "util/half.h"
#include "util/parallel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>

namespace sdf {

namespace {

constexpr int kN = Adf::kSide, kCells = kN - 1, kS = Adf::kBrickSamples;

int index(int x, int y, int z) {
	return x + kN * (y + kN * z);
}

bool overlaps(const Aabb &a, vec3 lo, float size) {
	return a.lo.x <= lo.x + size && a.hi.x >= lo.x && a.lo.y <= lo.y + size && a.hi.y >= lo.y && a.lo.z <= lo.z + size &&
			a.hi.z >= lo.z;
}

float axis_of(vec3 v, int axis) {
	return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

struct UnionFind {
	std::vector<std::int32_t> parent;
	explicit UnionFind(std::size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
	std::int32_t find(std::int32_t i) {
		while (parent[std::size_t(i)] != i) {
			parent[std::size_t(i)] = parent[std::size_t(parent[std::size_t(i)])];
			i = parent[std::size_t(i)];
		}
		return i;
	}
	void join(std::int32_t a, std::int32_t b) {
		a = find(a);
		b = find(b);
		if (a != b) {
			parent[std::size_t(std::max(a, b))] = std::min(a, b);
		}
	}
};

// Where two inside samples lie: in a plain brick, whose reconstruction matches the field to
// the ADF's tolerance near the surface (so that a gap it draws has samples in it), or in an
// exact leaf, whose brick strays up to `error` and whose own short tape gives the field.
struct Where {
	bool exact = false;
	float error = 0.0f;
	const std::uint32_t *tape = nullptr; // null: the octree's (across leaves)
	std::size_t count = 0;
	bool base = true;
};

// Whether the segment between two inside samples (p with value a, q with b) lies in the
// material all along.
struct Joiner {
	const Body &body;
	const Octree &octree;
	float lipschitz;
	float finest;
	std::atomic<std::size_t> &checks;

	bool operator()(vec3 p, float a, vec3 q, float b, const Where &where) const {
		if (!where.exact) {
			return true; // as drawn: a gap the samples miss is not drawn either
		}
		if (-(a + b) > lipschitz * gl::length(q - p) * 1.001f) {
			return true; // the field cannot climb to zero between them
		}
		if (std::max(a, b) < -where.error) {
			return true; // the brick (linear along the segment) stays further inside than it strays
		}
		++checks;
		return exact(p, a, q, b, where, 0);
	}

	// The field at the middle, then at the quarters: a gap a quarter of the segment wide (or
	// `finest`) shows.
	bool exact(vec3 p, float a, vec3 q, float b, const Where &where, int depth) const {
		const float h = gl::length(q - p);
		if (-(a + b) > lipschitz * h * 1.001f || h < finest || depth >= 2) {
			return true;
		}
		const vec3 m = (p + q) * 0.5f;
		const float c = where.tape ? Octree::eval_tape(body, where.base, where.tape, where.count, m).d
								   : octree.distance(body, m);
		if (c >= 0.0f) {
			return false; // air between them: a kerf
		}
		return exact(p, a, m, c, where, depth + 1) && exact(m, c, q, b, where, depth + 1);
	}
};

// Calls visit(a, b, axis) for every pair of leaves sharing a face, a below b along the axis
// (the octree face procedure), among the nodes overlapping `region` (all without one).
// Nodes: an octree's, with lo, size and child (the first of 8, or -1).
template <typename Node, typename Visit>
struct FacePairs {
	const std::vector<Node> &nodes;
	const Aabb *region;
	Visit &visit;

	bool in(int n) const {
		const Node &c = nodes[std::size_t(n)];
		return !region || overlaps(*region, c.lo, c.size);
	}

	void cell(int n) {
		const Node &c = nodes[std::size_t(n)];
		if (c.child < 0 || !in(n)) {
			return;
		}
		for (int i = 0; i < 8; ++i) {
			cell(c.child + i);
		}
		for (int axis = 0; axis < 3; ++axis) {
			const int bit = 1 << axis;
			for (int i = 0; i < 8; ++i) {
				if (!(i & bit)) {
					face(c.child + i, c.child + (i | bit), axis);
				}
			}
		}
	}

	void face(int a, int b, int axis) {
		if (!in(a) || !in(b)) {
			return;
		}
		const Node &na = nodes[std::size_t(a)], &nb = nodes[std::size_t(b)];
		if (na.child < 0 && nb.child < 0) {
			visit(a, b, axis);
			return;
		}
		// a's children on its upper face against b's on its lower one (a leaf stands for all
		// four of its quarters).
		const int bit = 1 << axis;
		for (int i = 0; i < 8; ++i) {
			if (i & bit) {
				face(na.child < 0 ? a : na.child + i, nb.child < 0 ? b : nb.child + (i ^ bit), axis);
			}
		}
	}
};

} // namespace

int Parts::count(double least) const {
	return int(std::count_if(parts.begin(), parts.end(), [&](const Part &p) { return p.volume >= least; }));
}

Parts find_parts(const Body &body, const Octree &octree, const Adf &adf, const Aabb *region, float finest) {
	const auto start = std::chrono::steady_clock::now();
	Parts out;
	const std::vector<Adf::Node> &nodes = adf.nodes();
	if (nodes.empty()) {
		return out;
	}
	out.node_part.assign(nodes.size(), -1);
	out.slot_group.assign(adf.brick_slots(), -1);
	out.group.assign(adf.brick_slots() * std::size_t(kS), Parts::kOutside);

	// The leaves looked at: solid ones, and ones with bricks.
	std::vector<int> bricks, solids;
	std::vector<std::int32_t> solid_of(nodes.size(), -1), brick_of(nodes.size(), -1);
	for (std::size_t i = 0; i < nodes.size(); ++i) {
		const Adf::Node &n = nodes[i];
		if (n.child >= 0 || (region && !overlaps(*region, n.lo, n.size))) {
			continue;
		}
		if (n.brick == Adf::kSolid) {
			solid_of[i] = std::int32_t(solids.size());
			solids.push_back(int(i));
		} else if (n.brick >= 0) {
			brick_of[i] = std::int32_t(bricks.size());
			bricks.push_back(int(i));
		}
	}
	auto inside_region = [&](const Adf::Node &n) {
		return !region || (n.lo.x >= region->lo.x && n.lo.y >= region->lo.y && n.lo.z >= region->lo.z &&
								  n.lo.x + n.size <= region->hi.x && n.lo.y + n.size <= region->hi.y &&
								  n.lo.z + n.size <= region->hi.z);
	};
	auto position = [&](int node, int i) {
		const Adf::Node &n = nodes[std::size_t(node)];
		return n.lo + vec3(float(i % kN), float((i / kN) % kN), float(i / (kN * kN))) * (n.size / float(kCells));
	};
	const std::vector<std::uint16_t> &values = adf.brick_values();
	auto value_at = [&](int node, int i) {
		return half_to_float(values[std::size_t(nodes[std::size_t(node)].brick) * kS + std::size_t(i)]);
	};
	struct Sum {
		double volume = 0.0, moment[3] = {0.0, 0.0, 0.0};
		Aabb bounds;
		vec3 inside{0.0f};
		bool touches = false;
		void add(vec3 lo, float size) {
			const double v = double(size) * size * size;
			const vec3 c = lo + vec3(size * 0.5f);
			volume += v;
			moment[0] += v * c.x;
			moment[1] += v * c.y;
			moment[2] += v * c.z;
			bounds.include(lo);
			bounds.include(lo + vec3(size));
		}
	};

	// Within each brick, in parallel: its inside samples in groups (joined within the
	// brick), and each group's volume.
	std::atomic<std::size_t> checks{0};
	const Joiner join{body, octree, std::max(body.lipschitz(), 1.0f), finest, checks};
	std::vector<Where> where(bricks.size());
	std::vector<std::vector<Sum>> group_sums(bricks.size());
	parallel_for(bricks.size(), [&](std::size_t k) {
		const int node = bricks[k];
		const Adf::Node &n = nodes[std::size_t(node)];
		float v[kS];
		const std::uint16_t *brick = values.data() + std::size_t(n.brick) * kS;
		for (int i = 0; i < kS; ++i) {
			v[i] = half_to_float(brick[i]);
		}
		if (n.exact()) {
			const Adf::ExactCell &cell = adf.exact_cells()[std::size_t(n.material - Adf::kExactBase)];
			where[k] = {true, cell.error + 1e-3f, adf.exact_tape().data() + cell.offset, cell.count, cell.base};
		}
		const float voxel = n.size / float(kCells);
		std::int16_t r[kS];
		for (int i = 0; i < kS; ++i) {
			r[i] = v[i] < 0.0f ? std::int16_t(i) : std::int16_t(-1);
		}
		auto find = [&](int i) {
			while (r[i] != i) {
				r[i] = r[r[i]];
				i = r[i];
			}
			return i;
		};
		for (int z = 0; z < kN; ++z) {
			for (int y = 0; y < kN; ++y) {
				for (int x = 0; x < kN; ++x) {
					const int i = index(x, y, z);
					if (v[i] >= 0.0f) {
						continue;
					}
					const int step[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
					for (const auto &st : step) {
						if (x + st[0] >= kN || y + st[1] >= kN || z + st[2] >= kN) {
							continue;
						}
						const int j = index(x + st[0], y + st[1], z + st[2]);
						if (v[j] >= 0.0f) {
							continue;
						}
						const int a = find(i), b = find(j);
						if (a == b) {
							continue;
						}
						const vec3 p = n.lo + vec3(float(x), float(y), float(z)) * voxel;
						const vec3 q = p + vec3(float(st[0]), float(st[1]), float(st[2])) * voxel;
						if (join(p, v[i], q, v[j], where[k])) {
							r[std::max(a, b)] = std::int16_t(std::min(a, b));
						}
					}
				}
			}
		}
		std::uint16_t *g = out.group.data() + std::size_t(n.brick) * kS;
		std::vector<Sum> &sums = group_sums[k];
		std::int16_t number[kS];
		std::fill(number, number + kS, std::int16_t(-1));
		const bool touches = !inside_region(n);
		for (int i = 0; i < kS; ++i) {
			if (r[i] < 0) {
				continue;
			}
			const int top = find(i);
			if (number[top] < 0) {
				number[top] = std::int16_t(sums.size());
				sums.emplace_back();
				sums.back().inside = position(node, i);
				sums.back().touches = touches;
			}
			g[i] = std::uint16_t(number[top]);
		}
		for (int z = 0; z < kCells; ++z) {
			for (int y = 0; y < kCells; ++y) {
				for (int x = 0; x < kCells; ++x) {
					float total = 0.0f;
					int first = -1;
					for (int c = 0; c < 8; ++c) {
						const int i = index(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1));
						total += v[i];
						first = first < 0 && v[i] < 0.0f ? i : first;
					}
					if (total < 0.0f && first >= 0) {
						sums[g[first]].add(n.lo + vec3(float(x), float(y), float(z)) * voxel, voxel);
					}
				}
			}
		}
	});

	// Groups numbered: solid leaves first, then each brick's.
	std::int32_t groups = std::int32_t(solids.size());
	for (std::size_t k = 0; k < bricks.size(); ++k) {
		out.slot_group[std::size_t(nodes[std::size_t(bricks[k])].brick)] = groups;
		groups += std::int32_t(group_sums[k].size());
	}
	auto group_of = [&](int node, int i) -> std::int32_t {
		const int slot = nodes[std::size_t(node)].brick;
		const std::uint16_t g = out.group[std::size_t(slot) * kS + std::size_t(i)];
		return g == Parts::kOutside ? -1 : out.slot_group[std::size_t(slot)] + g;
	};

	// Across the faces between leaves: the pairs of leaves first, then (in parallel) the
	// groups each pair joins.
	struct Pair {
		int a, b, axis;
	};
	std::vector<Pair> pairs;
	auto collect = [&](int a, int b, int axis) {
		const bool la = solid_of[std::size_t(a)] >= 0 || brick_of[std::size_t(a)] >= 0;
		const bool lb = solid_of[std::size_t(b)] >= 0 || brick_of[std::size_t(b)] >= 0;
		if (la && lb) {
			pairs.push_back({a, b, axis});
		}
	};
	FacePairs<Adf::Node, decltype(collect)>{nodes, region, collect}.cell(0);
	// Sample (u, w) of a leaf's face (lower side 0, upper 1) across `axis`.
	auto face_sample = [](int axis, int side, int u, int w) {
		int c[3];
		c[axis] = side ? kCells : 0;
		c[(axis + 1) % 3] = u;
		c[(axis + 2) % 3] = w;
		return index(c[0], c[1], c[2]);
	};
	std::vector<std::vector<std::array<std::int32_t, 2>>> joins(pairs.size());
	parallel_for(pairs.size(), [&](std::size_t k) {
		const int a = pairs[k].a, b = pairs[k].b, axis = pairs[k].axis;
		std::vector<std::array<std::int32_t, 2>> &list = joins[k];
		auto listed = [&](std::int32_t x, std::int32_t y) {
			return std::any_of(list.begin(), list.end(), [&](const std::array<std::int32_t, 2> &j) {
				return (j[0] == x && j[1] == y) || (j[0] == y && j[1] == x);
			});
		};
		auto add = [&](std::int32_t x, std::int32_t y) {
			if (x >= 0 && y >= 0 && !listed(x, y)) {
				list.push_back({x, y});
			}
		};
		const Adf::Node &na = nodes[std::size_t(a)], &nb = nodes[std::size_t(b)];
		const bool solid_a = na.brick == Adf::kSolid, solid_b = nb.brick == Adf::kSolid;
		const int u_axis = (axis + 1) % 3, w_axis = (axis + 2) % 3;
		if (solid_a && solid_b) {
			add(solid_of[std::size_t(a)], solid_of[std::size_t(b)]);
			return;
		}
		if (solid_a || solid_b) {
			// The brick's samples on the solid leaf's face are in its material.
			const int solid = solid_a ? a : b, brick = solid_a ? b : a;
			const Adf::Node &s = nodes[std::size_t(solid)];
			const float eps = s.size * 1e-4f;
			for (int u = 0; u < kN; ++u) {
				for (int w = 0; w < kN; ++w) {
					const int i = face_sample(axis, solid_a ? 0 : 1, u, w);
					const std::int32_t gi = group_of(brick, i);
					if (gi < 0) {
						continue;
					}
					const vec3 p = position(brick, i);
					const float pu = axis_of(p, u_axis), pw = axis_of(p, w_axis);
					if (pu >= axis_of(s.lo, u_axis) - eps && pu <= axis_of(s.lo, u_axis) + s.size + eps &&
							pw >= axis_of(s.lo, w_axis) - eps && pw <= axis_of(s.lo, w_axis) + s.size + eps) {
						add(solid_of[std::size_t(solid)], gi);
					}
				}
			}
			return;
		}
		if (std::fabs(na.size - nb.size) <= na.size * 1e-4f) {
			// The same samples on both sides.
			for (int u = 0; u < kN; ++u) {
				for (int w = 0; w < kN; ++w) {
					add(group_of(a, face_sample(axis, 1, u, w)), group_of(b, face_sample(axis, 0, u, w)));
				}
			}
			return;
		}
		// Different sizes: each inside sample on the face joins the nearest one across it (both
		// ways, so that neither side's samples are left out).
		const Where &wa = where[std::size_t(brick_of[std::size_t(a)])], &wb = where[std::size_t(brick_of[std::size_t(b)])];
		const Where across_leaves{wa.exact || wb.exact, std::max(wa.error, wb.error)};
		auto across = [&](int from, int from_side, int to, int to_side, bool within_to) {
			const Adf::Node &t = nodes[std::size_t(to)];
			const float tv = t.size / float(kCells), eps = 1e-4f * float(kCells);
			for (int u = 0; u < kN; ++u) {
				for (int w = 0; w < kN; ++w) {
					const int i = face_sample(axis, from_side, u, w);
					const std::int32_t gi = group_of(from, i);
					if (gi < 0) {
						continue;
					}
					const vec3 p = position(from, i);
					const float ru = (axis_of(p, u_axis) - axis_of(t.lo, u_axis)) / tv;
					const float rw = (axis_of(p, w_axis) - axis_of(t.lo, w_axis)) / tv;
					if (within_to && (ru < -eps || ru > float(kCells) + eps || rw < -eps || rw > float(kCells) + eps)) {
						continue; // off the smaller leaf's face
					}
					const int j = face_sample(axis, to_side, std::clamp(int(std::lround(ru)), 0, kCells),
							std::clamp(int(std::lround(rw)), 0, kCells));
					const std::int32_t gj = group_of(to, j);
					if (gj < 0 || listed(gi, gj)) {
						continue;
					}
					if (join(p, value_at(from, i), position(to, j), value_at(to, j), across_leaves)) {
						add(gi, gj);
					}
				}
			}
		};
		const bool a_small = na.size < nb.size;
		const int small = a_small ? a : b, big = a_small ? b : a;
		const int small_side = a_small ? 1 : 0, big_side = a_small ? 0 : 1;
		across(small, small_side, big, big_side, false);
		across(big, big_side, small, small_side, true);
	});
	UnionFind uf(std::size_t(std::max(groups, 1)));
	for (const auto &list : joins) {
		for (const auto &j : list) {
			uf.join(j[0], j[1]);
		}
	}

	// Parts: each group's, summed, then numbered largest first.
	out.group_part.assign(std::size_t(groups), -1);
	std::vector<std::int32_t> part_of_root(std::size_t(groups), -1);
	std::vector<Sum> sums;
	std::vector<char> seen;
	auto merge = [&](std::int32_t gid, const Sum &s) {
		const std::int32_t r = uf.find(gid);
		if (part_of_root[std::size_t(r)] < 0) {
			part_of_root[std::size_t(r)] = std::int32_t(sums.size());
			sums.emplace_back();
			sums.back().inside = s.inside;
		}
		const std::int32_t part = part_of_root[std::size_t(r)];
		out.group_part[std::size_t(gid)] = part;
		Sum &t = sums[std::size_t(part)];
		t.volume += s.volume;
		for (int a = 0; a < 3; ++a) {
			t.moment[a] += s.moment[a];
		}
		t.bounds.include(s.bounds);
		t.touches = t.touches || s.touches;
	};
	for (std::size_t k = 0; k < solids.size(); ++k) {
		const Adf::Node &node = nodes[std::size_t(solids[k])];
		Sum s;
		s.inside = node.lo + vec3(node.size * 0.5f);
		s.touches = !inside_region(node);
		s.add(node.lo, node.size);
		merge(std::int32_t(k), s);
	}
	for (std::size_t k = 0; k < bricks.size(); ++k) {
		const std::int32_t first = out.slot_group[std::size_t(nodes[std::size_t(bricks[k])].brick)];
		for (std::size_t j = 0; j < group_sums[k].size(); ++j) {
			merge(first + std::int32_t(j), group_sums[k][j]);
		}
	}
	std::vector<std::int32_t> order(sums.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(),
			[&](std::int32_t a, std::int32_t b) { return sums[std::size_t(a)].volume > sums[std::size_t(b)].volume; });
	std::vector<std::int32_t> rank(sums.size());
	for (std::size_t i = 0; i < order.size(); ++i) {
		const Sum &s = sums[std::size_t(order[i])];
		rank[std::size_t(order[i])] = std::int32_t(i);
		Parts::Part part;
		part.volume = s.volume;
		if (s.volume > 0.0) {
			part.centre = vec3(float(s.moment[0] / s.volume), float(s.moment[1] / s.volume), float(s.moment[2] / s.volume));
		}
		part.bounds = s.bounds;
		part.inside = s.inside;
		part.touches_region = s.touches;
		out.parts.push_back(part);
	}
	for (std::int32_t &p : out.group_part) {
		p = p < 0 ? p : rank[std::size_t(p)];
	}
	for (std::size_t k = 0; k < solids.size(); ++k) {
		out.node_part[std::size_t(solids[k])] = out.group_part[k];
	}
	out.exact_checks = checks.load();
	out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	return out;
}

Island find_island(const Body &body, const Octree &octree, const Adf &adf, const Aabb &near, double least, float margin,
		float finest) {
	Island out;
	if (adf.nodes().empty() || near.empty()) {
		return out;
	}
	// Round the cut; then, if parts there reach beyond it, grown to hold them (a chip cut free
	// is usually a little larger than the last cut round it); then the whole body.
	Aabb looking = near;
	for (int look = 0; look < 3; ++look) {
		const bool whole = look == 2;
		out.parts = find_parts(body, octree, adf, whole ? nullptr : &looking, finest);
		out.region = whole ? Aabb::infinite() : looking;
		++out.passes;
		out.ms += out.parts.ms;
		if (out.parts.count(least) <= 1) {
			return out; // one piece (and perhaps crumbs)
		}
		// The smallest part big enough to count that the region holds whole (with the whole
		// body looked at, the smallest there is).
		for (int p = int(out.parts.parts.size()) - 1; p >= 0; --p) {
			const Parts::Part &part = out.parts.parts[std::size_t(p)];
			const Aabb room = part.bounds.expanded(margin);
			const bool held = whole || (!part.touches_region && room.lo.x >= near.lo.x && room.lo.y >= near.lo.y &&
												 room.lo.z >= near.lo.z && room.hi.x <= near.hi.x &&
												 room.hi.y <= near.hi.y && room.hi.z <= near.hi.z);
			if (part.volume >= least && held) {
				out.island = p;
				return out;
			}
		}
		if (look == 0) {
			Aabb grown = looking;
			for (std::size_t p = 1; p < out.parts.parts.size(); ++p) {
				if (out.parts.parts[p].volume >= least) {
					grown.include(out.parts.parts[p].bounds.expanded(2.0f * margin));
				}
			}
			const vec3 was = looking.size(), now = grown.size();
			if (now.x * now.y * now.z <= was.x * was.y * was.z * 1.0001f) {
				look = 1; // it cannot grow: the whole body next
			}
			looking = grown;
		}
	}
	return out;
}

CutOut cut_out(const Body &body, const Octree &octree, const Adf &adf, const Parts &parts, int island, float margin,
		float finest) {
	const auto start = std::chrono::steady_clock::now();
	CutOut out;
	if (island < 0 || std::size_t(island) >= parts.parts.size() || adf.nodes().empty()) {
		out.failed = "no island";
		return out;
	}
	const std::vector<Adf::Node> &adf_nodes = adf.nodes();
	const std::vector<std::uint16_t> &values = adf.brick_values();
	const Aabb bounds = parts.parts[std::size_t(island)].bounds.expanded(margin);
	const float root_size = std::max(bounds.size().x, std::max(bounds.size().y, bounds.size().z));
	const vec3 root_lo = bounds.centre() - vec3(root_size * 0.5f);
	const Aabb root{root_lo, root_lo + vec3(root_size)};
	constexpr unsigned kIsland = 1, kRest = 2;

	// Whose material each ADF node holds, over the root: bit 1 the island's, bit 2 anything
	// else's (inside samples of other parts, or of none: those lie outside what was looked at).
	std::vector<std::uint8_t> mask(adf_nodes.size(), 0);
	std::vector<std::int32_t> order{0};
	for (std::size_t k = 0; k < order.size(); ++k) {
		const Adf::Node &n = adf_nodes[std::size_t(order[k])];
		if (n.child >= 0) {
			for (int c = 0; c < 8; ++c) {
				const Adf::Node &ch = adf_nodes[std::size_t(n.child + c)];
				if (overlaps(root, ch.lo, ch.size)) {
					order.push_back(n.child + c);
				}
			}
		}
	}
	for (std::size_t k = order.size(); k-- > 0;) {
		const int i = order[k];
		const Adf::Node &n = adf_nodes[std::size_t(i)];
		std::uint8_t m = 0;
		if (n.child >= 0) {
			for (int c = 0; c < 8; ++c) {
				m |= mask[std::size_t(n.child + c)];
			}
		} else if (n.brick == Adf::kSolid) {
			m = parts.of_node(i) == island ? kIsland : kRest;
		} else if (n.brick >= 0) {
			const std::uint16_t *brick = values.data() + std::size_t(n.brick) * kS;
			for (int j = 0; j < kS && m != (kIsland | kRest); ++j) {
				if (half_to_float(brick[j]) < 0.0f) {
					m |= parts.of_sample(n.brick, j) == island ? kIsland : kRest;
				}
			}
		}
		mask[std::size_t(i)] = m;
	}
	// Whose material the samples in a box show.
	auto material = [&](vec3 lo, vec3 hi) {
		unsigned bits = 0;
		std::vector<int> stack{0};
		while (!stack.empty() && bits != (kIsland | kRest)) {
			const int i = stack.back();
			stack.pop_back();
			const Adf::Node &n = adf_nodes[std::size_t(i)];
			const vec3 nhi = n.lo + vec3(n.size);
			if (!mask[std::size_t(i)] || nhi.x < lo.x || nhi.y < lo.y || nhi.z < lo.z || n.lo.x > hi.x || n.lo.y > hi.y ||
					n.lo.z > hi.z) {
				continue;
			}
			if (n.lo.x >= lo.x && n.lo.y >= lo.y && n.lo.z >= lo.z && nhi.x <= hi.x && nhi.y <= hi.y && nhi.z <= hi.z) {
				bits |= mask[std::size_t(i)];
				continue;
			}
			if (n.child >= 0) {
				for (int c = 0; c < 8; ++c) {
					stack.push_back(n.child + c);
				}
				continue;
			}
			if (n.brick == Adf::kSolid) {
				bits |= mask[std::size_t(i)];
				continue;
			}
			// The brick's samples inside the box.
			const float voxel = n.size / float(kCells);
			int from[3], to[3];
			for (int a = 0; a < 3; ++a) {
				from[a] = std::clamp(int(std::ceil((axis_of(lo, a) - axis_of(n.lo, a)) / voxel - 1e-4f)), 0, kCells);
				to[a] = std::clamp(int(std::floor((axis_of(hi, a) - axis_of(n.lo, a)) / voxel + 1e-4f)), 0, kCells);
			}
			const std::uint16_t *brick = values.data() + std::size_t(n.brick) * kS;
			for (int z = from[2]; z <= to[2]; ++z) {
				for (int y = from[1]; y <= to[1]; ++y) {
					for (int x = from[0]; x <= to[0]; ++x) {
						const int j = index(x, y, z);
						if (half_to_float(brick[j]) < 0.0f) {
							bits |= parts.of_sample(n.brick, j) == island ? kIsland : kRest;
						}
					}
				}
			}
		}
		return bits;
	};

	std::atomic<std::size_t> evaluations{0};
	// Whose material lies nearest a point (d: the field there): the point itself, if in
	// material; else just inside the nearest surface, down the field's gradient. There, the
	// solid leaf's part, or the nearest inside sample's in the brick. 0 if none is found.
	auto side_at = [&](vec3 p, float d) -> unsigned {
		if (d > 0.0f) {
			const float h = 0.01f;
			const vec3 k0(1, -1, -1), k1(-1, -1, 1), k2(-1, 1, -1), k3(1, 1, 1);
			vec3 g = k0 * octree.distance(body, p + k0 * h) + k1 * octree.distance(body, p + k1 * h) +
					k2 * octree.distance(body, p + k2 * h) + k3 * octree.distance(body, p + k3 * h);
			evaluations += 4;
			if (gl::length(g) < 1e-12f) {
				return 0;
			}
			p = p - gl::normalize(g) * (d + 0.02f);
		}
		// The nearest inside sample (or solid leaf) within a radius, growing.
		for (const float r : {0.25f, 0.5f, 1.0f, 2.0f}) {
			const vec3 lo = p - vec3(r), hi = p + vec3(r);
			float best = std::numeric_limits<float>::max();
			unsigned side = 0;
			std::vector<int> stack{0};
			while (!stack.empty()) {
				const int i = stack.back();
				stack.pop_back();
				const Adf::Node &n = adf_nodes[std::size_t(i)];
				const vec3 nhi = n.lo + vec3(n.size);
				if (nhi.x < lo.x || nhi.y < lo.y || nhi.z < lo.z || n.lo.x > hi.x || n.lo.y > hi.y || n.lo.z > hi.z) {
					continue;
				}
				if (n.child >= 0) {
					for (int c = 0; c < 8; ++c) {
						stack.push_back(n.child + c);
					}
					continue;
				}
				if (n.brick == Adf::kSolid) {
					const vec3 gap = gl::max(gl::max(n.lo - p, p - nhi), 0.0f);
					const float dist = gl::length(gap);
					if (dist < best) {
						best = dist;
						side = parts.of_node(i) == island ? kIsland : kRest;
					}
					continue;
				}
				if (n.brick < 0) {
					continue;
				}
				const std::uint16_t *brick = values.data() + std::size_t(n.brick) * kS;
				const float voxel = n.size / float(kCells);
				for (int j = 0; j < kS; ++j) {
					if (half_to_float(brick[j]) >= 0.0f) {
						continue;
					}
					const vec3 q = n.lo + vec3(float(j % kN), float((j / kN) % kN), float(j / (kN * kN))) * voxel;
					const float dist = gl::length(q - p);
					if (dist < best) {
						best = dist;
						side = parts.of_sample(n.brick, j) == island ? kIsland : kRest;
					}
				}
			}
			if (side && best <= r) {
				return side;
			}
		}
		return 0u;
	};

	// The region's tree, classified a level of new cubes at a time, in parallel.
	const float lipschitz = std::max(body.lipschitz(), 1.0f);
	const float clear = 0.5f * Region::kFloor;
	std::vector<Region::Node> nodes;
	nodes.push_back({root_lo, root_size, -1, Region::Free, 0});
	std::atomic<const char *> failed{nullptr};
	auto classify = [&](int i) {
		Region::Node &n = nodes[std::size_t(i)];
		const vec3 centre = n.lo + vec3(n.size * 0.5f);
		const float reach = lipschitz * n.size * 0.8660254f;
		const float d = octree.distance(body, centre);
		++evaluations;
		if (d - reach >= clear) {
			n.label = Region::Free; // air, and clear of every surface
			return;
		}
		const unsigned bits = material(n.lo - vec3(1e-4f), n.lo + vec3(n.size + 1e-4f));
		if (bits == kIsland || bits == kRest) {
			n.label = bits == kIsland ? Region::Island : Region::Rest;
			return;
		}
		if (bits == 0) {
			// No samples in it, yet near a surface (or in material thinner than the samples):
			// the side whose surface is nearest. Where that is wrong, the cube touches a cube
			// of the other side, and both split.
			const unsigned side = side_at(centre, d);
			if (side == kIsland || side == kRest) {
				n.label = side == kIsland ? Region::Island : Region::Rest;
				return;
			}
		}
		if (n.size * 0.5f >= finest) {
			n.child = -2; // to split
			return;
		}
		failed = bits ? "both sides' material in a cube at the finest" : "material the samples do not show";
	};
	auto split = [&](int i, std::vector<int> &into) {
		const int first = int(nodes.size());
		const Region::Node parent = nodes[std::size_t(i)];
		nodes[std::size_t(i)].child = first;
		for (int c = 0; c < 8; ++c) {
			const vec3 off(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1));
			nodes.push_back({parent.lo + off * (parent.size * 0.5f), parent.size * 0.5f, -1, Region::Free, 0});
			into.push_back(first + c);
		}
	};
	// Classifies `batch`, splitting where both sides' samples share a cube, down to leaves.
	auto settle = [&](std::vector<int> batch) {
		while (!batch.empty() && !failed) {
			parallel_for(batch.size(), [&](std::size_t k) { classify(batch[k]); });
			std::vector<int> next;
			for (const int i : batch) {
				if (nodes[std::size_t(i)].child == -2) {
					split(i, next);
				}
			}
			batch = std::move(next);
		}
		return !failed;
	};
	auto finish = [&]() {
		out.failed = failed;
		out.evaluations = evaluations;
		out.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return out;
	};
	if (!settle({0})) {
		return finish();
	}
	// Then wherever an island cube meets a rest cube, or the root's side (beyond which lies
	// the rest), across a face that is not clear of every surface, split both, until none do.
	// (Across a clear face the field is the same kept or dropped: max(d, c - d) = d.)
	struct Face {
		int a, b; // b: -1 for the root's side
		int node, axis;
		bool upper;
	};
	auto face_clear = [&](const Face &f) {
		const Region::Node &n = nodes[std::size_t(f.node)];
		const float shift = f.upper ? n.size * 0.5f : -n.size * 0.5f;
		const vec3 centre = n.lo + vec3(n.size * 0.5f) +
				vec3(f.axis == 0 ? shift : 0.0f, f.axis == 1 ? shift : 0.0f, f.axis == 2 ? shift : 0.0f);
		++evaluations;
		return octree.distance(body, centre) - lipschitz * n.size * 0.7071068f >= clear;
	};
	const float eps = root_size * 1e-6f;
	for (;;) {
		++out.passes;
		std::vector<Face> faces;
		auto visit = [&](int a, int b, int axis) {
			const Region::Label la = nodes[std::size_t(a)].label, lb = nodes[std::size_t(b)].label;
			if ((la == Region::Island && lb == Region::Rest) || (la == Region::Rest && lb == Region::Island)) {
				// The face they share is the smaller one's (a lies below b along the axis).
				const bool a_small = nodes[std::size_t(a)].size <= nodes[std::size_t(b)].size;
				faces.push_back({a, b, a_small ? a : b, axis, a_small});
			}
		};
		FacePairs<Region::Node, decltype(visit)>{nodes, nullptr, visit}.cell(0);
		for (std::size_t i = 0; i < nodes.size(); ++i) {
			const Region::Node &n = nodes[i];
			if (n.child >= 0 || n.label != Region::Island) {
				continue;
			}
			const float lo[3] = {n.lo.x, n.lo.y, n.lo.z}, rlo[3] = {root.lo.x, root.lo.y, root.lo.z},
						rhi[3] = {root.hi.x, root.hi.y, root.hi.z};
			for (int axis = 0; axis < 3; ++axis) {
				if (lo[axis] <= rlo[axis] + eps) {
					faces.push_back({int(i), -1, int(i), axis, false});
				}
				if (lo[axis] + n.size >= rhi[axis] - eps) {
					faces.push_back({int(i), -1, int(i), axis, true});
				}
			}
		}
		std::vector<char> shut(faces.size(), 0);
		parallel_for(faces.size(), [&](std::size_t k) { shut[k] = !face_clear(faces[k]); });
		std::vector<char> touching(nodes.size(), 0);
		for (std::size_t k = 0; k < faces.size(); ++k) {
			if (shut[k]) {
				touching[std::size_t(faces[k].a)] = 1;
				if (faces[k].b >= 0) {
					touching[std::size_t(faces[k].b)] = 1;
				}
			}
		}
		std::vector<int> batch;
		for (std::size_t i = 0; i < touching.size(); ++i) {
			if (!touching[i]) {
				continue;
			}
			if (nodes[i].size * 0.5f < finest) {
				failed = "the island's cubes and the rest's touch at the finest";
				return finish();
			}
			split(int(i), batch);
		}
		if (batch.empty()) {
			break;
		}
		if (!settle(std::move(batch))) {
			return finish();
		}
	}
	auto region = std::make_shared<const Region>(std::move(nodes));
	out.leaves = region->leaves();
	out.region = std::move(region);
	return finish();
}

} // namespace sdf
