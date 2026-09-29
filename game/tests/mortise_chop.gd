extends "res://tests/harness.gd"

## The mallet's mortise made with the workshop's tools, on an oak head blank (120 x 70 x 55)
## in the vise, the head's sheet scribed on it (its mortise 30 x 12, through: body x -20 to
## 10, y -6 to 6) and sawn to length:
## - the brace (B) with its 12 mm bit, driven through the workshop's press and drag (the
##   pointer round the bit, clockwise), at 8 times the pace: three holes through, 9 mm apart
##   along the mortise's middle. Set down 3 mm past the mortise's end and 3 mm off its
##   middle, the bit is held between the knife lines: its rim on the end line, halfway
##   across. Each hole goes through and a millimetre beyond, the grip's turns 1.6 mm each;
## - what the holes leave (the cusps along the sides, the corners) chopped to the lines with
##   the bench chisel's firm blows, as act() strikes them (straight on the SdfBody): its edge
##   along each side, its bevel to the holes, and across each end, half from each face;
## - K: the head as drawn (nothing proud of it by 1 mm).
## Reports the turns, the blows, the time at the workshop's own pace and interval, and the
## edits. Rendered, with --out=<dir>: the brace at work (brace_boring.png), and the mortise
## (mortise_bored.png).

const Workshop := preload("res://workshop/workshop.tscn")
const TOP := 27.5

var workshop
var _failed := false
var _out := ""
var _seed := 1


