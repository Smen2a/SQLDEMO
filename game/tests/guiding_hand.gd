extends "res://tests/harness.gd"

## The guiding hand (workshop.gd: begin_pivot, pivot, lean), driven through the workshop's
## own input handler, as the mouse and keyboard would:
## - right-drag pivots a chisel: up raises the handle (0.25 degrees a pixel, Ctrl a fifth of
##   that), right skews its edge, the wheel leans it; paring stays below a chop, a chop above;
## - it pivots a plan as it is made (Space held): tipped below its bevel's clearance the
##   plan skates, and the gauge says the bevel rides;
## - a leaned chisel cuts one side deeper;
## - the rasp's lean is its tilt; the sanding block turns.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "300"))).timeout.connect(func():
		push_error("guiding hand: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	workshop.enter_work(false) # (at the bench, over the board in the vise)
	await _frames(3)
	var cam: Camera3D = workshop.camera
	var mid: Vector2 = cam.unproject_position(Vector3(0.0, 0.025, 0.0))
	workshop.select_tool("chisel")
	workshop.hover_screen(mid)
	var s: Dictionary = workshop.settings.chisel

	# Right-drag: up 40 pixels raises the handle 10 degrees, right 20 skews it 5, two notches of
	# the wheel lean it 5; with Ctrl, a fifth as far.
	_button(MOUSE_BUTTON_RIGHT, true)
	_check(workshop.is_pivoting() and workshop.attitude_shown(), "the right button gives the tool to the guiding hand")
	_move(Vector2(0, -40))
	_move(Vector2(20, 0))
	_button(MOUSE_BUTTON_WHEEL_UP, true)
	_button(MOUSE_BUTTON_WHEEL_UP, true)
	_check(is_equal_approx(s.angle, 40.0) and is_equal_approx(s.skew, 5.0) and is_equal_approx(s.lean, 5.0),
			"up raises the handle, right skews, the wheel leans (%.2f, %.2f, %.2f)" % [s.angle, s.skew, s.lean])
	_move(Vector2(0, 20), true)
	_check(is_equal_approx(s.angle, 39.0), "Ctrl pivots it finely (%.2f)" % s.angle)
	# Paring stays paring: the handle raised as far as it goes stops short of a chop.
	_move(Vector2(0, -400))
	_check(s.angle < workshop.CHOP_ANGLE and not workshop.is_chopping(), "raised all the way it still pares (%.1f)" % s.angle)
	_button(MOUSE_BUTTON_RIGHT, false)
	_check(not workshop.is_pivoting(), "let go, the hand lets the tool go")
	print("guiding hand: right-drag: %s" % workshop._ui.attitude_text())
	# C stands it up to chop; the hand keeps a chop a chop.
	_key(KEY_C, true)
	_button(MOUSE_BUTTON_RIGHT, true)
	_move(Vector2(0, 400))
	_button(MOUSE_BUTTON_RIGHT, false)
	_check(workshop.is_chopping() and is_equal_approx(s.angle, workshop.CHOP_ANGLE), "lowered all the way it still chops (%.1f)" % s.angle)
	_key(KEY_C, true)
	workshop.set_setting("chisel", "angle", 30.0)
	workshop.set_setting("chisel", "skew", 0.0)
	workshop.set_setting("chisel", "lean", 0.0)

	# Space held: the stroke is planned, and the hand pivots the plan as it is made. Mid-face,
	# tipped under its bevel's clearance (a bench chisel's 25 degrees and 2 more), it skates.
	workshop.hover_screen(mid)
	_key(KEY_SPACE, true)
	workshop.aim(cam.unproject_position(Vector3(0.03, 0.025, 0.0)))
	await _frames(2)
	_check(workshop.is_planning(), "Space held plans the stroke")
	var before: Dictionary = workshop.get_plan()
	_button(MOUSE_BUTTON_RIGHT, true)
	_move(Vector2(0, 16)) # 30 -> 26 degrees
	await _frames(2)
	var after: Dictionary = workshop.get_plan()
	var gauge: String = workshop._ui.attitude_text()
	_button(MOUSE_BUTTON_RIGHT, false)
	print("guiding hand: planned at 30°: %s; pivoted to %.0f°: %s (%s)" % [before.get("warnings"), s.angle,
			after.get("warnings"), gauge])
	_check(not "skates" in before.get("warnings", []) and "skates" in after.get("warnings", []),
			"the plan follows the hand: tipped under its bevel it skates")
	_check(workshop.is_planning() and gauge.contains("rides its bevel"), "and the gauge says so")
	_key(KEY_SPACE, false)
	_check(not workshop.is_planning(), "Space let go drops the plan")
	workshop.set_setting("chisel", "angle", 30.0)

	# Leaned 5 degrees, a pass along the top cuts its front side deeper than its back.
	workshop.set_setting("chisel", "depth", 0.3)
	workshop.set_setting("chisel", "lean", 5.0)
	var top := _depth(Vector3(0.0, 0.03, -0.02))
	await _pare(Vector3(-0.074, 0.025, 0.0), Vector3(0.0, 0.025, 0.0))
	var front := _depth(Vector3(-0.04, 0.03, 0.004)) - top
	var back := _depth(Vector3(-0.04, 0.03, -0.004)) - top
	print("guiding hand: leaned 5°, a 0.3 mm pass is %.2f mm deep 4 mm to the front, %.2f mm 4 mm to the back" % [
			front, back])
	# (Its floor turned 5 degrees about the way it goes: 0.3 + 4 tan 5 = 0.65 mm deep 4 mm to the
	# front; 4 mm to the back it is out of the wood.)
	_check(absf(front - (0.3 + 4.0 * tan(deg_to_rad(5.0)))) < 0.05 and back < 0.01,
			"leaned, the chisel's floor is turned with it: one side deeper")
	workshop.set_setting("chisel", "lean", 0.0)

	# The rasp: its lean is its tilt. The sanding block: turned about the face.
	workshop.select_tool("rasp")
	workshop.hover_screen(mid)
	_button(MOUSE_BUTTON_RIGHT, true)
	_button(MOUSE_BUTTON_WHEEL_DOWN, true)
	_button(MOUSE_BUTTON_RIGHT, false)
	_check(is_equal_approx(workshop.settings.rasp.tilt, -5.0), "the wheel tilts the rasp (%.1f)" % workshop.settings.rasp.tilt)
	workshop.select_tool("sanding_block")
	workshop.hover_screen(mid)
	var yaw: float = workshop.yaw
	_button(MOUSE_BUTTON_RIGHT, true)
	_move(Vector2(60, 0))
	_button(MOUSE_BUTTON_RIGHT, false)
	_check(is_equal_approx(rad_to_deg(workshop.yaw - yaw), -15.0), "right-drag turns the block (%.1f°)" % rad_to_deg(workshop.yaw - yaw))

	print("guiding hand: %s" % ("the hand pivots the tools" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A mouse button pressed or let go, through the workshop's input handler, at the pointer.
func _button(index: MouseButton, pressed: bool) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = index
	e.pressed = pressed
	e.position = workshop._pointer
	workshop._unhandled_input(e)


## The mouse moved `relative` pixels (`ctrl`: with Ctrl held).
func _move(relative: Vector2, ctrl := false) -> void:
	var e := InputEventMouseMotion.new()
	e.relative = relative
	e.position = workshop._pointer
	e.ctrl_pressed = ctrl
	workshop._unhandled_input(e)


func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.pressed = pressed
	workshop._unhandled_input(e)


## A chisel pared from `from` to `to` (on the board's top), direct, at its working speed.
func _pare(from: Vector3, to: Vector3) -> void:
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(from))
	await _frames(2)
	workshop.press(cam.unproject_position(from))
	for k in 12:
		workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 12)))
		await _frames(1)
	await catch_up(workshop)
	workshop.release()
	workshop.board.flush()
	for i in 1200:
		if not workshop.board.is_busy():
			break
		await _frames(1)


## How far down (mm) a ray from `from` goes to the board.
func _depth(from: Vector3) -> float:
	var hit: Dictionary = workshop.board.raycast(from, Vector3.DOWN, 0.02)
	return hit.get("distance", INF) * 1000.0


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("guiding hand: " + what)
