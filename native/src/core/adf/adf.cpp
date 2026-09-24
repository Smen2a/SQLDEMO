#include "adf/adf.h"

#include "adf/sampler.h"
#include "util/half.h"
#include "util/parallel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <thread>

namespace sdf {

namespace {

constexpr float kSqrt3 = 1.7320508f;
constexpr int kN = Adf::kSide;  // samples per side
constexpr int kCells = kN - 1;  // voxels per side
constexpr int kMaterialBytes = 4 * Adf::kBrickSamples;

bool overlaps(const Aabb &a, vec3 lo, float size) {
	return a.lo.x <= lo.x + size && a.hi.x >= lo.x && a.lo.y <= lo.y + size && a.hi.y >= lo.y && a.lo.z <= lo.z + size &&
			a.hi.z >= lo.z;
}

vec3 corner(int c) {
	return vec3(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1));
}

int index(int x, int y, int z) {
	return x + kN * (y + kN * z);
}

// The material a sample shows: the one that dominates its mix.
std::int32_t dominant(const Sample &s) {
	return std::int32_t(s.t < 0.5f ? s.m0 : s.m1);
}

// The previous tree, when updating: cells outside `region` are copied from it, and so are
// cells inside it that no changed edit reaches (see Adf::Change).
struct Old {
	const std::vector<Adf::Node> *nodes = nullptr;
	const std::vector<Adf::ExactCell> *exact = nullptr;
	const std::vector<std::uint32_t> *tape = nullptr;
	Aabb region;
	std::uint32_t first_changed = 0;
	Aabb removed;
	// The old bricks' samples (their slots are only rewritten when the new tree is spliced).
	const std::vector<std::uint16_t> *values = nullptr;
	// Whether the change only appends cuts (see Refiner::fold).
	bool fold = false;
	// A refinement pass (Adf::refine): nothing changed, but coarse exact leaves in the
	// region are refined down to the finest exact cells.
	bool refining = false;
	// An update with coarse exact cells: split nodes the change reaches are redone from their
	// cube (their fine leaves are refine()'s to rebuild), so that cutting across finely
	// refined creases costs no more than cutting a face.
	bool coarse = false;
};

// Node of `nodes` covering exactly the cube (lo, size), or -1.
int find_cube(const std::vector<Adf::Node> &nodes, vec3 lo, float size) {
	if (nodes.empty()) {
		return -1;
	}
	const vec3 centre = lo + vec3(size * 0.5f);
	int node = 0;
	while (nodes[std::size_t(node)].size > size * 1.5f) {
		const Adf::Node &n = nodes[std::size_t(node)];
		if (n.child < 0) {
			return -1;
		}
		const float half = n.size * 0.5f;
		node = n.child + int(centre.x >= n.lo.x + half) + 2 * int(centre.y >= n.lo.y + half) +
				4 * int(centre.z >= n.lo.z + half);
	}
	const Adf::Node &n = nodes[std::size_t(node)];
	const float tol = size * 1e-4f;
	if (std::fabs(n.size - size) > tol || std::fabs(n.lo.x - lo.x) > tol || std::fabs(n.lo.y - lo.y) > tol ||
			std::fabs(n.lo.z - lo.z) > tol) {
		return -1;
	}
	return node;
}


// A subtree built by one worker, spliced into the tree afterwards. Local node 0 is the
// task's cell. New bricks, material bricks and exact tapes are numbered locally; bricks
// copied from the old tree keep their slots (flagged in kept_brick / kept_material), and
// bricks folded from an old one are written back into its slot (reuse_brick).
struct Local {
	std::vector<Adf::Node> nodes;
	std::vector<char> kept_brick, kept_material;
	std::vector<std::int32_t> reuse_brick;
	std::vector<std::uint16_t> values;
	std::vector<std::uint8_t> materials;
	std::vector<Adf::ExactCell> exact;
	std::vector<std::uint32_t> tape;
	std::size_t folded = 0, unchanged = 0;

	void start(vec3 lo, float size) {
		nodes.resize(1);
		kept_brick.assign(1, 0);
		kept_material.assign(1, 0);
		reuse_brick.assign(1, -1);
		nodes[0].lo = lo;
		nodes[0].size = size;
	}

	int add_children(int parent, vec3 lo, float size) {
		const int first = int(nodes.size());
		nodes.resize(std::size_t(first) + 8);
		kept_brick.resize(nodes.size(), 0);
		kept_material.resize(nodes.size(), 0);
		reuse_brick.resize(nodes.size(), -1);
		nodes[std::size_t(parent)].child = first;
		for (int c = 0; c < 8; ++c) {
			nodes[std::size_t(first + c)].lo = lo + corner(c) * (size * 0.5f);
			nodes[std::size_t(first + c)].size = size * 0.5f;
		}
		return first;
	}
};

// A cube waiting to be refined: local node `node` of a task, covering (lo, size). `parent`
// is a tape valid over a cell containing it; `old_node` the old tree's node for the same
// cube, or -1.
struct Cube {
	int node;
	vec3 lo;
	float size;
	const Octree::Leaf *parent;
	int old_node;
};

// One task's refinement in progress, a level at a time: its Local, the cubes of the level
// to prepare, and those prepared that need their bricks sampled.
struct Work {
	Local local;
	std::vector<Cube> frontier;
	std::vector<Cube> sampling;
	std::vector<const Octree::Leaf *> sampling_cells; // their pruned tapes
	std::deque<Octree::Leaf> cells;                   // pruned tapes (children prune from them)
	std::size_t first_job = 0;                        // sampling[0]'s index in the level's jobs
};

// Refines tasks a level at a time, so that a sampler can take a whole level's bricks in one
// batch: prepare() settles every cube it can without samples and lists the rest, the sampler
// samples them, and decide() makes each a leaf or splits it into the next level. A task's
// Local is only ever touched by one thread at a time.
struct Refiner {
	const Body &body;
	const Octree &octree;
	const AdfParams &params;
	const Old &old;
	float exact_cell; // this pass's: creases stop refining at cells this large

