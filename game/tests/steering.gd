extends "res://tests/harness.gd"

## Steering a chisel as it goes (workshop.gd _steer, SdfBody.steer_stroke), driven through
## the workshop's own pointer methods and input handler:
## - a drag round a quarter circle of 40 mm radius: the edge follows it, and the cut's middle
##   stays within 0.5 mm of the arc, 0.3 mm deep, in few edits;
## - the guiding hand mid-stroke: along the board held on its bevel (level), the handle
##   raised 6 degrees (it dives), back on the bevel (level again, deeper), lowered 6 degrees
##   under it (it rises and lifts out).

const Workshop := preload("res://workshop/workshop.tscn")
const MM := 0.001

var workshop
var _failed := false
var _pointer_at := Vector3.ZERO # where _drag_to() left the pointer (world)


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "600"))).timeout.connect(func():
		push_error("steering: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	workshop.enter_work(false) # (at the bench, over the board in the vise)
	await _frames(3)
	var cam: Camera3D = workshop.camera
	var top := _depth(Vector3(0.0, 0.03, 0.0))
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "variant", "bench_6")
	workshop.set_setting("chisel", "depth", 0.3)
	workshop.set_setting("chisel", "angle", 33.0) # (6 degrees past its bevel: in mid-face)

	# A quarter circle: from (-40, 0) heading along x, round to (0, 40) heading to the front.
	var centre := Vector3(-0.04, 0.025, 0.04)
	var radius := 0.04
	var on_arc := func(deg: float) -> Vector3:
		return centre + Vector3(sin(deg_to_rad(deg)), 0.0, -cos(deg_to_rad(deg))) * radius
	var start: Vector3 = on_arc.call(0.0)
	workshop.hover_screen(cam.unproject_position(start))
	await _frames(2)
	workshop.press(cam.unproject_position(start))
	var most := 0
	for i in 100:
		# The pointer a little ahead of where the edge has got to along the arc.
		var deg := minf(float(i) * 1.0 + 8.0, 96.0)
		workshop.drag_screen(cam.unproject_position(on_arc.call(deg)))
		await _frames(2)
		most = maxi(most, workshop.board.get_stats().get("overlay_edits", 0))
	await catch_up(workshop)
	var line: String = workshop._ui.plan_text()
	workshop.release()
	await _settle()
	var worst := 0.0
	for deg in [15.0, 45.0, 75.0]:
		var out := Vector3(sin(deg_to_rad(deg)), 0.0, -cos(deg_to_rad(deg)))
		var first := -1.0
		var last := -1.0
		var r := radius - 0.006
		while r <= radius + 0.006:
			var q: Vector3 = centre + out * r
			if _depth(Vector3(q.x, 0.03, q.z)) - top > 0.15:
				first = r if first < 0.0 else first
				last = r
			r += 0.00005
		var on: Vector3 = on_arc.call(deg)
		var deep := _depth(Vector3(on.x, 0.03, on.z)) - top
		print("steering: at %2.0f° the cut runs %.2f to %.2f mm out (its middle %.2f), %.3f mm deep on the arc" % [
				deg, first / MM, last / MM, 0.5 * (first + last) / MM, deep])
		_check(first > 0.0 and absf(deep - 0.3) < 0.05, "the cut follows the arc at its depth (at %.0f°)" % deg)
		worst = maxf(worst, absf(0.5 * (first + last) - radius) / MM)
	print("steering: round a 40 mm quarter circle, the cut's middle within %.2f mm of it; at most %d edits in the overlay (\"%s\")" % [
			worst, most, line.replace("\n", " / ")])
	_check(worst < 0.5, "the cut's middle within 0.5 mm of the arc")
	_check(most <= 16, "the overlay holds it")

	# The guiding hand mid-stroke, along the back of the board: in mid-face 6 degrees past its
	# bevel (it dives to the depth set, 0.3 mm, and levels), then held on its bevel (level),
	# raised 6 degrees again (it dives), back on its bevel (level, deeper), lowered 6 degrees
	# under it (it rises and lifts out).
	var z := -0.03
	var from := Vector3(-0.07, 0.025, z)
	workshop.hover_screen(cam.unproject_position(from))
	await _frames(2)
	workshop.press(cam.unproject_position(from))
	_pointer_at = from
	await _drag_to(Vector3(-0.056, 0.025, z))
	_pivot(Vector2(0, 24))                         # on its bevel: level
	await _drag_to(Vector3(-0.05, 0.025, z))
	_pivot(Vector2(0, -24))                        # raised 6 degrees: it dives
	await _drag_to(Vector3(-0.045, 0.025, z))
	_pivot(Vector2(0, 24))                         # back on its bevel: level
	await _drag_to(Vector3(-0.03, 0.025, z))
	_pivot(Vector2(0, 24))                         # lowered 6 under it: it lifts out
	await _drag_to(Vector3(0.0, 0.025, z))
	var said: Array = workshop.get_plan().get("warnings", [])
	workshop.release()
	await _settle()
	var at := func(x: float) -> float: return _depth(Vector3(x, 0.03, z)) - top
	var level: float = at.call(-0.052)
	var dived: float = at.call(-0.042)
	var dived_on: float = at.call(-0.033)
	var out: float = at.call(-0.012)
	print("steering: the hand mid-stroke: %.3f mm on the bevel, %.3f after 6° raised for 5 mm, %.3f held on the bevel, %.3f once lowered (%s)" % [
			level, dived, dived_on, out, said])
	_check(absf(level - 0.3) < 0.03, "held on its bevel it runs level")
	_check(dived > level + 0.35 and absf(dived_on - dived) < 0.05, "raised past its bevel it dives; back on it, level")
	_check(out < 0.01 and "lifts out" in said, "lowered under its bevel it lifts out")

	print("steering: %s" % ("the hand steers the edge" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The pointer dragged straight on from where it was to `to`, a millimetre at a time; waits
## for the edge.
func _drag_to(to: Vector3) -> void:
	var cam: Camera3D = workshop.camera
	var n := maxi(int(_pointer_at.distance_to(to) / MM), 1)
	for k in n:
		workshop.drag_screen(cam.unproject_position(_pointer_at.lerp(to, float(k + 1) / n)))
		await _frames(1)
	_pointer_at = to
	await catch_up(workshop)


## The guiding hand, mid-stroke: right button down, the mouse moved, right up.
func _pivot(relative: Vector2) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_RIGHT
	e.pressed = true
	workshop._unhandled_input(e)
	var m := InputEventMouseMotion.new()
	m.relative = relative
	workshop._unhandled_input(m)
	var up := e.duplicate()
	up.pressed = false
	workshop._unhandled_input(up)


func _settle() -> void:
	workshop.board.flush()
	for i in 1200:
		if not workshop.board.is_busy() and not workshop.board.get_stats().get("refine_pending", false):
			break
		await _frames(1)
	await _frames(2)


func _depth(from: Vector3) -> float:
	var hit: Dictionary = workshop.board.raycast(from, Vector3.DOWN, 0.02)
	return hit.get("distance", INF) * 1000.0


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("steering: " + what)
