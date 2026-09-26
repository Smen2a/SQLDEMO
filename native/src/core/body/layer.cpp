#include "body/layer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sdf {

namespace {

int floor_div(int a, int b) {
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

} // namespace

void Layer::Block::finish() {
	phi_lo = w_lo = std::numeric_limits<float>::max();
	phi_hi = w_hi = -std::numeric_limits<float>::max();
	for (int i = 0; i < kBlockSamples; ++i) {
		phi_lo = std::min(phi_lo, phi[i]);
		phi_hi = std::max(phi_hi, phi[i]);
		w_lo = std::min(w_lo, w[i]);
		w_hi = std::max(w_hi, w[i]);
	}
}

Layer::Layer(float spacing, const std::vector<std::pair<Coord, std::shared_ptr<const Block>>> &blocks,
		const Measures &measures)
	: h_(spacing), measures_(measures) {
	if (blocks.empty()) {
		return;
	}
	Coord lo = blocks.front().first, hi = lo;
	for (const auto &[c, b] : blocks) {
		for (int a = 0; a < 3; ++a) {
			lo[a] = std::min(lo[a], c[a]);
			hi[a] = std::max(hi[a], c[a]);
		}
	}
	lo_ = lo;
	dims_ = {hi[0] - lo[0] + 1, hi[1] - lo[1] + 1, hi[2] - lo[2] + 1};
	index_.assign(std::size_t(dims_[0]) * std::size_t(dims_[1]) * std::size_t(dims_[2]), -1);
	const float side = float(kBlock) * h_;
	for (const auto &[c, b] : blocks) {
		index_[std::size_t(c[0] - lo_[0]) +
				std::size_t(dims_[0]) * (std::size_t(c[1] - lo_[1]) + std::size_t(dims_[1]) * std::size_t(c[2] - lo_[2]))] =
				std::int32_t(blocks_.size());
		blocks_.push_back(b);
		if (b->w_hi > 0.0f) {
			const vec3 corner = vec3(float(c[0]), float(c[1]), float(c[2])) * side;
			box_.include(corner);
			box_.include(corner + vec3(side - h_));
		}
	}
	if (!box_.empty()) {
		box_ = box_.expanded(1.5f * h_); // the B-spline's reach
	}
}

const Layer::Block *Layer::block(int bx, int by, int bz) const {
	const int x = bx - lo_[0], y = by - lo_[1], z = bz - lo_[2];
	if (x < 0 || y < 0 || z < 0 || x >= dims_[0] || y >= dims_[1] || z >= dims_[2]) {
		return nullptr;
	}
	const std::int32_t i = index_[std::size_t(x) + std::size_t(dims_[0]) * (std::size_t(y) + std::size_t(dims_[1]) * std::size_t(z))];
	return i < 0 ? nullptr : blocks_[std::size_t(i)].get();
}

float Layer::apply(vec3 p, float d) const {
	if (box_.empty() || p.x < box_.lo.x || p.y < box_.lo.y || p.z < box_.lo.z || p.x > box_.hi.x || p.y > box_.hi.y ||
			p.z > box_.hi.z) {
		return d;
	}
	// Quadratic B-spline: the three samples nearest p on each axis.
	const float u[3] = {p.x / h_, p.y / h_, p.z / h_};
	int first[3];
	float weight[3][3];
	for (int a = 0; a < 3; ++a) {
		const float nearest = std::floor(u[a] + 0.5f);
		const float t = u[a] - nearest;
		weight[a][0] = 0.5f * (0.5f - t) * (0.5f - t);
		weight[a][1] = 0.75f - t * t;
		weight[a][2] = 0.5f * (0.5f + t) * (0.5f + t);
		first[a] = int(nearest) - 1;
	}
	// The taps span at most two blocks on each axis: look those up once.
	int base_block[3], which[3][3], local[3][3];
	for (int a = 0; a < 3; ++a) {
		base_block[a] = floor_div(first[a], kBlock);
		for (int k = 0; k < 3; ++k) {
			const int i = first[a] + k - kBlock * base_block[a];
			which[a][k] = i >= kBlock;
			local[a][k] = i - kBlock * which[a][k];
		}
	}
	const Block *near[2][2][2];
	for (int z = 0; z <= which[2][2]; ++z) {
		for (int y = 0; y <= which[1][2]; ++y) {
			for (int x = 0; x <= which[0][2]; ++x) {
				near[z][y][x] = block(base_block[0] + x, base_block[1] + y, base_block[2] + z);
			}
		}
	}
	float w = 0.0f, phi = 0.0f;
	for (int z = 0; z < 3; ++z) {
		for (int y = 0; y < 3; ++y) {
			const float wyz = weight[1][y] * weight[2][z];
			for (int x = 0; x < 3; ++x) {
				const float b = weight[0][x] * wyz;
				const Block *blk = near[which[2][z]][which[1][y]][which[0][x]];
				if (!blk) {
					phi += b * d;
					continue;
				}
				const int s = local[0][x] + kBlock * (local[1][y] + kBlock * local[2][z]);
				w += b * blk->w[s];
				phi += b * blk->phi[s];
			}
		}
	}
	return w > 0.0f ? d + w * (phi - d) : d;
}

Layer::Range Layer::range(vec3 lo, float size) const {
	if (blocks_.empty() || box_.empty() || lo.x > box_.hi.x || lo.y > box_.hi.y || lo.z > box_.hi.z ||
			lo.x + size < box_.lo.x || lo.y + size < box_.lo.y || lo.z + size < box_.lo.z) {
		return {};
	}
	const float reach = 1.5f * h_;
	const float from[3] = {lo.x - reach, lo.y - reach, lo.z - reach};
	int b0[3], b1[3];
	bool missing = false;
	for (int a = 0; a < 3; ++a) {
		b0[a] = floor_div(int(std::floor(from[a] / h_)), kBlock);
		b1[a] = floor_div(int(std::ceil((from[a] + size + 2.0f * reach) / h_)), kBlock);
		if (b0[a] < lo_[a] || b1[a] >= lo_[a] + dims_[a]) {
			missing = true;
		}
		b0[a] = std::max(b0[a], lo_[a]);
		b1[a] = std::min(b1[a], lo_[a] + dims_[a] - 1);
	}
	Range r{std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			-std::numeric_limits<float>::max()};
	bool any = false;
	for (int z = b0[2]; z <= b1[2]; ++z) {
		for (int y = b0[1]; y <= b1[1]; ++y) {
			for (int x = b0[0]; x <= b1[0]; ++x) {
				const Block *blk = block(x, y, z);
				if (!blk) {
					missing = true;
					continue;
				}
				any = true;
				r.w_lo = std::min(r.w_lo, blk->w_lo);
				r.w_hi = std::max(r.w_hi, blk->w_hi);
				r.phi_lo = std::min(r.phi_lo, blk->phi_lo);
				r.phi_hi = std::max(r.phi_hi, blk->phi_hi);
			}
		}
	}
	if (!any || r.w_hi <= 0.0f) {
		return {};
	}
	if (missing) {
		r.w_lo = 0.0f;
	}
	return r;
}

} // namespace sdf