	Sample at(const Octree::Leaf &cell, vec3 p) const {
		return Octree::eval_tape(body, cell.base, cell.tape.data(), cell.tape.size(), p);
	}

	bool holds_layer(const Octree::Leaf &cell) const {
		for (std::uint32_t entry : cell.tape) {
			if (body.edits()[entry & ~Octree::kResetBit].op == Op::Layer) {
				return true;
			}
		}
		return false;
	}

	void copy(Local &local, int from, int to) const {
		const Adf::Node n = (*old.nodes)[std::size_t(from)];
		local.nodes[std::size_t(to)] = n;
		if (n.exact()) {
			const Adf::ExactCell &cell = (*old.exact)[std::size_t(n.material - Adf::kExactBase)];
			local.nodes[std::size_t(to)].material = Adf::kExactBase + int(local.exact.size());
			local.exact.push_back({std::uint32_t(local.tape.size()), cell.count, cell.base, cell.error});
			local.tape.insert(local.tape.end(), old.tape->begin() + cell.offset, old.tape->begin() + cell.offset + cell.count);
		}
		local.kept_brick[std::size_t(to)] = n.brick >= 0;
		local.kept_material[std::size_t(to)] = n.brick >= 0 && n.material < 0;
		if (n.child >= 0) {
			const int first = local.add_children(to, n.lo, n.size);
			for (int c = 0; c < 8; ++c) {
				copy(local, n.child + c, first + c);
			}
		}
	}

	void leaf_without_brick(Local &local, int node, Octree::State state, const Sample &centre) const {
		Adf::Node &n = local.nodes[std::size_t(node)];
		n.brick = state == Octree::State::Empty ? Adf::kEmpty : Adf::kSolid;
		n.material = dominant(centre);
		n.value = centre.d;
	}

	// Settles the frontier's cubes that need no brick samples (and, where an old split is
	// kept, their children), leaving the rest in w.sampling.
	void prepare(Work &w) const {
		Local &local = w.local;
		for (std::size_t k = 0; k < w.frontier.size(); ++k) {
			const Cube cube = w.frontier[k];
			if (cube.old_node >= 0 && !overlaps(old.region, cube.lo, cube.size)) {
				copy(local, cube.old_node, cube.node);
				continue;
			}
			if (old.refining && cube.old_node >= 0) {
				// Only coarse exact leaves are redone; their tapes are valid over the cubes
				// below, pruned or not, so split nodes are passed without pruning.
				const Adf::Node &o = (*old.nodes)[std::size_t(cube.old_node)];
				if (o.child >= 0) {
					const int first = local.add_children(cube.node, cube.lo, cube.size);
					for (int c = 0; c < 8; ++c) {
						w.frontier.push_back({first + c, local.nodes[std::size_t(first + c)].lo, cube.size * 0.5f, cube.parent,
								o.child + c});
					}
					continue;
				}
				if (!(o.exact() && o.size > exact_cell * 1.001f)) {
					copy(local, cube.old_node, cube.node);
					continue;
				}
			}
			// Only the edits that can shape this cube: a few, even where the octree cell's tape
			// is long, and none at all where the cube is provably empty or solid.
			const Octree::Leaf &cell = w.cells.emplace_back(octree.prune_within(body, *cube.parent, cube.lo, cube.size));
			if (cube.old_node >= 0 && !old.refining && !overlaps(old.removed, cube.lo, cube.size) &&
					std::none_of(cell.tape.begin(), cell.tape.end(),
							[&](std::uint32_t entry) { return (entry & ~Octree::kResetBit) >= old.first_changed; })) {
				// Pruning dropped every changed edit here: each provably changes nothing in this
				// cube (a long stroke's bounding box holds far more cells than its groove reaches).
				w.cells.pop_back();
				copy(local, cube.old_node, cube.node);
				continue;
			}
			// A coarse update redoes a split cube whole where it can become an exact cell (its
			// splits were for creases, which refine() will redo); cubes whose tapes are too long
			// for that, or hold a layer, would only split the same way again.
			const bool collapse = old.coarse && cube.size <= exact_cell * 1.001f &&
					int(cell.tape.size()) <= params.exact_tape_limit && !holds_layer(cell);
			if (cube.old_node >= 0 && (*old.nodes)[std::size_t(cube.old_node)].child >= 0 && !collapse) {
				// Split before: stay split (at worst finer than now needed) and redo only the
				// children the change reaches.
				const int first = local.add_children(cube.node, cube.lo, cube.size);
				const int old_first = (*old.nodes)[std::size_t(cube.old_node)].child;
				for (int c = 0; c < 8; ++c) {
					w.frontier.push_back(
							{first + c, local.nodes[std::size_t(first + c)].lo, cube.size * 0.5f, &cell, old_first + c});
				}
				continue;
			}
			if (cell.state != Octree::State::Surface) {
				leaf_without_brick(local, cube.node, cell.state, at(cell, cube.lo + vec3(cube.size * 0.5f)));
				w.cells.pop_back();
				continue;
			}
			if (old.fold && cube.old_node >= 0 && (*old.nodes)[std::size_t(cube.old_node)].child < 0) {
				const Adf::Node &o = (*old.nodes)[std::size_t(cube.old_node)];
				if (o.brick == Adf::kEmpty) {
					// Cuts only remove material: an empty cube stays empty (its field only grows).
					leaf_without_brick(local, cube.node, Octree::State::Empty, at(cell, cube.lo + vec3(cube.size * 0.5f)));
					w.cells.pop_back();
					continue;
				}
				if (o.brick >= 0 && !o.exact()) {
					fold(w, cube, cell);
					continue;
				}
			}
			w.sampling.push_back(cube);
			w.sampling_cells.push_back(&cell);
		}
		w.frontier.clear();
	}

