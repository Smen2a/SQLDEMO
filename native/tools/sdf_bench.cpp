// Octree scaling benchmark: builds, incrementally updates and queries bodies with growing
// edit counts, and reports tape statistics and memory.
//   sdf_bench [max_edits]

#include "compile/octree.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

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

int main(int argc, char **argv) {
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
