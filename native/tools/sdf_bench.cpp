// Octree scaling benchmark: builds, incrementally updates and queries bodies with growing
// edit counts, and reports tape statistics and memory.
//   sdf_bench [max_edits]
// Adaptive distance field (Live display cache): build cost, size and per-stroke updates.
//   sdf_bench adf
// Texel fetches per pixel of the Live ADF shader, replayed on the CPU for the Live bench's
// scenarios (game/bench/live_bench.gd) at 1280x720. The shader is fetch bound, so this
// ranks optimizations before a GPU run; keep it in step with sdf_live.gdshaderinc.
//   sdf_bench count
// Live tool use on the workshop board: the time each drag step takes to update the octree
// and ADF, as the Godot workshop applies them.
//   sdf_bench tools

#include "adf/adf.h"
#include "compile/octree.h"
#include "demo/gallery.h"
#include "edit/session.h"
#include "tools/tools.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <string>

using namespace sdf;

namespace {

double now() {
	return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A granite block roughed out by percussive chips: each blow removes a small, irregular
// piece near the current working face, which is lowered steadily as the job progresses.
Body stone_job(int chips, unsigned seed) {
	std::mt19937 gen(seed);
	auto uni = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(gen); };
	Body b;
	b.base = Primitive::box({0, 0, 0}, {400, 250, 150}, 2.0f);
	b.base_material = 6;
	for (int i = 0; i < chips; ++i) {
		const float progress = float(i) / float(chips);
		const float face = 150.0f - 60.0f * progress; // the working face sinks 60 mm
		const vec3 c(uni(-380, 380), uni(-230, 230), face + uni(-3, 4));
		Edit e;
		e.op = Op::Subtract;
		const vec3 axis(uni(-1, 1), uni(-1, 1), uni(-1, 1));
		e.prim = Primitive::box(c, {uni(4, 12), uni(3, 9), uni(1.5f, 4)}, 0.5f,
				quat_axis_angle(gl::length(axis) > 1e-3f ? axis : vec3(0, 0, 1), uni(0, 3.14f)));
		b.add(e);
	}
	return b;
}

void report(const char *what, int edits, const Body &body, const Octree &oct, double build_s) {
	const Octree::Stats s = oct.stats();
	const double tape_mb = double(s.tape_entries) * 4.0 / 1e6;
	const double node_mb = double(s.nodes) * sizeof(Octree::Node) / 1e6;
	std::printf("%-8s %7d edits  build %7.3fs  nodes %8zu  surface leaves %8zu  mean tape %5.2f  max %4zu  "
			"memory %6.1f MB\n", what, edits, build_s, s.nodes, s.surface_leaves, s.mean_surface_tape, s.max_tape,
			tape_mb + node_mb);
	(void)body;
}

} // namespace

int adf_bench() {
	struct Case {
		const char *demo;
		int extra_strokes;
	};
	for (const Case c : {Case{"carved_panel", 0}, Case{"sphere", 0}, Case{"session", 0}, Case{"session", 1700}}) {
		Body body;
		Camera camera;
		demo::named_demo(c.demo, body, camera);
		for (const Edit &e : demo::random_strokes(body, c.extra_strokes, 7)) {
			body.add(e);
		}
		Octree oct;
		oct.build(body);
		Adf adf;
		adf.build(body, oct);
		const Adf::Stats s = adf.stats();
		std::printf("%-13s %5zu edits  build %6.2fs  %6zu bricks %6.1f MB  %6zu exact cells  %7zu nodes  finest %.3f mm\n",
				c.demo, body.edits().size(), s.seconds, s.bricks, double(s.bytes) / 1048576.0, s.exact_leaves, s.nodes,
				double(s.finest_voxel));
		// One stroke at a time, as a carver works: octree update, then the ADF around it.
		double worst = 0, total = 0, oct_total = 0;
		std::size_t rebuilt = 0;
		int strokes = 0;
		for (const Edit &e : demo::random_strokes(body, 40, 11)) {
			if (!body.add(e)) {
				continue;
			}
			const std::size_t index = body.edits().size() - 1;
			const double t0 = now();
			oct.add_edit(body, std::uint32_t(index));
			const double t1 = now();
			adf.update(body, oct, Adf::dirty_region(body, oct, index));
			const double t2 = now();
			oct_total += t1 - t0;
			total += t2 - t1;
			worst = std::max(worst, t2 - t1);
			rebuilt += adf.stats().rebuilt_bricks;
			++strokes;
		}
		std::printf("              per stroke: octree %.1f ms, ADF %.1f ms (worst %.1f), %.0f bricks re-sampled\n",
				1e3 * oct_total / strokes, 1e3 * total / strokes, 1e3 * worst, double(rebuilt) / strokes);
	}
	return 0;
}