	// A cut folded into an old brick: the new edits (the tape's last entries) applied to its
	// samples, one primitive each, instead of the whole tape. Appending a cut is exact on
	// the field, so these are the samples re-sampling would give (bit for bit for hard cuts,
	// as half rounding is monotone; within the old samples' rounding otherwise). Then the
	// usual decision, whose centre checks (through the cube's tape) catch what the cut does
	// between the samples: a new crease, or a cut too thin or curved for the brick, splits
	// the cube, and its children are sampled in full.
	void fold(Work &w, const Cube &cube, const Octree::Leaf &cell) const {
		const Adf::Node &o = (*old.nodes)[std::size_t(cube.old_node)];
		std::size_t first = cell.tape.size();
		while (first > 0 && (cell.tape[first - 1] & ~Octree::kResetBit) >= old.first_changed) {
			--first;
		}
		const std::uint16_t *before = old.values->data() + std::size_t(o.brick) * Adf::kBrickSamples;
		const float voxel = cube.size / float(kCells);
		Sample s[Adf::kBrickSamples];
		bool unchanged = true;
		for (int z = 0; z < kN; ++z) {
			for (int y = 0; y < kN; ++y) {
				for (int x = 0; x < kN; ++x) {
					const int i = index(x, y, z);
					s[i].d = Octree::continue_tape(body, half_to_float(before[i]), cell.tape.data() + first,
							cell.tape.size() - first, cube.lo + vec3(float(x), float(y), float(z)) * voxel);
					unchanged = unchanged && float_to_half(s[i].d) == before[i];
				}
			}
		}
		decide(w, cube, cell, s, nullptr, cube.old_node, unchanged);
	}

	// Makes each sampled cube a leaf (with or without a brick, exact or not) or splits it,
	// its children going to the next level's frontier. `corners` and `centres` are the
	// level's samples (centres only if the sampler batches them).
	void decide(Work &w, const Sample *corners, const float *centres) const {
		for (std::size_t k = 0; k < w.sampling.size(); ++k) {
			const std::size_t job = w.first_job + k;
			decide(w, w.sampling[k], *w.sampling_cells[k], corners + job * AdfSampler::kCorners,
					centres ? centres + job * AdfSampler::kCentres : nullptr);
		}
		w.sampling.clear();
		w.sampling_cells.clear();
	}

	// `folded` is the old brick leaf the samples were folded from (see fold()), or -1;
	// `unchanged` whether folding changed none of them.
	void decide(Work &w, const Cube &cube, const Octree::Leaf &cell, const Sample *s, const float *centres,
			int folded = -1, bool unchanged = false) const {
		Local &local = w.local;
		const int node = cube.node;
		const vec3 lo = cube.lo;
		const float size = cube.size;
		const float voxel = size / float(kCells);
		std::uint16_t half[Adf::kBrickSamples];
		float v[Adf::kBrickSamples];
		bool neg = false, pos = false;
		float min_abs = std::numeric_limits<float>::max();
		for (int i = 0; i < Adf::kBrickSamples; ++i) {
			half[i] = float_to_half(s[i].d);
			v[i] = half_to_float(half[i]);
			neg = neg || v[i] < 0.0f;
			pos = pos || v[i] >= 0.0f;
			min_abs = std::min(min_abs, std::fabs(s[i].d));
		}
		if (!(neg && pos) && min_abs > cell.lipschitz * voxel * kSqrt3) {
			// The surface does not pass through this cell.
			leaf_without_brick(local, node, pos ? Octree::State::Empty : Octree::State::Solid,
					at(cell, lo + vec3(size * 0.5f)));
			return;
		}

		// Voxels within a voxel of the surface decide what is drawn: check the trilinear
		// reconstruction against the exact field at their centres. A cell that may become
		// exact checks all of them, as its worst difference is its error band; others stop
		// at the first failure.
		const bool can_split = voxel * 0.5f >= params.min_voxel;
		// The Live shader cannot evaluate smoothing layers (they are sampled grids, which only
		// the CPU holds): a cell where one weighs in refines its bricks instead.
		const bool may_be_exact = (size <= exact_cell || !can_split) &&
				int(cell.tape.size()) <= params.exact_tape_limit && !holds_layer(cell);
		const float stop = may_be_exact ? std::numeric_limits<float>::max() : params.tolerance;
		float worst = 0.0f;
		for (int z = 0; z < kCells && worst <= stop; ++z) {
			for (int y = 0; y < kCells && worst <= stop; ++y) {
				for (int x = 0; x < kCells && worst <= stop; ++x) {
					float lo_v = std::numeric_limits<float>::max(), hi_v = -lo_v, sum = 0.0f, near = lo_v;
					for (int c = 0; c < 8; ++c) {
						const float cv = v[index(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1))];
						lo_v = std::min(lo_v, cv);
						hi_v = std::max(hi_v, cv);
						near = std::min(near, std::fabs(cv));
						sum += cv;
					}
					if (!(lo_v < 0.0f && hi_v >= 0.0f) && near > voxel * cell.lipschitz) {
						continue;
					}
					const float exact = centres ? centres[x + kCells * (y + kCells * z)]
												: at(cell, lo + (vec3(float(x), float(y), float(z)) + 0.5f) * voxel).d;
					worst = std::max(worst, std::fabs(sum * 0.125f - exact));
				}
			}
		}
		const bool exact = worst > params.tolerance && may_be_exact;
		if (worst > params.tolerance && can_split && !exact) {
			const int first = local.add_children(node, lo, size);
			for (int c = 0; c < 8; ++c) {
				w.frontier.push_back({first + c, local.nodes[std::size_t(first + c)].lo, size * 0.5f, &cell, -1});
			}
			return;
		}

