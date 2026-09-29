#include "adf/sampler.h"

#include "util/parallel.h"

namespace sdf {

void CpuSampler::sample_job(const Body &body, const AdfJob &job, Sample *corners, float *centres) {
	const Octree::Leaf &cell = *job.cell;
	for (int z = 0, i = 0; z < 8; ++z) {
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x, ++i) {
				corners[i] = Octree::eval_tape(body, cell.base, cell.tape.data(), cell.tape.size(),
						job.lo + vec3(float(x), float(y), float(z)) * job.voxel);
			}
		}
	}
	if (!centres) {
		return;
	}
	for (int z = 0, i = 0; z < 7; ++z) {
		for (int y = 0; y < 7; ++y) {
			for (int x = 0; x < 7; ++x, ++i) {
				centres[i] = Octree::eval_tape(body, cell.base, cell.tape.data(), cell.tape.size(),
						job.lo + (vec3(float(x), float(y), float(z)) + 0.5f) * job.voxel)
									 .d;
			}
		}
	}
}

void CpuSampler::sample(const Body &body, const std::vector<AdfJob> &jobs, Sample *corners, float *centres) {
	const bool with_centres = centres && batches_centres();
	parallel_for(
			jobs.size(),
			[&](std::size_t j) {
				sample_job(body, jobs[j], corners + j * kCorners, with_centres ? centres + j * kCentres : nullptr);
			},
			threads_);
}

} // namespace sdf