func _ready() -> void:
	get_tree().create_timer(600.0).timeout.connect(func():
		push_error("mortise chop: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)

	# The head's sheet scribed on a head blank, and the blank sawn to length.
	workshop.set_wood("head_oak")
	await _frames(2)
	workshop.enter_work(false)
	await _frames(2)
	workshop.plans.take_sheet("mallet", "head")
	workshop.select_tool("layout")
	workshop.set_setting("layout", "scribe", true)
	_click(Vector3(-50, -28, TOP))
	await _settle()
	var piece: RigidBody3D = workshop.clamped
	var board = workshop.board
	_check(piece.get_meta("part", {}).get("part", "") == "head" and workshop.layout.marks_of(piece).size() == 12,
			"the head scribed on the blank")
	_saw(board, Vector3(50.4, 0, TOP))
	for i in 300:
		if not workshop.offcuts.is_empty():
			break
		await _frames(1)
	await _settle()
	_check(workshop.clamped == piece and not workshop.offcuts.is_empty(), "sawn to length")

	# The brace: three holes through, the pointer turned round each.
	workshop.select_tool("brace")
	workshop.pace = 8.0
	var turns := 0.0
	var clock := 0.0
	var started := Time.get_ticks_msec()
	for at in [Vector3(-17, 0, TOP), Vector3(-5, 3, TOP), Vector3(4, 0, TOP)]:
		var bored: Dictionary = await _bore(at)
		turns += bored.turns
		clock += bored.seconds
		if at.x < -10.0:
			await _shot("brace_boring")
		workshop.release()
		await _settle()
	workshop.pace = 1.0
	var bored_ms := Time.get_ticks_msec() - started
	# Each hole where the lines hold it: the first with its rim on the end line (x -20), the
	# second halfway across (y 0); all through.
	var holes := [[-14.0, 0.0], [-5.0, 0.0], [4.0, 0.0]]
	var true_holes := 0
	for h in holes:
		var ok := true
		for k in 12:
			var a := TAU * k / 12.0
			for z in [20.0, 0.0, -20.0]:
				var inside := Vector3(h[0] + 5.7 * cos(a), h[1] + 5.7 * sin(a), z)
				ok = ok and not _wood(inside)
		true_holes += 1 if ok else 0
	_check(true_holes == 3, "three 12 mm holes through, where the lines hold them (%d)" % true_holes)
	_check(_wood(Vector3(-14, 6.4, 0)) and _wood(Vector3(-14, -6.4, 0)) and _wood(Vector3(-20.4, 0, 0)) and
			_wood(Vector3(-5, 6.4, 0)) and _wood(Vector3(-5, -6.4, 0)), "none past the lines")
	print("mortise chop: bored at %.0fx pace: %.1f turns (%.1f at 1x: %.0f s at %.0f turns a second), %.1f s of play here" % [
			8.0, turns, turns * 8.0, turns * 8.0 / workshop.BRACE_TURNS, workshop.BRACE_TURNS, clock])

	# What is left chopped to the lines, half from each face.
	started = Time.get_ticks_msec()
	var blows := 0
	for side in [1.0, -1.0]:
		var depth := TOP + 1.0
		for x in [-14.0, -9.5, -5.0, -0.5, 4.0]:
			blows += await _chop_down(Vector3(x, 6, side * TOP), Vector3(0, -1, 0), side, depth, 2.5)
			blows += await _chop_down(Vector3(x, -6, side * TOP), Vector3(0, 1, 0), side, depth, 2.5)
		blows += await _chop_down(Vector3(-20, 0, side * TOP), Vector3(1, 0, 0), side, depth, 6.0)
		blows += await _chop_down(Vector3(10, 0, side * TOP), Vector3(-1, 0, 0), side, depth, 6.0)
	var chopped_ms := Time.get_ticks_msec() - started
	print("mortise chop: then %d firm blows (%.0f s at a blow each %.2f s); %.1f s to bore and %.1f s to chop here; %d edits" % [
			blows, blows * workshop.BLOW_INTERVAL, workshop.BLOW_INTERVAL, bored_ms / 1000.0, chopped_ms / 1000.0,
			board.get_stats().get("edits", 0)])
	_check(blows > 0 and blows < 150, "chopped clean in fewer blows than the 220 it takes unbored (%d)" % blows)

	# Checked: as drawn.
	workshop.debris.clear()
	_key(KEY_K)
	await _frames(1)
	var spots: Array = workshop.checking.result.get("spots", [])
	print("mortise chop: checked: %s" % "; ".join(workshop.checking.lines()))
	var worst := 0.0
	for s in spots:
		worst = maxf(worst, s.get("most", 0.0))
	_check(workshop.checking.result.has("spots") and worst < 1.0, "the head as drawn, to within a millimetre (%.2f)" % worst)
	await _shot("mortise_bored")

	print("mortise chop: %s" % ("the mortise bored and chopped" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The brace pressed at `at` (body mm, on the top) and the pointer turned round it, clockwise
## seen from above, an eighth of a turn at a time, until it is through (or 60 turns of the
## pointer). Returns {"turns": the grip's, "seconds": game time}.
func _bore(at: Vector3) -> Dictionary:
	var board = workshop.board
	var screen := _screen(at)
	workshop.hover_screen(screen)
	workshop.press(screen)
	_check(workshop.is_engaged(), "the brace set on the work at %s" % at)
	var lock: Dictionary = workshop._lock
	var n: Vector3 = lock.normal
	var x: Vector3 = lock.along
	var centre: Vector3 = lock.point
	var r := 0.03
	var seconds := 0.0
	var k := 0
	while k < 8 * 60 and workshop.stroke_state().get("limit", "") == "":
		var a := -TAU * k / 8.0
		workshop.drag_screen(workshop.camera.unproject_position(centre + (x * cos(a) + n.cross(x) * sin(a)) * r))
		seconds += await catch_up(workshop)
		k += 1
	var state: Dictionary = workshop.stroke_state()
	print("mortise chop: bored at (%.0f, %.0f): %.1f mm deep, %s, %d eighths of a turn of the pointer" % [
			at.x, at.y, state.get("depth", 0.0), state.get("limit", ""), k])
	_check(state.get("limit", "") == "through" and absf(state.get("depth", 0.0) - 56.0) < 0.5, "bored through")
	return {"turns": state.get("depth", 0.0) / (workshop.variant().get("pitch", 1.6) * workshop.pace), "seconds": seconds}


## Chops down from the face `side` along a marked line (the chisel's middle at `at`, body mm,
## on the face; its bevel towards `path`, the waste) until no wood stands within `band` mm
## of the line down to `depth`: a firm blow each time with the edge on the line, or moved in
## onto what is left nearer the waste, where it rests (as act() strikes it: a chisel stroke
## straight on the body). Returns the blows.
func _chop_down(at: Vector3, path: Vector3, side: float, depth: float, band: float) -> int:
	var board = workshop.board
	var basis: Basis = board.global_basis
	var n := Vector3(0, 0, side)
	var blows := 0
	while blows < 60:
		var struck := false
		var inward := 0.0
		while inward <= band and not struck:
			var line: Vector3 = at + path * inward
			var d := _resting(line, path, n)
			inward += 0.6
			if d >= depth:
				continue
			_seed += 1
			var start: Vector3 = board.to_global(line - n * d)
			board.begin_stroke("chisel", start, (basis * n).normalized(), (basis * path).normalized(),
					{"variant": "bench_12", "angle": 90.0, "blow": 1.0, "seed": _seed, "length": 6.0})
			board.move_stroke(start)
			board.end_stroke()
			await _settle()
			blows += 1
			struck = true
		if not struck:
			break
	return blows


## How deep below the face (normal `n`) the chisel's edge, 12 mm wide, its middle at `line`
## (body mm) and its back to `path`, first meets wood across its width: rays straight down
## onto it.
func _resting(line: Vector3, path: Vector3, n: Vector3) -> float:
	var board = workshop.board
	var basis: Basis = board.global_basis
	var across := n.cross(path)
	var least := 2.0 * TOP
	for j in range(-4, 5):
		var q: Vector3 = line + path * 0.3 + across * (5.4 * j / 4.0)
		var hit: Dictionary = board.raycast(board.to_global(q + n * 5.0), (basis * -n).normalized(), 0.2)
		if not hit.is_empty():
			var d: float = (q - board.to_local(hit.position)).dot(n)
			least = minf(least, maxf(d, 0.0))
	return least


## Whether there is wood at a point (body mm).
func _wood(p: Vector3) -> bool:
	return workshop.board.distance_at(workshop.board.to_global(p)) < 0.0


## A saw stroke straight on the body across its length at `at` (body mm), through.
func _saw(board, at: Vector3) -> void:
	var basis: Basis = board.global_basis
	var along := Vector3(0, 1, 0)
	board.begin_stroke("saw", board.to_global(at), (basis * Vector3(0, 0, 1)).normalized(), (basis * along).normalized(),
			{"feed": 0.5})
	for y in [45.0, -45.0, 45.0, -45.0, 45.0, -45.0]:
		board.move_stroke(board.to_global(at + along * y))
	board.end_stroke()


func _screen(body: Vector3) -> Vector2:
	return workshop.camera.unproject_position(workshop.board.to_global(body))


## A click (no drag) at a point on the piece (body mm).
func _click(body: Vector3) -> void:
	var screen := _screen(body)
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
		push_error("mortise chop: " + what)
