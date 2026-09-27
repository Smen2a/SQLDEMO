extends "res://tests/harness.gd"

## Laying out (workshop/layout.gd), driven through the workshop's own pointer methods:
## - the marking gauge set 6 mm, hovered near the board's front edge on its top: a click
##   scribes a line the whole length of the board, 6 mm in from that edge: a groove about
##   0.3 mm deep found there (and none a few millimetres off it), and a mark on the piece;
## - the knife and square: a click scribes a line across the board, square to the edge;
##   a drag scribes only as far as it goes;
## - undo takes the lines back, groove and mark, and redo brings them back.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "300"))).timeout.connect(func():
		push_error("layout lines: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	workshop.enter_work(false) # (at the bench, over the board in the vise)
	await _frames(3)
	var layout = workshop.layout
	workshop.select_tool("layout")
	workshop.set_setting("layout", "variant", "gauge")
	workshop.set_setting("layout", "distance", 6.0)
	await _frames(2)

	# The gauge, its fence on the front face: a line 6 mm in along the top.
	var top := _depth(Vector3(0.0, 0.03, 0.03))
	_click(Vector3(0.0, 0.025, 0.046))
	await _settle()
	var marks: Array = layout.marks_of(workshop.clamped)
	_check(marks.size() == 1 and marks[0].kind == "gauge" and is_equal_approx(marks[0].distance, 6.0),
			"a click with the gauge scribes a line, and marks it (%s)" % [marks])
	var groove := _deepest(Vector3(0.0, 0.03, 0.041), Vector3(0.0, 0.03, 0.047))
	print("layout lines: the gauge line, %.3f mm deep at %.2f mm in from the front edge (the top %.3f mm down)" % [
			groove.depth - top, (0.05 - groove.at.z) * 1000.0, top])
	_check(groove.depth - top > 0.2 and absf((0.05 - groove.at.z) * 1000.0 - 6.0) < 0.15,
			"a groove 6 mm in from the edge, a few tenths deep")
	_check(absf(_depth(Vector3(0.0, 0.03, 0.040)) - top) < 0.01, "and none 4 mm off it")
	var along := _depth(Vector3(-0.07, 0.03, groove.at.z)) - top
	_check(along > 0.15, "the whole length of the board (%.3f mm deep near its end)" % along)

	# The knife and square: a line across the board, square to the edge.
	workshop.set_setting("layout", "variant", "knife")
	await _frames(1)
	_click(Vector3(0.02, 0.025, 0.0))
	await _settle()
	var across := _deepest(Vector3(0.017, 0.03, 0.01), Vector3(0.023, 0.03, 0.01))
	print("layout lines: the knife line, %.3f mm deep at x = %.2f mm" % [across.depth - top, across.at.x * 1000.0])
	_check(marks.size() == 2 and marks[1].kind == "knife" and across.depth - top > 0.2 and
			absf(across.at.x - 0.02) < 0.0002, "a click with the knife scribes a line across, square to the edge")
	# A drag: only as far as it goes.
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(Vector3(-0.03, 0.025, 0.02)))
	workshop.press(cam.unproject_position(Vector3(-0.03, 0.025, 0.02)))
	for k in 6:
		workshop.drag_screen(cam.unproject_position(Vector3(-0.03, 0.025, 0.02 - 0.005 * (k + 1))))
		await _frames(1)
	workshop.release()
	await _settle()
	var inside: float = _deepest(Vector3(-0.033, 0.03, 0.0), Vector3(-0.027, 0.03, 0.0)).depth - top
	var beyond: float = _deepest(Vector3(-0.033, 0.03, 0.035), Vector3(-0.027, 0.03, 0.035)).depth - top
	print("layout lines: a knife line dragged 30 mm: %.3f mm deep within it, %.3f beyond" % [inside, beyond])
	_check(marks.size() == 3 and inside > 0.2 and beyond < 0.01, "a drag scribes only as far as it goes")

	# Undo takes the lines back (the grooves and the marks); redo brings them back.
	for i in 3:
		workshop.undo()
		await _settle()
	marks = layout.marks_of(workshop.clamped)
	var gone: float = _deepest(Vector3(0.0, 0.03, 0.041), Vector3(0.0, 0.03, 0.047)).depth - top
	_check(marks.is_empty() and gone < 0.01, "undo takes them back (%d marks, %.3f mm deep)" % [marks.size(), gone])
	workshop.redo()
	await _settle()
	marks = layout.marks_of(workshop.clamped)
	var back: float = _deepest(Vector3(0.0, 0.03, 0.041), Vector3(0.0, 0.03, 0.047)).depth - top
	_check(marks.size() == 1 and marks[0].kind == "gauge" and back > 0.2, "redo brings the gauge line back")

	print("layout lines: %s" % ("the lines are laid out" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A click (no drag) at a point on the board: the whole line.
func _click(at: Vector3) -> void:
	var screen: Vector2 = workshop.camera.unproject_position(at)
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()


## Until the scribed edit has landed.
func _settle() -> void:
	workshop.board.flush()
	await _frames(2)


## How far down (mm) a ray from `from` goes to the board.
func _depth(from: Vector3) -> float:
	var hit: Dictionary = workshop.board.raycast(from, Vector3.DOWN, 0.02)
	return hit.get("distance", INF) * 1000.0


## The deepest point between two points (rays down every 0.05 mm): {"depth" (mm), "at"}.
func _deepest(a: Vector3, b: Vector3) -> Dictionary:
	var best := {"depth": -INF, "at": a}
	var n := int(a.distance_to(b) / 0.00005)
	for i in n + 1:
		var p := a.lerp(b, float(i) / n)
		var d := _depth(p)
		if d > best.depth:
			best = {"depth": d, "at": p}
	return best


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("layout lines: " + what)
