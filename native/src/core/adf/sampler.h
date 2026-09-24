#pragma once

#include "body/body.h"
#include "compile/octree.h"

#include <vector>

namespace sdf {

// A brick to sample: the 8^3 corners of the cube at `lo` (x fastest, then y, then z),
// `voxel` apart, through `cell`'s tape (valid over the cube). Also its 7^3 voxel centres,
// when the sampler batches them.
struct AdfJob {
	vec3 lo;
	float voxel = 0;
	const Octree::Leaf *cell = nullptr;
};

// Where an ADF's samples come from: the exact field through each cube's pruned tape, a level
// of the refinement at a time (see Adf). The CPU evaluates them by default; a GPU sampler
// takes whole levels in one dispatch.
class AdfSampler {
public:
	static constexpr int kCorners = 512, kCentres = 343;

	virtual ~AdfSampler() = default;
	// Whether sample() fills the voxel centres too. If not, the refiner evaluates only the
	// ones it needs, near the surface, stopping at the first that misses the tolerance.
	virtual bool batches_centres() const { return false; }
	// corners[j * kCorners + i] for job j and corner i; with batched centres, also
	// centres[j * kCentres + i] (distances only). Positions are lo + vec3(x, y, z) * voxel
	// and lo + (vec3(x, y, z) + 0.5) * voxel, computed exactly so.
	virtual void sample(const Body &body, const std::vector<AdfJob> &jobs, Sample *corners, float *centres) = 0;
};

// Octree::eval_tape on the CPU, jobs spread over `threads` (0 = the hardware's).
class CpuSampler : public AdfSampler {
public:
	explicit CpuSampler(int threads = 0) : threads_(threads) {}
	void sample(const Body &body, const std::vector<AdfJob> &jobs, Sample *corners, float *centres) override;

	// One job's samples, for samplers that hand some jobs back to the CPU.
	static void sample_job(const Body &body, const AdfJob &job, Sample *corners, float *centres);

private:
	int threads_;
};

// A CpuSampler that batches centres, as a GPU sampler does: it exercises the refiner's
// batched path without a GPU (tests), and gives the same trees as the lazy one.
class BatchingCpuSampler : public CpuSampler {
public:
	using CpuSampler::CpuSampler;
	bool batches_centres() const override { return true; }
};

} // namespace sdf