		// A brick leaf.
		if (folded >= 0 && unchanged && !exact) {
			// The cut passed the samples by: the old brick stays, in its slot (no upload).
			copy(local, folded, node);
			++local.unchanged;
			return;
		}
		Adf::Node &n = local.nodes[std::size_t(node)];
		n.brick = int(local.values.size() / Adf::kBrickSamples);
		local.values.insert(local.values.end(), half, half + Adf::kBrickSamples);
		if (folded >= 0) {
			local.reuse_brick[std::size_t(node)] = (*old.nodes)[std::size_t(folded)].brick;
			++local.folded;
		}
		// The trilinear field's gradient in a voxel is bounded, per axis, by the largest of
		// the voxel's four edge differences along that axis.
		float lip = 0.0f;
		for (int z = 0; z < kCells; ++z) {
			for (int y = 0; y < kCells; ++y) {
				for (int x = 0; x < kCells; ++x) {
					float g[3] = {0.0f, 0.0f, 0.0f};
					for (int a = 0; a < 2; ++a) {
						for (int b = 0; b < 2; ++b) {
							g[0] = std::max(g[0], std::fabs(v[index(x + 1, y + a, z + b)] - v[index(x, y + a, z + b)]));
							g[1] = std::max(g[1], std::fabs(v[index(x + a, y + 1, z + b)] - v[index(x + a, y, z + b)]));
							g[2] = std::max(g[2], std::fabs(v[index(x + a, y + b, z + 1)] - v[index(x + a, y + b, z)]));
						}
					}
					lip = std::max(lip, std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) / voxel);
				}
			}
		}
		n.value = std::max(lip, 1e-3f);
		if (exact) {
			// The tape decides hits, normals and materials here; the brick only speeds rays up,
			// to within its error band (see ExactCell::error): the worst difference measured
			// above, where rays stop, near the surface. Hardware filtering weights carry 8
			// fractional bits: up to L * voxel / 256 more.
			n.material = Adf::kExactBase + int(local.exact.size());
			n.value = std::max(n.value, cell.lipschitz);
			const float error = std::min(2.0f * worst + n.value * voxel / 128.0f + 1e-4f, n.value * voxel * kSqrt3);
			local.exact.push_back({std::uint32_t(local.tape.size()), std::uint32_t(cell.tape.size()), cell.base, error});
			local.tape.insert(local.tape.end(), cell.tape.begin(), cell.tape.end());
			return;
		}
		if (folded >= 0) {
			// Cuts keep the materials: the old id, or the old material brick in its slot.
			const Adf::Node &o = (*old.nodes)[std::size_t(folded)];
			n.material = o.material;
			local.kept_material[std::size_t(node)] = o.material < 0;
			return;
		}

		// Material: one id for the whole brick unless it varies.
		auto effective = [](const Sample &smp) {
			if (smp.t <= 0.5f / 255.0f) {
				return int(smp.m0);
			}
			if (smp.t >= 1.0f - 0.5f / 255.0f) {
				return int(smp.m1);
			}
			return -1;
		};
		const int first_material = effective(s[0]);
		bool uniform = first_material >= 0;
		for (int i = 1; i < Adf::kBrickSamples && uniform; ++i) {
			uniform = effective(s[i]) == first_material;
		}
		if (uniform) {
			n.material = first_material;
			return;
		}
		n.material = -(int(local.materials.size() / kMaterialBytes) + 1);
		for (int i = 0; i < Adf::kBrickSamples; ++i) {
			local.materials.push_back(std::uint8_t(s[i].m0));
			local.materials.push_back(std::uint8_t(s[i].m1));
			local.materials.push_back(std::uint8_t(std::lround(std::clamp(s[i].t, 0.0f, 1.0f) * 255.0f)));
			local.materials.push_back(255);
		}
	}
};

} // namespace

void Adf::build(const Body &body, const Octree &octree, const AdfParams &params) {
	params_ = params;
	coarse_ = Aabb();
	rebuild(body, octree, Change{Aabb::infinite(), 0, Aabb::infinite()}, false, Pass::Build);
}

void Adf::update(const Body &body, const Octree &octree, const Change &change) {
	const Octree::Node &root = octree.nodes()[0];
	const bool same_root = !nodes_.empty() && nodes_[0].size == root.size && nodes_[0].lo.x == root.lo.x &&
			nodes_[0].lo.y == root.lo.y && nodes_[0].lo.z == root.lo.z;
	const Change applied = same_root ? change : Change{Aabb::infinite(), 0, Aabb::infinite()};
	rebuild(body, octree, applied, same_root, Pass::Update);
	if (params_.update_exact_cell > params_.exact_cell) {
		coarse_.include(applied.region);
	}
}

void Adf::refine(const Body &body, const Octree &octree) {
	if (coarse_.empty() || nodes_.empty()) {
		coarse_ = Aabb();
		return;
	}
	const Aabb region = coarse_;
	coarse_ = Aabb();
	rebuild(body, octree, Change{region, std::uint32_t(body.edits().size()), Aabb()}, true, Pass::Refine);
}

