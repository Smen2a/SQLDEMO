extends "res://tests/harness.gd"

## Plans a chisel stroke with the right button held and makes it with a left-drag, checking
## each step: the plan (its depth, drawn hatched on the board, the section inset showing)
## with the tool in hand kept out of the main view; the wheel deepening it and Shift+wheel
## tilting the chisel; the tool fading in once it acts; the cut it leaves as deep as
## planned; the next pass planned from the same spot while the right button stays down;
## everything dropped when it comes up. Then the camera: middle-drag orbits, Shift+middle-
## drag pans, the right button leaves it alone. When rendering, saves the main view and the
## inset while planning (--out=<dir>).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("tool planning: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	var cam: Camera3D = workshop.camera
	var chisel = workshop.tools["chisel"]
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "depth", 1.0)
	await _frames(8)

	# Plan: locked 30 mm in from the left end, pushed 40 mm to the right.
	var start := Vector3(-0.03, 0.025, 0.0)
	var end := Vector3(0.01, 0.025, 0.0)
	workshop.hover_screen(cam.unproject_position(start))
	await _frames(2)
	workshop.lock(cam.unproject_position(start))
	workshop.aim(cam.unproject_position(end))
	var plan: Dictionary = workshop.get_plan()
	_check(workshop.is_planning(), "a stroke is planned")
	_check(absf(plan.get("depth", 0.0) - 1.0) < 0.01, "the plan goes 1.0 mm deep (%.3f)" % plan.get("depth", 0.0))
	_check(absf(plan.get("length", 0.0) - 40.0) < 2.0, "the plan runs 40 mm (%.1f)" % plan.get("length", 0.0))
	_check(workshop.board.get_stats().get("planned_edits", 0) >= 2, "the plan is drawn on the board")
	_check(chisel.layers == workshop.LAYER_INSET and chisel.opacity == 0.0, "the chisel is kept out of the main view")
	await _frames(1)
	_check(workshop._inset.visible, "the section inset shows")
	_check(not cam.wheel_zoom, "the wheel is the plan's")
	workshop.adjust(3, false)
	workshop.adjust(2, true)
	plan = workshop.get_plan()
	_check(absf(plan.get("depth", 0.0) - 1.3) < 0.01, "three notches deepen it to 1.3 mm (%.3f)" % plan.get("depth", 0.0))
	_check(absf(workshop.settings.chisel.angle - 24.0) < 0.01, "two Shift notches tilt the chisel to 24 degrees")
	await _frames(3)
	var out := user_arg("--out", "")
	if out != "" and DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		DirAccess.make_dir_recursive_absolute(out)
		get_viewport().get_texture().get_image().save_png(out.path_join("tool_planning_main.png"))
		workshop._inset._viewport.get_texture().get_image().save_png(out.path_join("tool_planning_inset.png"))

	# Act: the chisel appears and follows the plan; the right button can come up meanwhile.
	workshop.press(cam.unproject_position(start))
	_check(workshop.is_engaged(), "the chisel sets to work")
	await get_tree().create_timer(0.25).timeout
	_check(chisel.opacity == 1.0 and (chisel.layers & workshop.LAYER_MAIN) != 0, "the chisel faded into the main view")
	for k in 12:
		workshop.drag_screen(cam.unproject_position(start.lerp(end + Vector3(0.02, 0, 0), float(k + 1) / 12.0)))
		await _frames(1)
	workshop.release()
	_check(workshop.is_planning(), "with the right button still down, the next pass is planned from the same spot")
	workshop.unlock()
	_check(not workshop.is_planning() and workshop.board.get_stats().get("planned_edits", 0) == 0,
			"letting go of the right button drops the plan")
	workshop.board.flush()
	await get_tree().create_timer(0.25).timeout
	_check(chisel.opacity == 0.0 and chisel.layers == workshop.LAYER_INSET, "the chisel faded out of the main view")
	_check(not workshop._inset.visible and cam.wheel_zoom, "the inset hides and the wheel zooms again")

	# The cut is as deep as planned, and ends where the plan did (the drag went 20 mm further).
	var middle := _depth_at(Vector3(-0.005, 0.0, 0.0))
	var beyond := _depth_at(Vector3(0.02, 0.0, 0.0))
	print("tool planning: planned 1.30 mm, cut %.2f mm; %.2f mm beyond the plan's end" % [middle, beyond])
	_check(absf(middle - 1.3) < 0.05, "the cut is as deep as planned")
	_check(beyond < 0.05, "the stroke stopped at the plan's end")

	# The camera: middle-drag orbits, Shift+middle-drag pans, the right button leaves it be.
	var yaw: float = cam.yaw
	var target: Vector3 = cam.target
	_mouse(cam, MOUSE_BUTTON_RIGHT, false)
	_check(cam.yaw == yaw and cam.target == target, "a right-drag does not move the camera")
	_mouse(cam, MOUSE_BUTTON_MIDDLE, false)
	_check(cam.yaw != yaw and cam.target == target, "a middle-drag orbits")
	yaw = cam.yaw
	_mouse(cam, MOUSE_BUTTON_MIDDLE, true)
	_check(cam.yaw == yaw and cam.target != target, "a Shift+middle-drag pans")
	print("tool planning: %s" % ("every step as planned" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The cut's depth (mm) below the board's top face at a point on it (world, y ignored).
func _depth_at(at: Vector3) -> float:
	var hit: Dictionary = workshop.board.raycast(Vector3(at.x, 0.1, at.z), Vector3.DOWN, 1.0)
	return 0.0 if hit.is_empty() else (0.025 - hit.position.y) * 1000.0


## A drag of `button` 40 pixels across, straight to the camera.
func _mouse(cam: Camera3D, button: MouseButton, shift: bool) -> void:
	var press := InputEventMouseButton.new()
	press.button_index = button
	press.pressed = true
	press.shift_pressed = shift
	cam._unhandled_input(press)
	var motion := InputEventMouseMotion.new()
	motion.relative = Vector2(40, 0)
	cam._unhandled_input(motion)
	var lift := press.duplicate()
	lift.pressed = false
	cam._unhandled_input(lift)


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("tool planning: " + what)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame
