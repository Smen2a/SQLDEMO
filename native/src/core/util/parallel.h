#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace sdf {

// f(i) for i in [0, n), spread over `threads` (0 = the hardware's), at most one per `grain`
// items. Items are handed out one at a time, so uneven ones balance.
template <typename F>
void parallel_for(std::size_t n, F &&f, int threads = 0, std::size_t grain = 1) {
	const std::size_t wanted = threads > 0 ? std::size_t(threads) : std::size_t(std::max(1u, std::thread::hardware_concurrency()));
	const std::size_t count = std::min(wanted, (n + grain - 1) / std::max<std::size_t>(grain, 1));
	if (count <= 1) {
		for (std::size_t i = 0; i < n; ++i) {
			f(i);
		}
		return;
	}
	std::atomic<std::size_t> next{0};
	auto run = [&]() {
		for (std::size_t i = next++; i < n; i = next++) {
			f(i);
		}
	};
	std::vector<std::thread> pool;
	for (std::size_t t = 1; t < count; ++t) {
		pool.emplace_back(run);
	}
	run();
	for (std::thread &t : pool) {
		t.join();
	}
}

} // namespace sdf
