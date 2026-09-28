extends "res://tests/harness.gd"

## Plans (workshop/plans.gd), driven through the plan book and the workshop's own pointer
## methods, on an oak mallet-head blank (120 x 70 x 55) in the vise:
## - the plan book lists the mallet's three parts; "Take this sheet" puts the head's in hand;
## - hovered on the blank's top near a corner, the sheet fits; a click draws the head on in
##   pencil: its mortise round the top and the bottom, knifed to length round the blank (12
##   lines, as SdfBody.part_lines places them), and the piece is the mallet's head; the body
##   is untouched (no step);
## - undo straight after takes the pencil off, and the part;
## - pencil lines hold no tool; the knife hovered a millimetre off one takes it, and a click
##   knifes it in (one step), which then holds a chisel;
## - the pencil: a click draws a line across, a drag one along the edge, gauged from it;
## - with "scribe as you lay it on", the sheet is scribed on at once: one step, undone as one.
## Rendered, with --out=<dir>: the sheet hovered (plan_sheet.png), drawn on
## (plan_pencil.png), and the plan book open (plan_book.png).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false
var _out := ""


func _ready() -> void:
	get_tree().create_timer(600.0).timeout.connect(func():
		push_error("plan transfer: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	workshop.set_wood("head_oak")
	var plans = workshop.plans
	var layout = workshop.layout
	_check(plans.plans.has("mallet"), "the mallet's plan is in the book")

	# The plan book: the mallet's parts; the head's sheet taken.
	workshop._ui.open_plans()
	await _frames(2)
	var viewer = workshop._ui._viewer
	_check(workshop._ui.plans_open() and viewer._parts.item_count == 3, "the plan book lists the mallet's 3 parts")
	viewer.show_part("head")
	await _shot("plan_book")
	viewer.take_sheet()
	await _frames(1)
	_check(not workshop._ui.plans_open() and plans.sheet == {"plan": "mallet", "part": "head"} and
			workshop.current == "layout" and workshop.settings.layout.variant == "plan", "the head's sheet taken in hand")

	# At the bench: the sheet over the blank's top, near its left front corner.
	workshop.enter_work(false)
	await _frames(2)
	var board = workshop.board
	var piece: RigidBody3D = workshop.clamped
	var steps: int = board.get_stats().get("steps", 0)
	workshop.hover_screen(_screen(Vector3(-50, -28, 27.5)))
	var placed: Dictionary = layout.placed
	_check(not placed.is_empty() and placed.fits and placed.marks.size() == 12 and placed.later == 0,
			"hovered on the top, the head fits the blank: 12 lines (%s)" % [placed.get("marks", []).size()])
	await _shot("plan_sheet")
	_click(Vector3(-50, -28, 27.5))
	var marks: Array = layout.marks_of(piece)
	var tag: Dictionary = piece.get_meta("part", {})
	_check(marks.size() == 12 and marks.all(func(m): return m.kind == "pencil"), "a click draws it on in pencil")
	_check(tag.get("plan", "") == "mallet" and tag.get("part", "") == "head" and plans.status("mallet").head == 1,
			"and the piece is the mallet's head")
	_check(board.get_stats().get("steps", 0) == steps, "the wood untouched")
	# Where the lines are: the mortise's on the top 40-70 mm from the left end, 29-41 from the
	# front edge (body x -20 to 10, y -6 to 6), and through on the bottom; knifed to length
	# 110 mm from the left end (x = 50) round the blank.
	var expected: Array = workshop.tools["layout"].part_lines(plans.part("mallet", "head"), Vector3(120, 70, 55)).lines
	var placement: Transform3D = tag.get("placement", Transform3D())
	var matched := 0
	for line in expected:
		for m in marks:
			if (m.origin - placement * line.origin).length() < 1e-3 and (m.dir - placement.basis * line.dir).length() < 1e-3:
				matched += 1
				break
	_check(matched == 12, "each line where part_lines puts it, placed (%d of 12)" % matched)
	var on_top := false
	var below := false
	for m in marks:
		if m.feature == "mortise" and absf(m.origin.z - 27.5) < 1e-3 and absf(m.origin.x + 20.0) < 1e-3 and \
				absf(m.origin.y + 6.0) < 1e-3:
			on_top = true
		if m.feature == "mortise" and absf(m.origin.z + 27.5) < 1e-3:
			below = true
	_check(on_top, "the mortise on the top")
	_check(below, "and on the bottom")
	_check(marks.filter(func(m): return m.feature == "" and absf(m.origin.x - 50.0) < 1e-3).size() == 4,
			"knifed to length round the blank")
	await _shot("plan_pencil")

	# Undo straight after: the pencil comes off, the piece is no part.
	workshop.undo()
	await _frames(1)
	_check(layout.marks_of(piece).is_empty() and not piece.has_meta("part") and
			board.get_stats().get("steps", 0) == steps, "undo takes the pencil off, and the part")
	workshop.hover_screen(_screen(Vector3(-50, -28, 27.5)))
	_click(Vector3(-50, -28, 27.5))
	_check(layout.marks_of(piece).size() == 12, "drawn on again")

	# Pencil holds no tool: a chisel inside the mortise, held by nothing.
	var lock := _lock(Vector3(-5, 0, 27.5))
	_check(layout.hold(lock, "chisel").is_empty(), "pencil lines hold no tool")
	# The knife a millimetre off the length line takes it; a click knifes it in.
	workshop.set_setting("layout", "variant", "knife")
	workshop.hover_screen(_screen(Vector3(51, 10, 27.5)))
	var snapped: Dictionary = layout.preview
	_check(snapped.get("snapped", false) and absf(snapped.origin.x - 50.0) < 1e-3 and snapped.kind == "knife",
			"the knife by the pencil line takes it")
	_click(Vector3(51, 10, 27.5))
	await _settle()
	var knifed: Array = layout.marks_of(piece).filter(func(m): return m.kind == "knife")
	_check(knifed.size() == 1 and absf(knifed[0].origin.x - 50.0) < 1e-3 and absf(knifed[0].to - knifed[0].from - 70.0) < 1e-3 and
			board.get_stats().get("steps", 0) == steps + 1, "a click knifes it in along the whole line (one step)")
	lock = _lock(Vector3(45, 0, 27.5))
	var held: Dictionary = layout.hold(lock, "chisel")
	_check(not held.get("ends", []).is_empty(), "the knifed line holds a chisel going at it")
	print("plan transfer: the head drawn on in 12 pencil lines; the length line knifed in")

	# The pencil: a click draws a line across; a drag, along the edge, gauged from it.
	workshop.set_setting("layout", "variant", "pencil")
	var before: int = layout.marks_of(piece).size()
	_click(Vector3(20, 25, 27.5))
	var drawn: Array = layout.marks_of(piece).slice(before)
	_check(drawn.size() == 1 and drawn[0].kind == "pencil" and drawn[0].as == "knife" and
			absf(drawn[0].origin.x - 20.0) < 0.5 and absf(drawn[0].to - drawn[0].from - 70.0) < 1e-3,
			"a pencil click: a line across the face")
	workshop.hover_screen(_screen(Vector3(-40, 20, 27.5)))
	workshop.press(_screen(Vector3(-40, 20, 27.5)))
	for k in 6:
		workshop.drag_screen(_screen(Vector3(-40 + 5.0 * (k + 1), 20, 27.5)))
	workshop.release()
	drawn = layout.marks_of(piece).slice(before)
	var along: Dictionary = drawn.back() if drawn.size() == 2 else {}
	print("plan transfer: a pencil line dragged along: %s, %.1f mm from the edge, %.1f mm long" % [
			along.get("as", "?"), along.get("distance", -1.0), along.get("to", 0.0) - along.get("from", 0.0)])
	_check(drawn.size() == 2 and along.as == "gauge" and absf(along.distance - 15.0) < 0.5 and
			absf(along.to - along.from - 30.0) < 1.0, "a pencil drag along: a line gauged 15 mm from the edge, 30 mm long")
	workshop.undo()
	workshop.undo()
	_check(layout.marks_of(piece).size() == before and board.get_stats().get("steps", 0) == steps + 1,
			"undo takes the pencil lines off, one at a time, the knifed line left")

	# Scribed as it is laid on: one step, undone as one.
	workshop.set_wood("head_oak")
	await _frames(2)
	workshop.enter_work(false)
	board = workshop.board
	piece = workshop.clamped
	steps = board.get_stats().get("steps", 0)
	workshop.select_tool("layout")
	workshop.set_setting("layout", "variant", "plan")
	workshop.set_setting("layout", "scribe", true)
	workshop.hover_screen(_screen(Vector3(-50, -28, 27.5)))
	_click(Vector3(-50, -28, 27.5))
	await _settle()
	marks = layout.marks_of(piece)
	_check(marks.size() == 12 and marks.all(func(m): return m.kind == "knife") and
			board.get_stats().get("steps", 0) == steps + 1 and piece.has_meta("part"),
			"scribed as it is laid on: 12 knife lines, one step")
	workshop.undo()
	await _settle()
	_check(layout.marks_of(piece).is_empty() and board.get_stats().get("steps", 0) == steps and not piece.has_meta("part"),
			"undone as one")
	workshop.set_setting("layout", "scribe", false)

	print("plan transfer: %s" % ("the plan is laid on the wood" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A point on the piece in the vise (body mm) on the screen.
func _screen(body: Vector3) -> Vector2:
	return workshop.camera.unproject_position(workshop.board.to_global(body))


## A click (no drag) at a point on the piece (body mm).
func _click(body: Vector3) -> void:
	var screen := _screen(body)
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()


## A chisel's lock at a point on the piece's top (body mm), pushed along x.
func _lock(body: Vector3) -> Dictionary:
	var point: Vector3 = workshop.board.to_global(body)
	var normal: Vector3 = (workshop.board.global_basis * Vector3(0, 0, 1)).normalized()
	var path: Vector3 = (workshop.board.global_basis * Vector3(1, 0, 0)).normalized()
	return {"point": point, "normal": normal, "path": path, "along": path, "plane": Plane(normal, point)}


func _settle() -> void:
	workshop.board.flush()
	for i in 600:
		if not workshop.board.is_busy():
			break
		await _frames(1)
	await _frames(1)


## Rendered, with --out: the view as it is, saved as <out>/<name>.png.
func _shot(name: String) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	for i in 3:
		await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_out)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_out, name])


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("plan transfer: " + what)