void Adf::update(const Body &body, const Octree &octree, const Aabb &region) {
	const std::size_t count = body.edits().size();
	// Edits removed through this form: anything in the region may have changed.
	update(body, octree, Change{region, std::uint32_t(std::min(count, edits_)), count < edits_ ? region : Aabb{}});
}

Aabb Adf::dirty_region(const Body &body, const Octree &, std::size_t index) {
	const Edit &e = body.edits()[index];
	if (e.op == Op::Intersect) {
		return Aabb::infinite();
	}
	// Near the surface, the field changes only within the edit's reach. (The octree re-prunes
	// a value margin further, which can change far-off values there; bricks keeping stale
	// far values stay safe, as steps stop at cell exits.)
	return body.edit_box(index).expanded(body.edit_influence(index));
}

void Adf::rebuild(const Body &body, const Octree &octree, const Change &change, bool reuse, Pass pass) {
	const Aabb &region = change.region;
	const auto start = std::chrono::steady_clock::now();
	// Cells outside the region keep their bricks. Their samples far from the surface may
	// now overestimate the distance to a new surface outside the cell, but steps stop at the
	// cell's exit, so no ray can skip that surface.
	std::vector<Node> old_nodes;
	std::vector<ExactCell> old_exact = std::move(exact_cells_);
	std::vector<std::uint32_t> old_tape = std::move(exact_tape_);
	std::vector<char> keep_brick, keep_material;
	if (reuse) {
		old_nodes = std::move(nodes_);
		keep_brick.assign(brick_slots(), 0);
		keep_material.assign(material_slots(), 0);
	} else {
		values_.clear();
		materials_.clear();
	}
	nodes_.clear();
	nodes_.reserve(old_nodes.size() + 64);
	exact_cells_.clear();
	exact_tape_.clear();
	// Folding needs every new edit to be a cut: it can only remove material (so empty cells
	// stay empty), keeps the materials, and is a function of the field before it.
	bool fold = reuse && change.removed.empty() && change.first_changed < body.edits().size();
	for (std::size_t i = change.first_changed; fold && i < body.edits().size(); ++i) {
		const Edit &e = body.edits()[i];
		fold = (e.op == Op::Subtract || e.op == Op::Intersect) && e.blend != Blend::Profile;
	}
	const Old old{&old_nodes, &old_exact, &old_tape, region, change.first_changed, change.removed, &values_,
			fold && pass == Pass::Update, pass == Pass::Refine,
			pass == Pass::Update && params_.update_exact_cell > params_.exact_cell};
	edits_ = body.edits().size();
	auto add_exact = [&](const ExactCell &cell, const std::uint32_t *entries) {
		exact_cells_.push_back({std::uint32_t(exact_tape_.size()), cell.count, cell.base, cell.error});
		exact_tape_.insert(exact_tape_.end(), entries, entries + cell.count);
		return kExactBase + int(exact_cells_.size()) - 1;
	};

	struct Task {
		int node;
		vec3 lo;
		float size;
		int tape_node; // octree leaf whose cell contains the task's
		int old_node;
	};
	std::vector<Task> tasks;

	// Above brick-sized cells (the skeleton), copy what the change cannot reach, mirror the
	// octree, and split surface cells down to tasks.
	std::function<void(int, int)> copy_subtree = [&](int from, int to) {
		const Node n = old_nodes[std::size_t(from)];
		nodes_[std::size_t(to)] = n;
		if (n.exact()) {
			const ExactCell &cell = old_exact[std::size_t(n.material - kExactBase)];
			nodes_[std::size_t(to)].material = add_exact(cell, old_tape.data() + cell.offset);
		}
		if (n.brick >= 0) {
			keep_brick[std::size_t(n.brick)] = 1;
			if (n.material < 0) {
				keep_material[std::size_t(-n.material - 1)] = 1;
			}
		}
		if (n.child >= 0) {
			const int first = int(nodes_.size());
			nodes_.resize(std::size_t(first) + 8);
			nodes_[std::size_t(to)].child = first;
			for (int c = 0; c < 8; ++c) {
				copy_subtree(n.child + c, first + c);
			}
		}
	};
	auto try_reuse = [&](int node, vec3 lo, float size) {
		if (!reuse || overlaps(region, lo, size)) {
			return false;
		}
		const int from = find_cube(old_nodes, lo, size);
		if (from < 0) {
			return false;
		}
		copy_subtree(from, node);
		return true;
	};
	auto leaf_without_brick = [&](int node, vec3 lo, float size, const Sample &centre) {
		Node &n = nodes_[std::size_t(node)];
		n.lo = lo;
		n.size = size;
		n.child = -1;
		n.brick = centre.d >= 0.0f ? kEmpty : kSolid;
		n.material = dominant(centre);
		n.value = centre.d;
	};
	std::function<void(int, vec3, float, float, int)> subdivide = [&](int node, vec3 lo, float size, float lip,
																		int tape_node) {
		nodes_[std::size_t(node)].lo = lo;
		nodes_[std::size_t(node)].size = size;
		if (try_reuse(node, lo, size)) {
			return;
		}
		const Sample centre = octree.sample(body, lo + vec3(size * 0.5f));
		if (std::fabs(centre.d) > lip * size * kSqrt3 * 0.5f) {
			leaf_without_brick(node, lo, size, centre);
			return;
		}
		if (size / float(kCells) > params_.max_voxel) {
			const int first = int(nodes_.size());
			nodes_.resize(std::size_t(first) + 8);
			nodes_[std::size_t(node)].child = first;
			for (int c = 0; c < 8; ++c) {
				subdivide(first + c, lo + corner(c) * (size * 0.5f), size * 0.5f, lip, tape_node);
			}
			return;
		}
		tasks.push_back({node, lo, size, tape_node, reuse ? find_cube(old_nodes, lo, size) : -1});
	};
	std::function<void(int, int)> mirror = [&](int tape_node, int node) {
		const Octree::Node &t = octree.nodes()[std::size_t(tape_node)];
		nodes_[std::size_t(node)].lo = t.lo;
		nodes_[std::size_t(node)].size = t.size;
		if (t.child >= 0) {
			if (try_reuse(node, t.lo, t.size)) {
				return;
			}
			const int first = int(nodes_.size());
			nodes_.resize(std::size_t(first) + 8);
			nodes_[std::size_t(node)].child = first;
			for (int c = 0; c < 8; ++c) {
				mirror(t.child + c, first + c);
			}
			return;
		}
		const Octree::Leaf &leaf = octree.leaves()[std::size_t(t.leaf)];
		if (leaf.state != Octree::State::Surface) {
			leaf_without_brick(node, t.lo, t.size, octree.sample(body, t.lo + vec3(t.size * 0.5f)));
			return;
		}
		subdivide(node, t.lo, t.size, leaf.lipschitz, tape_node);
	};
	nodes_.resize(1);
	mirror(0, 0);

	// Refine the brick-sized cells, a level at a time across a group of tasks, so the sampler
	// takes each level's bricks in one batch. (Groups bound the samples held at once.)
	std::vector<Local> locals(tasks.size());
	const int threads = params_.threads > 0 ? params_.threads : int(std::max(1u, std::thread::hardware_concurrency()));
	CpuSampler cpu(threads);
	AdfSampler &sampler = sampler_ ? *sampler_ : cpu;
	const bool batched = sampler.batches_centres();
	const Refiner refiner{body, octree, params_, old,
			pass == Pass::Update ? std::max(params_.update_exact_cell, params_.exact_cell) : params_.exact_cell};
	constexpr std::size_t kGroup = 2048;
	std::vector<AdfJob> jobs;
	std::vector<Sample> corners;
	std::vector<float> centres;
	for (std::size_t group = 0; group < tasks.size(); group += kGroup) {
		const std::size_t count = std::min(kGroup, tasks.size() - group);
		std::vector<Work> works(count);
		for (std::size_t i = 0; i < count; ++i) {
			const Task &task = tasks[group + i];
			works[i].local.start(task.lo, task.size);
			const Octree::Leaf &leaf = octree.leaves()[std::size_t(octree.nodes()[std::size_t(task.tape_node)].leaf)];
			works[i].frontier.push_back({0, task.lo, task.size, &leaf, task.old_node});
		}
		for (;;) {
			parallel_for(count, [&](std::size_t i) { refiner.prepare(works[i]); }, threads);
			jobs.clear();
			for (Work &w : works) {
				w.first_job = jobs.size();
				for (std::size_t k = 0; k < w.sampling.size(); ++k) {
					jobs.push_back({w.sampling[k].lo, w.sampling[k].size / float(kCells), w.sampling_cells[k]});
				}
			}
			if (jobs.empty()) {
				break;
			}
			corners.resize(jobs.size() * AdfSampler::kCorners);
			centres.resize(batched ? jobs.size() * AdfSampler::kCentres : 0);
			sampler.sample(body, jobs, corners.data(), batched ? centres.data() : nullptr);
			parallel_for(count, [&](std::size_t i) { refiner.decide(works[i], corners.data(), batched ? centres.data() : nullptr); },
					threads);
		}
		for (std::size_t i = 0; i < count; ++i) {
			locals[group + i] = std::move(works[i].local);
		}
	}

	// Splice the subtrees in task order, filling freed slots first, so the result does not
	// depend on scheduling.
	folded_ = unchanged_ = 0;
	for (const Local &local : locals) {
		folded_ += local.folded;
		unchanged_ += local.unchanged;
		for (std::size_t k = 0; k < local.nodes.size(); ++k) {
			if (local.kept_brick[k]) {
				keep_brick[std::size_t(local.nodes[k].brick)] = 1;
			}
			if (local.reuse_brick[k] >= 0) {
				keep_brick[std::size_t(local.reuse_brick[k])] = 1;
			}
			if (local.kept_material[k]) {
				keep_material[std::size_t(-local.nodes[k].material - 1)] = 1;
			}
		}
	}
	std::vector<std::uint32_t> free_bricks, free_materials;
	for (std::size_t i = 0; i < keep_brick.size(); ++i) {
		if (!keep_brick[i]) {
			free_bricks.push_back(std::uint32_t(i));
		}
	}
	for (std::size_t i = 0; i < keep_material.size(); ++i) {
		if (!keep_material[i]) {
			free_materials.push_back(std::uint32_t(i));
		}
	}
	std::size_t next_free_brick = 0, next_free_material = 0;
	dirty_bricks_.clear();
	dirty_materials_.clear();
	auto take_brick = [&]() {
		if (next_free_brick < free_bricks.size()) {
			return free_bricks[next_free_brick++];
		}
		const std::uint32_t slot = std::uint32_t(brick_slots());
		values_.resize(values_.size() + kBrickSamples);
		return slot;
	};
	auto take_material = [&]() {
		if (next_free_material < free_materials.size()) {
			return free_materials[next_free_material++];
		}
		const std::uint32_t slot = std::uint32_t(material_slots());
		materials_.resize(materials_.size() + kMaterialBytes);
		return slot;
	};
	for (std::size_t i = 0; i < tasks.size(); ++i) {
		const Local &local = locals[i];
		const int base = int(nodes_.size()) - 1; // local node k >= 1 goes to base + k
		nodes_.resize(nodes_.size() + local.nodes.size() - 1);
		for (std::size_t k = 0; k < local.nodes.size(); ++k) {
			Node n = local.nodes[k];
			if (n.child >= 0) {
				n.child += base;
			}
			if (n.exact()) {
				const ExactCell &cell = local.exact[std::size_t(n.material - kExactBase)];
				n.material = add_exact(cell, local.tape.data() + cell.offset);
			}
			if (n.brick >= 0 && !local.kept_brick[k]) {
				const std::uint32_t slot = local.reuse_brick[k] >= 0 ? std::uint32_t(local.reuse_brick[k]) : take_brick();
				std::copy_n(&local.values[std::size_t(n.brick) * kBrickSamples], kBrickSamples,
						&values_[std::size_t(slot) * kBrickSamples]);
				dirty_bricks_.push_back(slot);
				n.brick = int(slot);
			}
			if (n.brick >= 0 && n.material < 0 && !local.kept_material[k]) {
				const std::uint32_t slot = take_material();
				std::copy_n(&local.materials[std::size_t(-n.material - 1) * kMaterialBytes], kMaterialBytes,
						&materials_[std::size_t(slot) * kMaterialBytes]);
				dirty_materials_.push_back(slot);
				n.material = -int(slot) - 1;
			}
			nodes_[k == 0 ? std::size_t(tasks[i].node) : std::size_t(base) + k] = n;
		}
	}

	live_bricks_ = 0;
	live_materials_ = 0;
	for (const Node &n : nodes_) {
		live_bricks_ += n.brick >= 0;
		live_materials_ += n.brick >= 0 && n.material < 0;
	}
	rebuilt_ = dirty_bricks_.size();
	build_grid();
	seconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void Adf::build_grid() {
	grid_.assign(std::size_t(kGridSide) * kGridSide * kGridSide, GridCell{});
	// Fills the blocks [x, x + span)^3 (grid coordinates) under `node`, at `depth`.
	std::function<void(int, int, int, int, int, int)> fill = [&](int node, int depth, int x, int y, int z, int span) {
		const Node &n = nodes_[std::size_t(node)];
		if (n.child < 0 || depth == kGridLevels) {
			for (int k = z; k < z + span; ++k) {
				for (int j = y; j < y + span; ++j) {
					for (int i = x; i < x + span; ++i) {
						grid_[std::size_t(i + kGridSide * (j + kGridSide * k))] = {node, depth};
					}
				}
			}
			return;
		}
		const int half = span / 2;
		for (int c = 0; c < 8; ++c) {
			fill(n.child + c, depth + 1, x + (c & 1) * half, y + ((c >> 1) & 1) * half, z + ((c >> 2) & 1) * half, half);
		}
	};
	fill(0, 0, 0, 0, 0, kGridSide);
}

int Adf::leaf(vec3 p) const {
	if (nodes_.empty()) {
		return -1;
	}
	const Node &root = nodes_[0];
	const vec3 rel = p - root.lo;
	if (rel.x < 0 || rel.y < 0 || rel.z < 0 || rel.x > root.size || rel.y > root.size || rel.z > root.size) {
		return -1;
	}
	// The grid cell's node, then down (as sdf_adf_find_leaf in the Live shader).
	const vec3 g = rel * grid_scale();
	const int bx = std::clamp(int(std::floor(g.x)), 0, kGridSide - 1), by = std::clamp(int(std::floor(g.y)), 0, kGridSide - 1),
			  bz = std::clamp(int(std::floor(g.z)), 0, kGridSide - 1);
	int node = grid_[std::size_t(bx + kGridSide * (by + kGridSide * bz))].node;
	while (nodes_[std::size_t(node)].child >= 0) {
		const Node &n = nodes_[std::size_t(node)];
		const float half = n.size * 0.5f;
		node = n.child + int(p.x >= n.lo.x + half) + 2 * int(p.y >= n.lo.y + half) + 4 * int(p.z >= n.lo.z + half);
	}
	return node;
}

namespace {

// Trilinear lookup in a brick at voxel coordinates u in [0, 7]^3, the way the shader does
// it: bilinear within the two neighbouring z slices, then a lerp between them.
float brick_value(const std::uint16_t *b, vec3 u) {
	auto split = [](float c, int &i, float &f) {
		c = std::clamp(c, 0.0f, float(kCells));
		i = std::min(int(std::floor(c)), kCells - 1);
		f = c - float(i);
	};
	int x, y, z;
	float fx, fy, fz;
	split(u.x, x, fx);
	split(u.y, y, fy);
	split(u.z, z, fz);
	auto slice = [&](int zz) {
		const float v00 = half_to_float(b[index(x, y, zz)]), v10 = half_to_float(b[index(x + 1, y, zz)]);
		const float v01 = half_to_float(b[index(x, y + 1, zz)]), v11 = half_to_float(b[index(x + 1, y + 1, zz)]);
		return gl::mix(gl::mix(v00, v10, fx), gl::mix(v01, v11, fx), fy);
	};
	return gl::mix(slice(z), slice(z + 1), fz);
}

} // namespace

float Adf::approx_distance(vec3 p) const {
	const int node = leaf(p);
	if (node < 0) {
		const Node &root = nodes_[0];
		const vec3 outside = gl::max(gl::max(root.lo - p, p - (root.lo + vec3(root.size))), 0.0f);
		return std::max(gl::length(outside), 1e-3f);
	}
	const Node &n = nodes_[std::size_t(node)];
	if (n.brick < 0) {
		return n.value;
	}
	return brick_value(&values_[std::size_t(n.brick) * kBrickSamples], (p - n.lo) / n.size * float(kCells));
}

float Adf::distance(const Body &body, const Octree &octree, vec3 p) const {
	return sample(body, octree, p).d;
}

Sample Adf::sample(const Body &body, const Octree &, vec3 p) const {
	const int node = leaf(p);
	if (node >= 0 && nodes_[std::size_t(node)].exact()) {
		const ExactCell &cell = exact_cells_[std::size_t(nodes_[std::size_t(node)].material - kExactBase)];
		return Octree::eval_tape(body, cell.base, exact_tape_.data() + cell.offset, cell.count, p);
	}
	Sample s;
	s.d = approx_distance(p);
	if (node < 0) {
		return s;
	}
	const Node &n = nodes_[std::size_t(node)];
	if (n.material >= 0) {
		s.m0 = s.m1 = float(n.material);
		return s;
	}
	const vec3 u = gl::clamp((p - n.lo) / n.size * float(kCells) + 0.5f, 0.0f, float(kCells));
	const std::uint8_t *m = &materials_[std::size_t(-n.material - 1) * kMaterialBytes +
			4 * std::size_t(index(int(u.x), int(u.y), int(u.z)))];
	s.m0 = float(m[0]);
	s.m1 = float(m[1]);
	s.t = float(m[2]) / 255.0f;
	return s;
}

Adf::Step Adf::step(vec3 p, vec3 dir) const {
	const int node = leaf(p);
	if (node < 0) {
		return {gl::SDF_BIG, 0.0f, kEmpty, false, 0.0f, 1.0f, 0.0f};
	}
	const Node &n = nodes_[std::size_t(node)];
	float exit = std::numeric_limits<float>::max();
	const float o[3] = {p.x, p.y, p.z}, d[3] = {dir.x, dir.y, dir.z}, lo[3] = {n.lo.x, n.lo.y, n.lo.z};
	for (int a = 0; a < 3; ++a) {
		if (d[a] > 1e-12f) {
			exit = std::min(exit, (lo[a] + n.size - o[a]) / d[a]);
		} else if (d[a] < -1e-12f) {
			exit = std::min(exit, (lo[a] - o[a]) / d[a]);
		}
	}
	exit = std::max(exit, 0.0f);
	const float voxel = n.size / float(kCells);
	if (n.brick == kEmpty) {
		return {gl::SDF_BIG, exit, kEmpty, false, 0.0f, 1.0f, voxel};
	}
	if (n.brick == kSolid) {
		return {-1.0f, exit, kSolid, false, 0.0f, 1.0f, voxel};
	}
	const float v = brick_value(&values_[std::size_t(n.brick) * kBrickSamples], (p - n.lo) / n.size * float(kCells));
	const float error = n.exact() ? exact_cells_[std::size_t(n.material - kExactBase)].error : 0.0f;
	return {v, exit, n.brick, n.exact(), error, n.value, voxel};
}

vec3 Adf::normal(const Body &body, vec3 p, float eps, vec3 fallback) const {
	const int node = leaf(p);
	if (node < 0 || nodes_[std::size_t(node)].brick < 0) {
		return fallback;
	}
	const Node &n = nodes_[std::size_t(node)];
	const vec3 k[4] = {vec3(1, -1, -1), vec3(-1, -1, 1), vec3(-1, 1, -1), vec3(1, 1, 1)};
	vec3 g(0.0f);
	if (n.exact()) {
		const ExactCell &cell = exact_cells_[std::size_t(n.material - kExactBase)];
		const float h = std::min(std::max(eps, 2e-3f), kExactNormalReach);
		for (const vec3 &kk : k) {
			g += kk * Octree::eval_tape(body, cell.base, exact_tape_.data() + cell.offset, cell.count, p + kk * h).d;
		}
	} else {
		const float h = std::min(std::max(eps, 0.5f * n.size / float(kCells)), 0.25f * n.size);
		const vec3 c = gl::min(gl::max(p, n.lo + vec3(h)), n.lo + vec3(n.size - h));
		const std::uint16_t *b = &values_[std::size_t(n.brick) * kBrickSamples];
		for (const vec3 &kk : k) {
			g += kk * brick_value(b, (c + kk * h - n.lo) / n.size * float(kCells));
		}
	}
	const float len2 = gl::dot(g, g);
	return len2 > 1e-24f ? g / std::sqrt(len2) : fallback;
}

Adf::Stats Adf::stats() const {
	Stats s;
	s.nodes = nodes_.size();
	s.bricks = live_bricks_;
	s.material_bricks = live_materials_;
	s.bytes = live_bricks_ * kBrickSamples * sizeof(std::uint16_t) + live_materials_ * kMaterialBytes + nodes_.size() * 16;
	for (const Node &n : nodes_) {
		s.exact_leaves += n.exact();
	}
	s.mean_exact_tape = exact_cells_.empty() ? 0.0 : double(exact_tape_.size()) / double(exact_cells_.size());
	s.rebuilt_bricks = rebuilt_;
	s.folded_bricks = folded_;
	s.unchanged_bricks = unchanged_;
	s.seconds = seconds_;
	s.finest_voxel = std::numeric_limits<float>::max();
	for (const Node &n : nodes_) {
		if (n.brick >= 0) {
			s.finest_voxel = std::min(s.finest_voxel, n.size / float(kCells));
		}
	}
	return s;
}

} // namespace sdf
