#pragma once

#include "adf/sampler.h"

#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace godot {
class RenderingDevice;
}

namespace sdf::godot_bind {

// Samples ADF bricks on the GPU (see AdfSampler): a level of the refinement in one compute
// dispatch, each workgroup evaluating one brick's 512 corners and 343 voxel centres through
// its pruned tape. The shader is built from the same includes as the Live shader
// (primitives and blends, res://shaders/sdf), with a tape interpreter reading storage
// buffers, so the GPU samples the field the Live shader draws. Jobs whose tapes hold a
// smoothing layer (a sampled grid only the CPU holds) are sampled on the CPU meanwhile.
//
// Godot binds a local RenderingDevice to the thread that creates it, so the sampler runs
// its own thread: sample() hands it the batch and waits. One sampler serves every body.
class GpuSampler : public AdfSampler {
public:
	// The shared sampler, created on first use; null where there is no RenderingDevice
	// (the Compatibility renderer) or the shader fails to build (reported once).
	static std::shared_ptr<GpuSampler> shared();
	~GpuSampler() override;

	bool batches_centres() const override { return true; }
	void sample(const Body &body, const std::vector<AdfJob> &jobs, Sample *corners, float *centres) override;

	struct Stats {
		double ms = 0;             // in sample(), GPU and CPU jobs together
		double gpu_ms = 0;         // uploading, dispatching and reading back
		std::size_t gpu_jobs = 0, cpu_jobs = 0, dispatches = 0;
	};
	// Totals since the last call.
	Stats take_stats();

private:
	GpuSampler() = default;
	bool start(const godot::String &source);
	void run(const godot::String &source); // the device's thread
	// Runs `task` on the device's thread and waits for it.
	void call(const std::function<void()> &task);

	// On the device's thread only.
	bool build(const godot::String &source);
	void release();
	void ensure(int binding, std::size_t bytes);
	struct Batch;
	void dispatch(const Batch &batch, std::vector<float> &corners, std::vector<float> &centres);

	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable wake_, done_;
	std::function<void()> task_;
	bool pending_ = false; // task_ is waiting to run, or running
	bool quit_ = false, started_ = false, ok_ = false;
	std::mutex call_mutex_; // one batch at a time, whichever body sends it

	godot::RenderingDevice *rd_ = nullptr;
	godot::RID shader_, pipeline_, set_;
	static constexpr int kBindings = 5;
	godot::RID buffers_[kBindings];
	std::size_t capacity_[kBindings] = {};

	std::mutex stats_mutex_;
	Stats stats_;
};

} // namespace sdf::godot_bind
