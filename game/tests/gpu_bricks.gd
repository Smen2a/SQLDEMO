extends Node

## Bricks sampled on the GPU (a compute shader on a local RenderingDevice, so Forward+ or
## Mobile: run with tools/run.sh --vulkan) against bricks the CPU samples for the same
## bodies: after a build, after edits (coarse crease cells) and once refined when idle,
## both must agree within twice the ADF tolerance wherever both hold bricks. "sanded" has
## smoothing layers, whose bricks the CPU samples alongside the GPU.

const DEMOS := ["carved_panel", "sanded", "board"]
const MAX_DIFFERENCE := 0.02 # mm: twice AdfParams::tolerance

var _failed := false


func _ready() -> void:
	if RenderingServer.get_rendering_device() == null:
		printerr("gpu bricks: no RenderingDevice; run with tools/run.sh --vulkan")
		get_tree().quit(1)
		return
	for demo in DEMOS:
		# Timings, CPU then GPU (the GPU's include uploads and readbacks; on lavapipe the "GPU"
		# is the CPU too, so only a real one tells).
		var cpu_body = ClassDB.instantiate("SdfBody")
		add_child(cpu_body)
		cpu_body.gpu_bricks = false
		cpu_body.load_demo(demo)
		var cpu_build: float = cpu_body.get_stats().get("update_ms", 0.0)
		cpu_body.queue_free()

		var body = ClassDB.instantiate("SdfBody")
		add_child(body)
		body.load_demo(demo)
		var gpu_build: float = body.get_stats().get("update_ms", 0.0)
		_check(body, demo, "built")
		body.add_random_strokes(40, 7)
		_check(body, demo, "edited")
		var frames := 0
		while (body.is_busy() or body.get_stats().get("refine_pending", false)) and frames < 1200:
			await get_tree().process_frame
			frames += 1
		_check(body, demo, "refined")
		print("     %-13s build: CPU %.0f ms, GPU sampler %.0f ms; refining 40 strokes: %.0f ms (%.0f ms on the GPU)" % [
				demo, cpu_build, gpu_build, body.get_stats().get("refine_ms", 0.0), body.get_stats().get("gpu_ms", 0.0)])
		body.queue_free()
		await get_tree().process_frame
	if _failed:
		printerr("gpu bricks: FAILED")
		get_tree().quit(1)
		return
	print("gpu bricks: every demo agrees with the CPU")
	get_tree().quit()


func _check(body, demo: String, stage: String) -> void:
	var r: Dictionary = body.compare_bricks_with_cpu(4000)
	var ok: bool = r.get("sampler", "") == "gpu" and int(r.get("gpu_bricks_sampled", 0)) > 0 \
			and int(r.get("compared", 0)) > 500 and float(r.get("max_difference", 1.0)) <= MAX_DIFFERENCE
	print("%s %-13s %-8s %s" % ["ok  " if ok else "FAIL", demo, stage, r])
	if not ok:
		_failed = true