namespace {

// Replays fragment() of sdf_live.gdshaderinc (ADF source) and tallies texel fetches.
struct FetchCounter {
	const Body &body;
	const Octree &octree;
	const Adf &adf;
	double descent = 0, brick = 0, tape = 0, shading = 0, steps = 0, empty_steps = 0, enters = 0, settle_steps = 0, hits = 0;
	double tape_evals[3] = {0, 0, 0}; // march, settle, normal
	int phase = 0;

	struct Cell {
		int node = -1;
		vec3 lo;
		float size = -1.0f;
	};

	const Adf::Node &node(const Cell &c) const { return adf.nodes()[std::size_t(c.node)]; }

	static bool inside(vec3 p, vec3 lo, float size) {
		const vec3 r = p - lo;
		return std::min(r.x, std::min(r.y, r.z)) >= 0.0f && std::max(r.x, std::max(r.y, r.z)) <= size;
	}
	bool in_root(vec3 p) const { return inside(p, adf.nodes()[0].lo, adf.nodes()[0].size); }

	// sdf_enter_cell: sdf_adf_find_leaf reads the grid texel, then a node texel per level
	// down to the leaf's, then an exact leaf's cell texel.
	Cell enter(vec3 p) {
		const auto &nodes = adf.nodes();
		const vec3 g = (p - nodes[0].lo) * adf.grid_scale();
		auto block = [](float c) { return std::clamp(int(std::floor(c)), 0, Adf::kGridSide - 1); };
		int n = adf.grid()[std::size_t(block(g.x) + Adf::kGridSide * (block(g.y) + Adf::kGridSide * block(g.z)))].node;
		descent += 2;
		enters += 1;
		while (nodes[std::size_t(n)].child >= 0) {
			const Adf::Node &x = nodes[std::size_t(n)];
			const float h = x.size * 0.5f;
			n = x.child + int(p.x >= x.lo.x + h) + 2 * int(p.y >= x.lo.y + h) + 4 * int(p.z >= x.lo.z + h);
			descent += 1;
		}
		descent += nodes[std::size_t(n)].exact() ? 1 : 0;
		return {n, nodes[std::size_t(n)].lo, nodes[std::size_t(n)].size};
	}
	void count_tape(const Adf::Node &n) {
		const std::uint32_t count = adf.exact_cells()[std::size_t(n.material - Adf::kExactBase)].count;
		tape += 6.0 * count + std::ceil(count / 4.0) + 1.0;
		tape_evals[phase] += 1;
	}
	// sdf_cell_eval
	float eval(const Cell &c, vec3 p) {
		const Adf::Node &n = node(c);
		if (n.exact()) {
			count_tape(n);
		} else if (n.brick >= 0) {
			brick += 2 + (n.material < 0 ? 1 : 0);
		}
		return n.brick < 0 ? n.value : adf.distance(body, octree, p);
	}
	float near(Cell &c, vec3 p) {
		if (!inside(p, c.lo, c.size)) {
			if (!in_root(p)) {
				return 1.0f;
			}
			c = enter(p);
		}
		return eval(c, p);
	}

