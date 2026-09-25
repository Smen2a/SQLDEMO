extends "res://tests/harness.gd"

## Saws a strip off the workshop board and watches the offcut for a second of physics:
## how far it sinks into the bench top (it should rest on it), how far it tilts (it should
## stay upright) and how far it slides off the kerf (the nudge it gets). Prints one line
## with the physics engine in use. Fails unless the offcut sinks at most 0.3 mm, tilts at
## most 2 degrees and slides 2-15 mm; --report-only just reports (tools/compare_physics.sh).

const Workshop := preload("res://workshop/workshop.tscn")
const MAX_SINK := 0.3   # mm
const MAX_TILT := 2.0   # degrees
const SLIDE := [2.0, 15.0] # mm

var workshop


func _ready() -> void:
	get_tree().create_timer(600.0).timeout.connect(func():
		push_error("offcut physics: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	workshop.offcut_collider = user_arg("--collider", "auto")
	add_child(workshop)
	await _frames(3)
	workshop.select_tool("saw")
	workshop.set_setting("saw", "feed", 0.1)
	await _frames(8)
	var cam: Camera3D = workshop.camera
	var start := Vector3(0.0, 0.025, 0.03)
	workshop.hover_screen(cam.unproject_position(start))
	await _frames(2)
	workshop.lock(cam.unproject_position(start))
	workshop.aim(cam.unproject_position(Vector3(-0.03, 0.025, 0.03)))
	workshop.press(cam.unproject_position(start))
	var from := start
	for i in 12:
		var to := Vector3(-0.03 if i % 2 == 0 else 0.03, 0.025, 0.03)
		for k in 2:
			workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 2)))
			await _frames(1)
		from = to
	workshop.release()
	workshop.unlock()
	workshop.board.flush()
	for i in 30:
		if not workshop.offcuts.is_empty():
			break
		await _frames(1)
	if workshop.offcuts.is_empty():
		push_error("offcut physics: the board did not come apart")
		get_tree().quit(1)
		return

	var body: RigidBody3D = workshop.offcuts.back().body
	var start_xf: Transform3D = workshop.offcuts.back().spawn
	var worst_sink := -INF
	var step_ms := 0.0
	var trace := user_arg("--trace", "") != ""
	if trace:
		for key in ["physics/jolt_physics_3d/simulation/penetration_slop",
				"physics/jolt_physics_3d/simulation/speculative_contact_distance",
				"physics/3d/solver/contact_max_allowed_penetration"]:
			print("  ", key, " = ", ProjectSettings.get_setting(key))
		print("  spawn: sink %.2f mm, velocity %s" % [_sink(body), body.linear_velocity])
	for i in 60:
		await get_tree().physics_frame
		worst_sink = maxf(worst_sink, _sink(body))
		step_ms += Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0
		if trace and i < 12:
			print("  step %d: sink %.2f mm, y %.4f, v %s, w %s" % [i, _sink(body), body.global_position.y,
					body.linear_velocity, body.angular_velocity])
	var sink := _sink(body)
	var relative := start_xf.basis.inverse() * body.global_transform.basis
	var tilt := rad_to_deg(acos(clampf((relative.x.x + relative.y.y + relative.z.z - 1.0) * 0.5, -1.0, 1.0)))
	var moved := body.global_position - start_xf.origin
	var slid := Vector2(moved.x, moved.z).length() * 1000.0
	var engine: String = ProjectSettings.get_setting("physics/3d/physics_engine", "DEFAULT")
	var shape: String = "box" if body.get_child(body.get_child_count() - 1).shape is BoxShape3D else "hull"
	print("offcut physics: engine %s, %s collider; the offcut (%.0f g) sinks %.2f mm at rest (%.2f at worst), tilts %.1f degrees, slides %.1f mm; physics %.3f ms a step" % [
			engine, shape, body.mass * 1000.0, sink, worst_sink, tilt, slid, step_ms / 60.0])
	if user_arg("--report-only", "") == "" and (sink > MAX_SINK or tilt > MAX_TILT or slid < SLIDE[0] or slid > SLIDE[1]):
		push_error("offcut physics: the offcut should rest on the bench upright, slid a little off the kerf")
	get_tree().quit()


## How far (mm) the offcut's lowest collider point lies below the bench top (y = 0).
func _sink(body: RigidBody3D) -> float:
	var lowest := INF
	for child in body.get_children():
		if child is CollisionShape3D:
			var xf: Transform3D = body.global_transform * child.transform
			if child.shape is ConvexPolygonShape3D:
				for p in child.shape.points:
					lowest = minf(lowest, (xf * p).y)
			elif child.shape is BoxShape3D:
				var h: Vector3 = child.shape.size * 0.5
				for c in 8:
					var corner := Vector3(h.x if c & 1 else -h.x, h.y if c & 2 else -h.y, h.z if c & 4 else -h.z)
					lowest = minf(lowest, (xf * corner).y)
	return -lowest * 1000.0


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame
