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
	get_tree().create_timer(float(user_arg("--timeout", "600"))).timeout.connect(func():
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

	# --- The lines stop the tools ---------------------------------------------------------

	# A rebate along the front: 8 mm wide (gauged on the top), 1 mm deep (gauged on the front).
	# Pared along it with a 12 mm chisel: its side keeps to the width line, and it goes down to
	# the depth line and no further.
	workshop.set_setting("layout", "variant", "gauge")
	workshop.set_setting("layout", "distance", 8.0)
	_click(Vector3(0.0, 0.025, 0.046))
	await _settle()
	workshop.set_setting("layout", "distance", 1.0)
	_click(Vector3(0.0, 0.022, 0.05)) # (on the front face, near its top edge)
	await _settle()
	marks = layout.marks_of(workshop.clamped)
	_check(marks.size() == 3, "the rebate's two lines marked (%d marks)" % marks.size())
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "variant", "bench_12")
	workshop.set_setting("chisel", "depth", 0.5)
	workshop.set_setting("chisel", "angle", 30.0)
	var passes := 0
	var floor_at := INF
	for i in 5:
		await _pare(Vector3(-0.0785, 0.025, 0.0465), Vector3(0.07, 0.025, 0.0465))
		passes += 1
		var now := _depth(Vector3(0.0, 0.03, 0.046)) - top
		if absf(now - floor_at) < 0.005:
			break
		floor_at = now
	var shoulder := _depth(Vector3(0.0, 0.03, 0.0405)) - top
	var in_rebate := _depth(Vector3(-0.02, 0.03, 0.0465)) - top
	print("layout lines: a rebate 8 x 1 mm pared in %d passes: its floor %.3f mm down, 1.5 mm inside the width line %.3f mm" % [
			passes, in_rebate, shoulder])
	_check(absf(in_rebate - 1.0) < 0.06, "the rebate goes down to its depth line")
	_check(shoulder < 0.01, "and not past its width line")

	# A chamfer along the back: 3 mm gauged on the top and on the back face. The chisel across
	# the corner, its back laid on the plane between the lines: down to it, no further.
	workshop.select_tool("layout")
	workshop.set_setting("layout", "distance", 3.0)
	_click(Vector3(0.0, 0.025, -0.046))
	await _settle()
	# (the back face, near its top edge: out of the view's sight, so scribed through the layout
	# tool's own methods)
	var on_back: Dictionary = layout.line_at(Vector3(0.0, 0.022, -0.05), Vector3.FORWARD)
	layout.scribe(on_back, 0.0, on_back.length)
	await _settle()
	workshop.select_tool("chisel")
	# (Seen from behind, as a joiner would go round: from the front, the chamfer faces away.)
	var orbit = workshop.camera
	orbit.yaw = PI
	orbit._apply()
	var taken := []
	for i in 8:
		var before := _section(0.0)
		await _pare(Vector3(-0.05, 0.025, -0.049), Vector3(0.07, 0.025, -0.049))
		taken.append(_section(0.0) - before)
		if taken.back() < 0.01:
			break
	var mid := _depth(Vector3(0.0, 0.03, -0.0485)) - top    # 1.5 mm in: on the 45 degree plane, 1.5 down
	var beyond_line := _depth(Vector3(0.0, 0.03, -0.0465)) - top # 3.5 mm in: past the line, untouched
	var whole := _section(0.0) # (3 x 3 mm: 4.5 mm^2)
	print("layout lines: a 3 mm chamfer pared in %d passes (%s mm^2 of its section off each, %.2f in all): 1.5 mm in, %.2f mm down; past the line %.3f" % [
			taken.size(), ", ".join(taken.map(func(t): return "%.2f" % t)), whole, mid, beyond_line])
	_check(absf(mid - 1.5) < 0.15 and beyond_line < 0.01 and absf(whole - 4.5) < 0.25 and taken.back() < 0.01,
			"the chamfer goes down to the plane between its lines, and no further")
	orbit.yaw = 0.0
	orbit._apply()

	# A knife line across the top: a chisel pared towards it stops at it; with Alt, it does not.
	workshop.select_tool("layout")
	workshop.set_setting("layout", "variant", "knife")
	_click(Vector3(0.03, 0.025, 0.0))
	await _settle()
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "depth", 0.3)
	await _pare(Vector3(-0.03, 0.025, 0.01), Vector3(0.06, 0.025, 0.01))
	var before_line := _depth(Vector3(0.0285, 0.03, 0.01)) - top
	var after_line := _depth(Vector3(0.0315, 0.03, 0.01)) - top
	print("layout lines: pared towards a knife line: %.3f mm deep 1.5 mm before it, %.3f mm 1.5 mm past it" % [
			before_line, after_line])
	_check(before_line > 0.2 and after_line < 0.01, "a knife line stops the chisel, square")
	await _pare(Vector3(-0.03, 0.025, -0.01), Vector3(0.06, 0.025, -0.01), true)
	var freely := _depth(Vector3(0.04, 0.03, -0.01)) - top
	_check(freely > 0.2, "with Alt the chisel crosses it (%.3f mm deep past it)" % freely)

	# The saw down to a gauge line on the face it saws into: 5 mm down the board's end, gauged
	# from the top; sawn along the grain into the end (a tenon's cheek), the kerf stops there.
	workshop.select_tool("layout")
	workshop.set_setting("layout", "variant", "gauge")
	workshop.set_setting("layout", "distance", 5.0)
	# (on the end, near its top edge: out of the view's sight, so scribed through the layout
	# tool's own methods)
	var on_end: Dictionary = layout.line_at(Vector3(0.08, 0.022, -0.02), Vector3.RIGHT)
	layout.scribe(on_end, 0.0, on_end.length)
	await _settle()
	workshop.select_tool("saw")
	workshop.set_setting("saw", "feed", 0.2)
	await _saw(Vector3(0.068, 0.025, -0.02), 30)
	var kerf := _depth(Vector3(0.068, 0.03, -0.02)) - top
	print("layout lines: sawn down to a gauge line 5 mm down: the kerf %.2f mm deep" % kerf)
	_check(absf(kerf - 5.0) < 0.2, "the saw stops at the depth line")

	print("layout lines: %s" % ("the lines are laid out, and the tools keep to them" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The area (mm^2) taken off the board's back top corner at `x`: rays down every 0.25 mm over
## the 4 mm next to the back edge (0: square).
func _section(x: float) -> float:
	var area := 0.0
	for i in 16:
		area += (_depth(Vector3(x, 0.03, -0.05 + 0.00025 * (i + 0.5))) - 5.0) * 0.25
	return area


## A chisel pared from `from` to `to` (on the board's top), direct, followed at its working
## speed; `free`: with Alt.
func _pare(from: Vector3, to: Vector3, free := false) -> void:
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(from))
	await _frames(2)
	workshop.press(cam.unproject_position(from), free)
	for k in 16:
		workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 16)))
		await _frames(1)
	await catch_up(workshop)
	workshop.release()
	await _settle()


## The saw along the board's length (x) at `at`, `strokes` strokes of 20 mm.
func _saw(at: Vector3, strokes: int) -> void:
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(at))
	await _frames(2)
	workshop.press(cam.unproject_position(at))
	var from := at
	for i in strokes:
		var to := at + Vector3(-0.01 if i % 2 == 0 else 0.01, 0.0, 0.0)
		for k in 2:
			workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 2)))
			await _frames(1)
		await catch_up(workshop)
		from = to
	workshop.release()
	await _settle()


## A click (no drag) at a point on the board: the whole line.
func _click(at: Vector3) -> void:
	var screen: Vector2 = workshop.camera.unproject_position(at)
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()


## Until the edit has landed and the board is idle (refined, looked over for islands): rays
## locking the next stroke read it as it is.
func _settle() -> void:
	workshop.board.flush()
	for i in 1200:
		if not workshop.board.is_busy() and not workshop.board.get_stats().get("refine_pending", false):
			break
		await _frames(1)
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