	// One pixel's ray.
	void trace(vec3 ro, vec3 rd, float t0, float t1, float pixel) {
		float t = t0, eps = 1e-4f, s = 0.0f;
		Cell c;
		bool hit = false;
		phase = 0;
		for (int i = 0; i < 512 && t <= t1; ++i) {
			steps += 1;
			const vec3 p = ro + rd * t;
			eps = std::max(t * pixel * 0.5f, 1e-4f);
			if (c.node < 0 || !inside(p, c.lo, c.size)) {
				if (!in_root(p)) {
					break;
				}
				c = enter(p);
			}
			const Adf::Step st = adf.step(p, rd);
			const Adf::Node &n = node(c);
			if (n.brick == Adf::kEmpty) {
				empty_steps += 1;
				t += st.exit + 1e-4f;
				continue;
			}
			if (n.exact()) {
				brick += 2;
				const float coarse = st.d - st.error;
				if (coarse > eps) {
					t += std::min(std::max(coarse / n.value, eps * 0.5f), st.exit + 1e-4f);
					continue;
				}
			}
			s = n.brick == Adf::kSolid ? -1.0f : eval(c, p);
			if (s < eps) {
				hit = true;
				break;
			}
			t += std::min(std::max(s / n.value, eps * 0.5f), st.exit + 1e-4f);
		}
		if (!hit) {
			return;
		}
		hits += 1;
		phase = 1;
		for (int i = 0; i < 4 && std::fabs(s) > std::max(0.1f * eps, 1e-5f); ++i) {
			t += s;
			s = near(c, ro + rd * t);
			settle_steps += 1;
		}
		phase = 2;
		const vec3 p = ro + rd * t;
		// sdf_adf_normal: one tape pass in exact leaves, four brick lookups otherwise.
		if (!inside(p, c.lo, c.size) && in_root(p)) {
			c = enter(p);
		}
		if (node(c).exact()) {
			count_tape(node(c));
		} else if (node(c).brick >= 0) {
			brick += 8;
		}
		shading += 4; // material colour and properties
		const vec3 nrm = adf.normal(body, p, eps, -rd);
		for (float h : {0.3f, 0.9f, 1.8f, 3.0f, 4.5f}) { // sdf_occlusion
			const vec3 q = p + nrm * h;
			Cell ao = c;
			if (!inside(q, ao.lo, ao.size)) {
				if (!in_root(q)) {
					continue;
				}
				ao = enter(q);
			}
			brick += node(ao).brick >= 0 ? 2 : 0;
		}
	}
};

int count_bench() {
	struct Scenario {
		const char *name, *demo;
		int strokes;
		vec3 eye, target;
	};
	const Scenario scenarios[] = {
			{"panel_fill", "carved_panel", 0, {0, -62, 78}, {0, -3, 0}},
			{"panel_close", "carved_panel", 0, {6, -26, 32}, {0, 0, 4}},
			{"session_2000", "session", 1700, {0, -62, 78}, {0, -3, 0}},
	};
	const int width = 1280, height = 720, stride = 4; // every 4th pixel each way
	for (const Scenario &sc : scenarios) {
		Body body;
		Camera camera;
		demo::named_demo(sc.demo, body, camera);
		for (const Edit &e : demo::random_strokes(body, sc.strokes, 7)) {
			body.add(e);
		}
		Octree oct;
		oct.build(body);
		Adf adf;
		adf.build(body, oct);
		FetchCounter fc{body, oct, adf};
		const vec3 f = gl::normalize(sc.target - sc.eye), r = gl::normalize(gl::cross(f, vec3(0, 0, 1))),
				   u = gl::cross(r, f);
		const float tan_half = std::tan(gl::radians(38.0f) * 0.5f), pixel = 2.0f * tan_half / float(height);
		const Aabb box = body.bounds().expanded(0.5f);
		double covered = 0;
		for (int y = stride / 2; y < height; y += stride) {
			for (int x = stride / 2; x < width; x += stride) {
				const vec3 rd = gl::normalize(f + r * ((2.0f * (x + 0.5f) / width - 1.0f) * tan_half * width / height) +
						u * ((1.0f - 2.0f * (y + 0.5f) / height) * tan_half));
				float t0 = 0.0f, t1 = 1e9f;
				for (int a = 0; a < 3; ++a) {
					const float o = (&sc.eye.x)[a], d = (&rd.x)[a];
					const float inv = 1.0f / (std::fabs(d) > 1e-12f ? d : 1e-12f);
					float ta = ((&box.lo.x)[a] - o) * inv, tb = ((&box.hi.x)[a] - o) * inv;
					if (ta > tb) {
						std::swap(ta, tb);
					}
					t0 = std::max(t0, ta);
					t1 = std::min(t1, tb);
				}
				if (t0 > t1) {
					continue;
				}
				covered += 1;
				fc.trace(sc.eye, rd, t0, t1, pixel);
			}
		}
		const double total = fc.descent + fc.brick + fc.tape + fc.shading;
		std::printf("%-13s per pixel on the proxy: %5.1f steps; texel fetches: descent %5.1f, bricks %5.1f, tapes %6.1f,"
					" shading %3.1f, total %6.1f\n",
				sc.name, fc.steps / covered, fc.descent / covered, fc.brick / covered, fc.tape / covered,
				fc.shading / covered, total / covered);
		std::printf("              tape evaluations %.2f march + %.2f settle + %.2f normal; %.1f settling steps per hit;"
					" %.0f%% of the proxy hit\n",
				fc.tape_evals[0] / covered, fc.tape_evals[1] / covered, fc.tape_evals[2] / covered, fc.settle_steps / fc.hits,
				100.0 * fc.hits / covered);
		std::printf("              %.1f steps through empty cells; %.1f descents of %.1f fetches\n", fc.empty_steps / covered,
				fc.enters / covered, fc.descent / fc.enters);
	}
	return 0;
}

} // namespace

