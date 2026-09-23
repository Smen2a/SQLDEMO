extends Node3D

## Decision-gate benchmark for the Live raymarch path (plan §4): times each scenario and
## prints GPU and CPU render times per frame. Run it on real hardware, in the renderer to
## judge (the project default is Forward+):
##
##   godot --path game res://bench/live_bench.tscn
##   godot --path game --rendering-method gl_compatibility res://bench/live_bench.tscn
##
## User args (after `--`): --scenario=<name> to run one, --frames=<n> measured frames per
## scenario (default 240), --view=steps to show step-count heat maps, --shots=<dir> to save
## a screenshot of each scenario. To see where the time goes: --shadows=off (no shadow
## passes), --ao=off (no SDF ambient occlusion).
##
## Gate targets at 1920x1080: a bench-filling body <= ~6 ms GPU, three Live bodies <= ~8 ms.

const WARMUP_FRAMES := 30  # override with --warmup=<n>

## name -> [description, body layout, eye, target, target ms or 0]
var scenarios := {
	"empty": ["no bodies: the frame's fixed cost", [], Vector3(0, -150, 120), Vector3.ZERO, 0.0],
	"panel_fill": ["carved panel filling the screen", [["carved_panel", Vector3.ZERO, 0]],
			Vector3(0, -62, 78), Vector3(0, -3, 0), 6.0],
	"panel_close": ["close-up on the rosette, the carving view", [["carved_panel", Vector3.ZERO, 0]],
			Vector3(6, -26, 32), Vector3(0, 0, 4), 6.0],
	"session_2000": ["2000 random strokes (long tapes), filling the screen",
			[["session", Vector3.ZERO, 1700]], Vector3(0, -62, 78), Vector3(0, -3, 0), 6.0],
	"three_bodies": ["panel, fluted sphere and 300-stroke session side by side",
			[["carved_panel", Vector3(-68, 0, 0), 0], ["session", Vector3(68, 0, 0), 0],
			["sphere", Vector3(0, 75, 30), 0]], Vector3(0, -140, 125), Vector3(0, 22, 0), 8.0],
}

var _camera: Camera3D
var _bodies: Array = []


func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.27, 0.285, 0.31)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.42, 0.47, 0.55)
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = _arg("--shadows", "on") == "on"
	sun.directional_shadow_max_distance = 600.0
	add_child(sun)
	sun.look_at_from_position(Vector3.ZERO, -Vector3(-0.55, -0.40, 0.73), Vector3(0, 0, 1))
	_camera = Camera3D.new()
	_camera.fov = 38.0
	_camera.near = 1.0
	_camera.far = 4000.0
	add_child(_camera)

	await get_tree().process_frame
	var size := get_viewport().get_visible_rect().size
	print("Live raymarch benchmark: %s, %s, %dx%d" % [RenderingServer.get_video_adapter_name(),
			RenderingServer.get_current_rendering_method(), size.x, size.y])
	if size != Vector2(1920, 1080):
		print("  (gate targets assume 1920x1080; the window is %dx%d)" % [size.x, size.y])

	var only := _arg("--scenario", "")
	var frames := int(_arg("--frames", "240"))
	var rows: Array = []
	for name in scenarios:
		if only != "" and name != only:
			continue
		rows.append(await _run(name, scenarios[name], frames))
	print("")
	print("%-14s %9s %9s %9s %9s  %s" % ["scenario", "gpu med", "gpu p95", "cpu med", "frame med", "gate"])
	for r in rows:
		print("%-14s %7.2fms %7.2fms %7.2fms %7.2fms  %s" % r)
	print("GPU times read 0 where the renderer does not measure them (Compatibility); use frame times there.")
	get_tree().quit()


func _run(name: String, s: Array, frames: int) -> Array:
	for b in _bodies:
		b.queue_free()
	_bodies.clear()
	var view := ["shaded", "normals", "steps", "albedo"].find(_arg("--view", "shaded"))
	for layout in s[1]:
		var body = ClassDB.instantiate("SdfBody")
		add_child(body)
		body.position = layout[1]
		body.load_demo(layout[0])
		if layout[2] > 0:
			body.add_random_strokes(layout[2], 7)
		body.debug_view = view
		body.material_override.set_shader_parameter("sdf_ambient_occlusion", _arg("--ao", "on") == "on")
		_bodies.append(body)
	_camera.look_at_from_position(s[2], s[3], Vector3(0, 0, 1))

	for i in int(_arg("--warmup", str(WARMUP_FRAMES))):
		await RenderingServer.frame_post_draw
	var rid := get_viewport().get_viewport_rid()
	var gpu: Array[float] = []
	var cpu: Array[float] = []
	var frame: Array[float] = []
	var last := Time.get_ticks_usec()
	for i in frames:
		await RenderingServer.frame_post_draw
		var now := Time.get_ticks_usec()
		frame.append((now - last) / 1000.0)
		last = now
		gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(rid))
		cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(rid))

	var shots := _arg("--shots", "")
	if shots != "":
		DirAccess.make_dir_recursive_absolute(shots)
		get_viewport().get_texture().get_image().save_png("%s/%s.png" % [shots, name])

	var stats := ""
	for b in _bodies:
		var st: Dictionary = b.get_stats()
		stats += " [%d edits, %d surface cells, mean tape %.1f]" % [st.edits, st.surface_leaves, st.mean_tape]
	print("%-14s %s%s" % [name, s[0], stats])
	var gpu_med := _percentile(gpu, 0.5)
	var target: float = s[4]
	var verdict := "-"
	if target > 0.0:
		var measured := gpu_med if gpu_med > 0.0 else _percentile(frame, 0.5)
		verdict = "%s (<= %.0f ms)" % ["pass" if measured <= target else "FAIL", target]
	return [name, gpu_med, _percentile(gpu, 0.95), _percentile(cpu, 0.5), _percentile(frame, 0.5), verdict]


func _percentile(values: Array[float], q: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[mini(int(q * sorted.size()), sorted.size() - 1)]


func _arg(name: String, default: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(name + "="):
			return arg.substr(name.length() + 1)
	return default
