extends "res://tests/harness.gd"

## Plans strokes with the right button held and makes them with the left, checking each
## step against what the wood allows (core tools/cutting.h):
## - a bench chisel in the middle of the ash board: the plan (as deep as asked, while a hand
##   can push it; drawn hatched) with the chisel out of sight; the wheel deepening it;
##   tilted below its bevel it skates and plans nothing; Ctrl+wheel steps it by hundredths;
##   made, it fades in and cuts as deep as planned, and stops at the plan's end;
## - flicked 50 mm in one go, the chisel follows at its working speed (it takes over a second
##   to get there), and pushes a loose block lying in its way ahead of it, never riding under;
## - uphill, against the grain, the plan says so;
## - chopped (C), a click is a mallet blow: a slit about 2.4 mm deep;
## - a #7 gouge goes a millimetre deep there, where the chisel could not; two passes of it
##   cut a channel nearly 2 mm deep, and a 6 mm chisel's plan along it stops at the channel's
##   end (blocked by the step), and says so;
## - a spokeshave takes an even 0.1 mm shaving; a rasp worked back and forth takes what its
##   plan says, stroke by stroke;
## - without a plan, a left-drag uses the tool directly: a chisel goes the way it is
##   dragged, as deep as the wood lets it and no further than the drag, nothing hatched but
##   the line by the pointer saying what it comes to; a click without a drag does nothing;
##   held up to chop, a click strikes a blow; the sanding block rubs at once;
## - the camera: middle-drag orbits, Shift+middle-drag pans, the right button leaves it be.
## When rendering, saves the view while planning (--out=<dir>).

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
	workshop.set_setting("chisel", "depth", 0.3)
	await _frames(8)

	# Plan: locked 30 mm in from the left end, pushed 40 mm to the right (downhill).
	var start := Vector3(-0.03, 0.025, 0.0)
	var end := Vector3(0.01, 0.025, 0.0)
	var plan := await _plan(start, end)
	_check(workshop.is_planning(), "a stroke is planned")
	_check(absf(plan.get("depth", 0.0) - 0.3) < 0.01, "the plan goes 0.3 mm deep (%.3f)" % plan.get("depth", 0.0))
	_check(absf(plan.get("length", 0.0) - 40.0) < 2.0, "the plan runs 40 mm (%.1f)" % plan.get("length", 0.0))
	_check(plan.get("slope", 0) == 1 and plan.get("grain", 1.0) < 0.1, "along the grain, downhill")
	_check(plan.get("force", 0.0) > 0.0 and plan.get("available", 0.0) == 200.0, "the force it takes, of a hand's 200 N")
	_check(workshop.board.get_stats().get("planned_edits", 0) >= 2, "the plan is drawn on the board")
	_check(not chisel.visible and chisel.opacity == 0.0, "the chisel is out of sight")
	_check(not cam.wheel_zoom, "the wheel is the plan's")
	_check(workshop._ui.plan_text().contains(" N"), "the line by the pointer tells the force")
	workshop.adjust(2, false)
	plan = workshop.get_plan()
	_check(absf(plan.get("depth", 0.0) - 0.4) < 0.01, "two notches deepen it to 0.4 mm (%.3f)" % plan.get("depth", 0.0))
	workshop.adjust(-10, true)
	plan = workshop.get_plan()
	_check("skates" in plan.get("warnings", PackedStringArray()) and plan.get("edits", -1) == 0,
			"tilted to 20 degrees, below its bevel, it skates")
	workshop.adjust(10, true)
	plan = workshop.get_plan()
	_check(absf(plan.get("depth", 0.0) - 0.4) < 0.01 and plan.get("warnings", PackedStringArray()).is_empty(),
			"tipped back to 30 degrees it bites")
	workshop.adjust(-3, false, true)
	_check(is_equal_approx(workshop.settings.chisel.depth, 0.37), "Ctrl+wheel: three hundredths shallower (%.3f)"
			% workshop.settings.chisel.depth)
	workshop.adjust(3, false, true)
	await _frames(3)
	var out := user_arg("--out", "")
	if out != "" and DisplayServer.get_name() != "headless":
		await RenderingServer.frame_post_draw
		DirAccess.make_dir_recursive_absolute(out)
		get_viewport().get_texture().get_image().save_png(out.path_join("tool_planning.png"))

	# Act: the chisel appears and follows the plan; the right button can come up meanwhile.
	workshop.press(cam.unproject_position(start))
	_check(workshop.is_engaged(), "the chisel sets to work")
	await get_tree().create_timer(0.25).timeout
	_check(chisel.opacity == 1.0 and chisel.visible, "the chisel faded in")
	for k in 12:
		workshop.drag_screen(cam.unproject_position(start.lerp(end + Vector3(0.02, 0, 0), float(k + 1) / 12.0)))
		await _frames(1)
	await _catch_up()
	workshop.release()
	_check(workshop.is_planning(), "with the right button still down, the next pass is planned from the same spot")
	workshop.unlock()
	_check(not workshop.is_planning() and workshop.board.get_stats().get("planned_edits", 0) == 0,
			"letting go of the right button drops the plan")
	workshop.board.flush()
	await get_tree().create_timer(0.25).timeout
	_check(chisel.opacity == 0.0 and not chisel.visible, "the chisel faded out")
	_check(cam.wheel_zoom, "the wheel zooms again")
	var middle := _depth_at(Vector3(-0.005, 0.0, 0.0))
	var beyond := _depth_at(Vector3(0.02, 0.0, 0.0))
	print("tool planning: chisel planned 0.40 mm, cut %.2f mm; %.2f mm beyond the plan's end" % [middle, beyond])
	_check(absf(middle - 0.4) < 0.03, "the cut is as deep as planned")
	_check(beyond < 0.03, "the stroke stopped at the plan's end")

	# A flick: dragged 50 mm in one go, the chisel follows at its working speed. A loose block
	# lies in its way, on the board: the blade pushes it aside.
	var flick_from := Vector3(0.0, 0.025, 0.015)
	var flick_to := Vector3(0.05, 0.025, 0.015)
	plan = await _plan(flick_from, flick_to)
	workshop._update_board_collider(true) # (for the block to rest on)
	var block := RigidBody3D.new()
	# As an offcut: sliding on the board, 5 g to the solver.
	block.mass = 0.005
	var surface := PhysicsMaterial.new()
	surface.friction = 0.5
	block.physics_material_override = surface
	var block_shape := CollisionShape3D.new()
	block_shape.shape = BoxShape3D.new()
	block_shape.shape.size = Vector3(0.016, 0.008, 0.016)
	block_shape.shape.margin = workshop.SHAPE_MARGIN
	block.add_child(block_shape)
	add_child(block)
	var block_at := Vector3(0.03, 0.025 + 0.004 + 0.0002, 0.015)
	block.global_position = block_at
	await _physics(10)
	block_at = block.global_position
	var speed: float = workshop.working_speed()
	workshop.press(cam.unproject_position(flick_from))
	workshop.drag_screen(cam.unproject_position(flick_to + Vector3(0.01, 0, 0)))
	await _frames(1)
	var early: float = workshop._progress
	_check(early < 10.0, "a flick does not carry it there at once (%.1f mm after a frame)" % early)
	var took := get_process_delta_time()
	took += await _catch_up()
	workshop.release()
	workshop.unlock()
	workshop.board.flush()
	await _physics(40)
	var pushed := block.global_position - block_at
	print("tool planning: a flick of 50 mm took %.2f s at %.0f mm/s; the loose block went %.1f mm along, %.1f mm up"
			% [took, speed, pushed.x * 1000.0, pushed.y * 1000.0])
	_check(took > 50.0 / (workshop.WORKING_SPEED.chisel * workshop.pace) - 0.05 and took < 50.0 / speed * 1.3 + 0.3,
			"it got there at its working speed")
	_check(pushed.length() > 0.005, "the loose block was pushed aside (%.1f mm)" % (pushed.length() * 1000.0))
	# Ahead of the edge to the stroke's end (its back edge riding a little onto the blade),
	# not left behind with the cut run under it.
	_check(block.global_position.x - 0.008 > flick_to.x - 0.003,
			"the block's back is ahead of where the edge stopped (%.1f mm)" % ((block.global_position.x - 0.008) * 1000.0))
	block.queue_free()

	# Uphill: pushed the other way along the grain.
	plan = await _plan(Vector3(0.03, 0.025, 0.025), Vector3(-0.01, 0.025, 0.025))
	_check(plan.get("slope", 0) == -1, "pushed the other way it goes uphill, against the grain")
	workshop.unlock()

	# Chop: straight up, a click is a mallet blow.
	workshop.set_setting("chisel", "angle", 90.0)
	var chop := Vector3(0.04, 0.025, -0.02)
	plan = await _plan(chop, chop + Vector3(0.01, 0, 0))
	_check(workshop.is_chopping() and plan.get("chop", false), "held straight up, it chops")
	var blow: float = plan.get("blow", 0.0)
	_check(blow > 2.0 and blow < 3.0, "a blow across the grain goes 2-3 mm (%.2f)" % blow)
	_check("slit only" in plan.get("warnings", PackedStringArray()), "in the middle of a face, only a slit")
	workshop.press(cam.unproject_position(chop))
	_check(workshop.is_planning() and not workshop.is_engaged(), "the blow is struck at once, and the next planned")
	workshop.unlock()
	workshop.board.flush()
	var slit := _depth_at(chop + Vector3(0.0003, 0, 0))
	print("tool planning: a chop %.2f mm, the slit %.2f mm deep" % [blow, slit])
	_check(absf(slit - blow) < 0.1, "the slit is as deep as the blow")
	workshop.set_setting("chisel", "angle", 30.0)

	# A #7 gouge, corners out, a millimetre deep where the chisel stays shallow.
	workshop.select_tool("gouge")
	workshop.set_setting("gouge", "depth", 1.0)
	await _frames(4)
	plan = await _plan(Vector3(-0.03, 0.025, -0.03), Vector3(0.0, 0.025, -0.03))
	var gouge_depth: float = plan.get("depth", 0.0)
	workshop.unlock()
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "depth", 1.0)
	await _frames(4)
	plan = await _plan(Vector3(-0.03, 0.025, -0.03), Vector3(0.0, 0.025, -0.03))
	var chisel_depth: float = plan.get("depth", 0.0)
	workshop.unlock()
	print("tool planning: asked 1 mm, the #7 gouge %.2f mm, the bench chisel %.2f mm" % [gouge_depth, chisel_depth])
	_check(absf(gouge_depth - 1.0) < 0.01 and chisel_depth < 0.6, "the gouge goes deeper than the chisel")

	# A channel with a step at its end: two passes of the gouge, dived in steeply (35 degrees)
	# and on to the same end, the second deepening the first. A 6 mm chisel pared along its
	# floor stops at the step, and says why.
	workshop.select_tool("gouge")
	workshop.set_setting("gouge", "depth", 2.0)
	workshop.set_setting("gouge", "angle", 35.0)
	await _frames(4)
	var channel_end := Vector3(-0.04, 0.025, -0.015)
	var floor := 0.025
	for from_x in [-0.075, -0.066]:
		var channel_from := Vector3(from_x, floor, -0.015)
		plan = await _plan(channel_from, Vector3(channel_end.x, floor, channel_end.z))
		var pass_depth: float = plan.get("depth", 0.0)
		_check(pass_depth > 0.5 and plan.get("stop_at", -1.0) < 0.0,
				"a gouge pass along the channel goes %.2f mm deeper, to its end" % pass_depth)
		workshop.press(cam.unproject_position(channel_from))
		for k in 4:
			workshop.drag_screen(cam.unproject_position(channel_from.lerp(channel_end, float(k + 1) / 4.0)))
			await _frames(1)
		await _catch_up()
		workshop.release()
		workshop.unlock()
		workshop.board.flush()
		floor -= pass_depth * 0.001
	workshop.set_setting("gouge", "depth", 1.0)
	workshop.set_setting("gouge", "angle", 30.0)
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "variant", "bench_6")
	workshop.set_setting("chisel", "depth", 0.2)
	await _frames(4)
	# (Aimed at the channel's floor.)
	plan = await _plan(Vector3(-0.056, floor, -0.015), Vector3(-0.03, floor, -0.015))
	var stop_at: float = plan.get("stop_at", -1.0)
	var line: String = workshop._ui.plan_text()
	print("tool planning: a channel %.2f mm deep; the chisel in it stops at %.1f mm (%s, %.2f mm): %s"
			% [(0.025 - floor) * 1000.0, stop_at, plan.get("stop", ""), plan.get("wall", 0.0), line.replace("\n", " / ")])
	_check(plan.get("stop", "") == "blocked" and absf(stop_at - 16.0) < 1.5,
			"the chisel's plan stops at the channel's end, blocked (%.1f mm)" % stop_at)
	_check(plan.get("wall", 0.0) > 1.0, "by the step (%.2f mm)" % plan.get("wall", 0.0))
	_check(line.contains("stops at 16 mm: blocked"), "the line by the pointer says where it stops, and why")
	workshop.unlock()
	workshop.set_setting("chisel", "variant", "bench_12")

	# A spokeshave on the flat top: its sole sets an even 0.1 mm shaving from the start.
	workshop.select_tool("spokeshave")
	await _frames(4)
	var shave_from := Vector3(-0.06, 0.025, 0.035)
	var shave_to := Vector3(-0.02, 0.025, 0.035)
	plan = await _plan(shave_from, shave_to)
	_check(absf(plan.get("depth", 0.0) - 0.1) < 0.005 and not ("shallow" in plan.get("warnings", PackedStringArray())),
			"the spokeshave plans an even 0.1 mm shaving (%.3f)" % plan.get("depth", 0.0))
	workshop.press(cam.unproject_position(shave_from))
	for k in 8:
		workshop.drag_screen(cam.unproject_position(shave_from.lerp(shave_to, float(k + 1) / 8.0)))
		await _frames(1)
	await _catch_up()
	workshop.release()
	workshop.unlock()
	workshop.board.flush()
	var shaved := _depth_at(Vector3(-0.04, 0.0, 0.035))
	print("tool planning: the spokeshave took %.3f mm" % shaved)
	_check(absf(shaved - 0.1) < 0.02, "the spokeshave took its shaving")

	# A cabinet rasp worked ten strokes along 40 mm of ash: five times one stroke's plan.
	workshop.select_tool("rasp")
	await _frames(4)
	var rasp_from := Vector3(0.02, 0.025, 0.035)
	var rasp_to := Vector3(0.06, 0.025, 0.035)
	plan = await _plan(rasp_from, rasp_to)
	var per_stroke: float = plan.get("depth", 0.0)
	_check(per_stroke > 0.01 and per_stroke < 0.05, "a rasp stroke there and back takes a few hundredths (%.3f)" % per_stroke)
	workshop.press(cam.unproject_position(rasp_from))
	for k in 10:
		workshop.drag_screen(cam.unproject_position(rasp_to if k % 2 == 0 else rasp_from))
		await _frames(1)
	workshop.release()
	workshop.unlock()
	workshop.board.flush()
	var rasped := _depth_at(Vector3(0.04, 0.0, 0.035))
	print("tool planning: ten rasp strokes took %.3f mm (%.3f a stroke there and back)" % [rasped, per_stroke])
	_check(absf(rasped - 5.0 * per_stroke) < 0.02, "the rasp took what its plan said, stroke by stroke")

	# Without a plan: a left-drag uses the chisel directly, the way the drag goes.
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "depth", 0.3)
	await _frames(4)
	var direct_from := Vector3(-0.07, 0.025, -0.03)
	var direct_to := Vector3(-0.03, 0.025, -0.03)
	var steps: int = workshop.board.get_stats().get("steps", 0)
	workshop.hover_screen(cam.unproject_position(direct_from))
	await _frames(2)
	workshop.press(cam.unproject_position(direct_from))
	workshop.release()
	await _frames(1)
	_check(not workshop.is_engaged() and workshop.board.get_stats().get("steps", 0) == steps,
			"a click without a drag makes nothing")
	workshop.press(cam.unproject_position(direct_from))
	workshop.drag_screen(cam.unproject_position(direct_from + Vector3(0.001, 0, 0)))
	_check(not workshop.is_engaged() and not workshop.is_planning(), "it waits for the drag to show its way")
	var began := Time.get_ticks_usec()
	workshop.drag_screen(cam.unproject_position(direct_from + Vector3(0.004, 0, 0)))
	var begin_ms := (Time.get_ticks_usec() - began) / 1000.0
	_check(workshop.is_engaged(), "then it sets to work, unplanned")
	plan = workshop.get_plan()
	_check(plan.get("direct", false) and absf(plan.get("depth", 0.0) - 0.3) < 0.01,
			"it says how deep the wood lets it go (%.3f)" % plan.get("depth", 0.0))
	_check(workshop.board.get_stats().get("planned_edits", 0) == 0, "nothing is hatched")
	line = workshop._ui.plan_text()
	_check(line.contains(" N") and not line.contains("300 mm"), "the line by the pointer tells the force: " + line)
	for k in 10:
		workshop.drag_screen(cam.unproject_position(direct_from.lerp(direct_to, float(k + 1) / 10.0)))
		await _frames(1)
	await _catch_up()
	workshop.release()
	workshop.board.flush()
	var direct_cut := _depth_at(Vector3(-0.05, 0.0, -0.03))
	var past_drag := _depth_at(direct_to + Vector3(0.01, 0, 0))
	print("tool planning: an unplanned chisel stroke began in %.1f ms, cut %.2f mm; %.2f mm past where the drag ended"
			% [begin_ms, direct_cut, past_drag])
	_check(absf(direct_cut - 0.3) < 0.03, "it cut as deep as it said")
	_check(past_drag < 0.03, "and stopped where the drag did")
	_check(workshop.board.get_stats().get("steps", 0) == steps + 1 and workshop.get_plan().is_empty(),
			"one undo step, and nothing left planned")

	# Held up to chop, a click strikes one blow.
	workshop.set_setting("chisel", "angle", 90.0)
	var direct_chop := Vector3(0.065, 0.025, 0.005)
	workshop.hover_screen(cam.unproject_position(direct_chop))
	await _frames(2)
	workshop.press(cam.unproject_position(direct_chop))
	_check(not workshop.is_engaged() and not workshop.is_planning(), "a click chops at once")
	workshop.board.flush()
	var deepest := 0.0
	for offset in [Vector3.ZERO, Vector3(0.0003, 0, 0), Vector3(-0.0003, 0, 0), Vector3(0, 0, 0.0003), Vector3(0, 0, -0.0003)]:
		deepest = maxf(deepest, _depth_at(direct_chop + offset))
	print("tool planning: an unplanned chop, the slit %.2f mm deep" % deepest)
	_check(deepest > 1.0 and workshop.board.get_stats().get("steps", 0) == steps + 2, "one blow, a slit")
	workshop.set_setting("chisel", "angle", 30.0)

	# The sanding block rubs as soon as it is pressed, wherever it is dragged.
	workshop.select_tool("sanding_block")
	await _frames(4)
	var rub := Vector3(0.0, 0.025, -0.03)
	workshop.hover_screen(cam.unproject_position(rub))
	await _frames(2)
	var edits: int = workshop.board.get_stats().get("edits", 0)
	workshop.press(cam.unproject_position(rub))
	_check(workshop.is_engaged(), "the sanding block sets to work at once")
	for k in 8:
		workshop.drag_screen(cam.unproject_position(rub + Vector3(0.02 * sin(k), 0, 0.01 * cos(k))))
		await _frames(1)
	workshop.release()
	workshop.board.flush()
	_check(workshop.board.get_stats().get("steps", 0) == steps + 3 and workshop.board.get_stats().get("edits", 0) > edits,
			"it rubbed the board")

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


## Locks a stroke in at `from` and aims it at `to` (world), once the board is idle, and waits
## for its plan.
func _plan(from: Vector3, to: Vector3) -> Dictionary:
	var cam: Camera3D = workshop.camera
	workshop.board.flush()
	# And until the cuts so far are refined: the pointer's rays read the refined surface, and
	# the lock does not depend on how far refining has got.
	for i in 1200:
		if not workshop.board.is_busy() and not workshop.board.get_stats().get("refine_pending", false):
			break
		await _frames(1)
	workshop.hover_screen(cam.unproject_position(from))
	await _frames(2)
	workshop.lock(cam.unproject_position(from))
	workshop.aim(cam.unproject_position(to))
	await _frames(1)
	return workshop.get_plan()


## Waits (a minute at most) for the tool in hand to catch up with where it was dragged, and
## returns how long it took. In game time: where frames take seconds (rendered in software)
## the engine slows the game down, and the tool with it.
func _catch_up() -> float:
	var waited := 0.0
	while workshop.lagging() and waited < 60.0:
		await _frames(1)
		waited += get_process_delta_time()
	return waited


func _physics(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


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
