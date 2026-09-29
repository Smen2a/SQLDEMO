extends "res://tests/harness.gd"

## Checking a part against its drawing (workshop/checking.gd, SdfBody.check_part), on an oak
## mallet-head blank (120 x 70 x 55) in the vise with the head's sheet laid on it:
## - K at the bench: two places proud, what is left to do: the far end, 10 mm (not yet cut
##   to length), and the mortise (not yet chopped: up to 6 mm, half its width), named so; dots
##   on the wood; the part's panel lists them;
## - sawn to length (straight on the SdfBody, the kerf on the waste side of the line): the
##   check says the wood has changed, its dots hidden; K again: the mortise only;
## - K with a check showing clears it;
## - a board not laid out from a plan: K says so, and draws nothing.
## Rendered, with --out=<dir>: the dots on the blank (check_part.png).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false
var _out := ""


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("check part: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	var checking = workshop.checking

	# The head's sheet laid on a head blank.
	workshop.set_wood("head_oak")
	await _frames(2)
	workshop.enter_work(false)
	await _frames(2)
	workshop.plans.take_sheet("mallet", "head")
	workshop.select_tool("layout")
	var piece: RigidBody3D = workshop.clamped
	_click(Vector3(-50, -28, 27.5))
	_check(piece.get_meta("part", {}).get("part", "") == "head", "the blank is the mallet's head")

	# K: the far end and the mortise, proud.
	_key(KEY_K)
	await _frames(1)
	var spots: Array = checking.result.get("spots", [])
	print("check part: laid out, checked in %.0f ms: %s" % [checking.result.get("ms", 0.0), "; ".join(checking.lines())])
	_check(spots.size() == 2, "two places off the drawing (%d)" % spots.size())
	var far: Dictionary = spots[0] if spots.size() > 0 else {}
	var mortise: Dictionary = spots[1] if spots.size() > 1 else {}
	_check(far.get("kind", "") == "proud" and absf(far.get("most", 0.0) - 10.0) < 0.2 and checking.where(far) == "The far end",
			"the far end 10 mm proud: %s" % [checking.describe(far) if not far.is_empty() else "none"])
	_check(mortise.get("kind", "") == "proud" and absf(mortise.get("most", 0.0) - 6.0) < 0.3 and mortise.get("feature", -1) == 0 and
			checking.where(mortise).begins_with("The mortise"), "the mortise, up to 6 mm proud: %s" % [
			checking.describe(mortise) if not mortise.is_empty() else "none"])
	# The far end's dots beyond the length line (body x 50), the mortise's round it.
	var beyond := true
	for p in far.get("dots", PackedVector3Array()):
		beyond = beyond and p.x > 50.4
	_check(beyond and checking.dot_count() > 0, "dots on the wood (%d), the far end's beyond the line" % checking.dot_count())
	_check(workshop._ui._part_box.visible and workshop._ui._check_text.text.contains("The far end") and
			workshop._ui._check_text.text.contains("The mortise"), "the part's panel names them: " + workshop._ui._check_text.text)
	await _shot("check_part")

	# Sawn to length at the knife line (body x 50; the kerf, 0.8 mm, on the waste side).
	var board = workshop.board
	_saw(board, Vector3(50.4, 0, 27.5))
	for i in 300:
		if not workshop.offcuts.is_empty():
			break
		await _frames(1)
	await _settle()
	_check(workshop.clamped == piece and not workshop.offcuts.is_empty() and piece.has_meta("part"),
			"sawn to length: the offcut comes away, the head stays in the vise")
	_check(checking.stale and checking.dot_count() == 0 and checking.lines()[0].contains("changed"),
			"the check says the wood has changed")
	_key(KEY_K)
	await _frames(1)
	spots = checking.result.get("spots", [])
	print("check part: sawn to length, checked in %.0f ms: %s" % [checking.result.get("ms", 0.0), "; ".join(checking.lines())])
	_check(spots.size() == 1 and spots[0].get("feature", -1) == 0 and not checking.stale, "the mortise only is left")
	_key(KEY_K)
	await _frames(1)
	_check(checking.result.is_empty() and checking.dot_count() == 0, "K again clears it")

	# A board not laid out from a plan: nothing to check it against.
	workshop.set_wood("board")
	await _frames(2)
	workshop.enter_work(false)
	await _frames(1)
	_key(KEY_K)
	await _frames(1)
	_check(checking.result.has("none") and checking.dot_count() == 0 and checking.lines()[0].contains("not laid out"),
			"a board that is no part: %s" % [checking.lines()])

	print("check part: %s" % ("the part checked against its drawing" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A saw stroke straight on the body across its length at `at` (body mm), through.
func _saw(board, at: Vector3) -> void:
	var basis: Basis = board.global_basis
	var along := Vector3(0, 1, 0)
	board.begin_stroke("saw", board.to_global(at), (basis * Vector3(0, 0, 1)).normalized(), (basis * along).normalized(),
			{"feed": 0.5})
	for y in [45.0, -45.0, 45.0, -45.0, 45.0, -45.0]:
		board.move_stroke(board.to_global(at + along * y))
	board.end_stroke()


## A click (no drag) at a point on the piece (body mm).
func _click(body: Vector3) -> void:
	var screen: Vector2 = workshop.camera.unproject_position(workshop.board.to_global(body))
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()


func _key(code: Key) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	workshop._unhandled_input(key)


func _settle() -> void:
	workshop.board.flush()
	for i in 600:
		if not workshop.board.is_busy():
			break
		await _frames(1)
	await _frames(1)


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
		push_error("check part: " + what)