int tools_bench() {
	EditSession s;
	s.reset(demo::board(mat::Ash));
	std::printf("board: %zu bricks, built in %.0f ms\n", s.adf().stats().bricks, s.last().octree_ms + s.last().adf_ms);
	const float top = 12.5f;
	const vec3 up(0, 0, 1);
	struct Use {
		const char *name;
		std::unique_ptr<tools::Stroke> stroke;
		std::function<vec3(int step)> motion; // the tool's position on the work plane
	};
	Use uses[] = {
			{"chisel 12 mm, 1.5 mm deep, pushed 60 mm", tools::chisel_stroke(tools::Chisel{}, {-50, -20, top}, up, {1, 0, 0}, 1.5f),
					[&](int k) { return vec3(-50.0f + 2.0f * float(k + 1), -20, top); }},
			{"saw, 12 mm deep in 30 strokes", tools::saw_stroke(tools::Saw{}, {30, 0, top}, up, {0, 1, 0}, 0.02f),
					[&](int k) { return vec3(30, k % 2 ? -10.0f : 10.0f, top); }},
			{"sanding block rubbed over the board", tools::sanding_stroke(tools::SandingBlock{}, {20, 10, top}, up, {1, 0, 0}),
					[&](int k) { return vec3(20.0f + 25.0f * std::sin(0.7f * float(k)), 10.0f + 8.0f * std::cos(0.3f * float(k)), top); }},
	};
	for (Use &use : uses) {
		double total = 0, worst = 0, octree = 0, bricks = 0;
		int updates = 0;
		const int steps = 30;
		for (int k = 0; k < steps; ++k) {
			const tools::StrokeUpdate u = use.stroke->move_to(use.motion(k));
			if (u.empty()) {
				continue;
			}
			const double t0 = now();
			s.revise_stroke(u.drop, u.edits);
			const double ms = 1e3 * (now() - t0);
			total += ms;
			worst = std::max(worst, ms);
			octree += s.last().octree_ms;
			bricks += double(s.last().rebuilt_bricks);
			++updates;
		}
		s.extend_stroke(use.stroke->finish());
		s.commit();
		std::printf("%-40s %2d updates: %5.1f ms mean (octree %.1f), %5.1f worst, %4.0f bricks re-sampled; %zu edits so far\n",
				use.name, updates, updates ? total / updates : 0.0, updates ? octree / updates : 0.0, worst,
				updates ? bricks / updates : 0.0, s.body().edits().size());
	}
	const double t0 = now();
	s.undo();
	std::printf("undo (the sanding): %.1f ms\n", 1e3 * (now() - t0));
	return 0;
}

int main(int argc, char **argv) {
	if (argc > 1 && std::string(argv[1]) == "tools") {
		return tools_bench();
	}
	if (argc > 1 && std::string(argv[1]) == "adf") {
		return adf_bench();
	}
	if (argc > 1 && std::string(argv[1]) == "count") {
		return count_bench();
	}
	const int max_edits = argc > 1 ? std::atoi(argv[1]) : 100000;
	for (int n = 1000; n <= max_edits; n *= 10) {
		const Body body = stone_job(n, 7);
		Octree oct;
		double t0 = now();
		oct.build(body);
		report("build", int(body.edits().size()), body, oct, now() - t0);

		// Incremental: replay the same job one chip at a time.
		Body growing;
		growing.base = body.base;
		growing.base_material = body.base_material;
		Octree inc;
		inc.build(growing);
		t0 = now();
		for (std::size_t i = 0; i < body.edits().size(); ++i) {
			growing.add(body.edits()[i]);
			inc.add_edit(growing, std::uint32_t(i));
		}
		const double total = now() - t0;
		std::printf("         incremental: %.3fs total, %.1f us per edit\n", total, 1e6 * total / double(body.edits().size()));

		// Query throughput near the worked face (where tool contact and rendering happen).
		std::mt19937 gen(3);
		std::uniform_real_distribution<float> ux(-390, 390), uy(-240, 240), uz(80, 155);
		const int queries = 200000;
		float sink = 0;
		t0 = now();
		for (int q = 0; q < queries; ++q) {
			sink += oct.distance(body, {ux(gen), uy(gen), uz(gen)});
		}
		const double oct_rate = queries / (now() - t0);
		const int slow_queries = n <= 10000 ? 20000 : 2000;
		t0 = now();
		for (int q = 0; q < slow_queries; ++q) {
			sink += body.sample_exhaustive({ux(gen), uy(gen), uz(gen)}).d;
		}
		const double full_rate = slow_queries / (now() - t0);
		std::printf("         queries: octree %.2f M/s, full edit list %.3f M/s (%.0fx)  [%g]\n\n", oct_rate / 1e6,
				full_rate / 1e6, oct_rate / full_rate, double(sink > 0));
	}
	return 0;
}
