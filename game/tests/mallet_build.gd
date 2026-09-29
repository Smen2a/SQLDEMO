extends "res://tests/harness.gd"

## The whole mallet made from the rack with the workshop's tools (G4c; the long tier:
## `tools/test_godot.sh --long`). Each part's stock is taken from its stack on the rack
## (take_stock) and let go over the vise; its sheet scribed on; then:
## - the head: sawn to length; the mortise bored with the brace through the workshop's own
##   press and drag (three 12 mm holes), and what they leave chopped to the lines with firm
##   blows (as act() strikes them), half from each face;
## - the handle: sawn to length; the tenon's four shoulders sawn across, its four cheeks
##   sawn in from the end down to them (the waste comes away as it is freed), and the
##   wedge's kerf sawn down its middle;
## - the wedge: sawn off its strip (the part goes with it, the strip staying in the vise),
##   then its taper sawn along it from the thin end;
## - each checked against its drawing (K): nothing off by a millimetre;
## - put together as a player does (wedge_glue's way): the handle offered up to the head in
##   the vise, glued, pushed and tapped home, joined; the glue set; out, tipped handle down,
##   back in the vise; the wedge offered and driven home; the tenon and wedge sawn flush;
## - recognised as the finished mallet, and taken up as yours.
## Saws cut at their real rate, straight on the part's body, held to the scribed lines
## (their depths the lines': a shoulder to the tenon's face, a cheek to the shoulder), at the
## pace set below. Reports each part's play time (the saw's travel at its working speed, the
## brace's turns, the blows at the mallet's interval), the time it took here, and its
## strokes, turns, blows and edits.

const Workshop := preload("res://workshop/workshop.tscn")
const PACE := 8.0
const BIT := 12.0

var workshop
var assembling
var _failed := false
var _out := ""
var _seed := 1
## Per part: {"play" (s), "strokes", "turns", "blows"}; and the table's rows.
var _tally := {}
var _rows: Array[String] = []


func _ready() -> void:
	get_tree().create_timer(1800.0).timeout.connect(func():
		push_error("mallet build: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	assembling = workshop.assembling
	workshop.pace = PACE

	var head: RigidBody3D = await _make_head()
	var handle: RigidBody3D = await _make_handle() if not _failed else null
	var wedge: RigidBody3D = await _make_wedge() if not _failed else null
	if not _failed:
		await _put_together(head, handle, wedge)

	print("mallet build: part     play (s)  here (s)  saw strokes  turns  blows  edits")
	for row in _rows:
		print("mallet build: " + row)
	print("mallet build: %s" % ("the mallet made from the rack" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


# --- the parts ---------------------------------------------------------------------------

func _make_head() -> RigidBody3D:
	var started := Time.get_ticks_msec()
	var head: RigidBody3D = await _from_rack("head_oak", "head", Vector3(-50, -28, 27.5))
	if _failed:
		return head
	var sdf = head.get_meta("sdf")
	var tag: Dictionary = head.get_meta("part")
	_tally = {"play": 0.0, "strokes": 0, "turns": 0.0, "blows": 0}
	# To length: the kerf on the waste side of the far end's line (part x 110).
	await _saw(sdf, tag, Vector3(110.45, 35, 0), Vector3(0, 0, -1), Vector3(0, 1, 0))
	await _offcut_off(1)
	# The mortise: bored (three holes, the end ones' rims on the end lines), then chopped.
	workshop.select_tool("brace")
	for x in [46.0, 55.0, 64.0]:
		await _bore(sdf, tag, Vector3(x, 35, 0))
	for face in [0.0, 55.0]:
		var out := Vector3(0, 0, -1 if face == 0.0 else 1) # (the face's outward normal, part space)
		var depth := 28.5
		for x in [46.0, 50.5, 55.0, 59.5, 64.0]:
			await _chop_down(sdf, tag, Vector3(x, 29, face), Vector3(0, 1, 0), out, depth, 2.5)
			await _chop_down(sdf, tag, Vector3(x, 41, face), Vector3(0, -1, 0), out, depth, 2.5)
		await _chop_down(sdf, tag, Vector3(40, 35, face), Vector3(1, 0, 0), out, depth, 6.0)
		await _chop_down(sdf, tag, Vector3(70, 35, face), Vector3(-1, 0, 0), out, depth, 6.0)
	await _checked(head, "head")
	_row("head", started, sdf)
	return head


func _make_handle() -> RigidBody3D:
	var started := Time.get_ticks_msec()
	var handle: RigidBody3D = await _from_rack("handle_ash", "handle", Vector3(-150, -12, 14))
	if _failed:
		return handle
	var sdf = handle.get_meta("sdf")
	var tag: Dictionary = handle.get_meta("part")
	_tally = {"play": 0.0, "strokes": 0, "turns": 0.0, "blows": 0}
	var offcuts: int = workshop.offcuts.size()
	await _saw(sdf, tag, Vector3(300.45, 17.5, 0), Vector3(0, 0, -1), Vector3(0, 1, 0))
	await _offcut_off(offcuts + 1)
	# The tenon: the shoulders across at x 58 (their kerfs on the end's side), each down to
	# the tenon's face; then the cheeks in from the end, their kerfs on the waste side of the
	# tenon's lines, down to the shoulder. Its thickness first (z 8 to 20), then its width
	# (y 2.5 to 32.5): each waste comes away as the cheek meets its shoulder.
	await _saw(sdf, tag, Vector3(57.55, 17.5, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), 8.0)
	await _saw(sdf, tag, Vector3(57.55, 17.5, 28), Vector3(0, 0, 1), Vector3(0, 1, 0), 8.0)
	await _saw(sdf, tag, Vector3(0, 17.5, 7.55), Vector3(-1, 0, 0), Vector3(0, 1, 0), 58.0)
	await _saw(sdf, tag, Vector3(0, 17.5, 20.45), Vector3(-1, 0, 0), Vector3(0, 1, 0), 58.0)
	await _saw(sdf, tag, Vector3(57.55, 0, 14), Vector3(0, -1, 0), Vector3(0, 0, 1), 2.5)
	await _saw(sdf, tag, Vector3(57.55, 35, 14), Vector3(0, 1, 0), Vector3(0, 0, 1), 2.5)
	await _saw(sdf, tag, Vector3(0, 2.05, 14), Vector3(-1, 0, 0), Vector3(0, 0, 1), 58.0)
	await _saw(sdf, tag, Vector3(0, 32.95, 14), Vector3(-1, 0, 0), Vector3(0, 0, 1), 58.0)
	# The wedge's kerf down the tenon's middle, 38 deep.
	await _saw(sdf, tag, Vector3(0, 17.5, 14), Vector3(-1, 0, 0), Vector3(0, 0, 1), 38.0)
	await _settle(sdf)
	print("mallet build: the tenon's waste came away in %d pieces" % (workshop.offcuts.size() - offcuts - 1))
	await _checked(handle, "handle")
	_row("handle", started, sdf)
	return handle


func _make_wedge() -> RigidBody3D:
	var started := Time.get_ticks_msec()
	var strip: RigidBody3D = await _from_rack("strip_oak", "wedge", Vector3(-70, -4, 2.5))
	if _failed:
		return strip
	var sdf = strip.get_meta("sdf")
	var tag: Dictionary = strip.get_meta("part")
	_tally = {"play": 0.0, "strokes": 0, "turns": 0.0, "blows": 0}
	var offcuts: int = workshop.offcuts.size()
	await _saw(sdf, tag, Vector3(45.45, 6, 0), Vector3(0, 0, -1), Vector3(0, 1, 0))
	await _offcut_off(offcuts + 1)
	# The wedge came away: the part with it, the strip left in the vise no part.
	var wedge: RigidBody3D = workshop.offcuts.back().body
	_check(wedge.get_meta("part", {}).get("part", "") == "wedge" and not strip.has_meta("part"),
			"sawn off its strip, the wedge is the part")
	workshop._unclamp()
	_set_aside(strip, Vector3(-0.6, 0.05, -0.25))
	workshop._put_in_vise(wedge)
	await _frames(2)
	sdf = wedge.get_meta("sdf")
	tag = wedge.get_meta("part")
	# The taper: from the thin end (1 mm) to the back's full 5 mm at the other, sawn along it
	# from the thin end, the kerf on the waste side (the back's).
	var slope := Vector3(45, 0, -4).normalized() # (out of the wood at the thin end, along the taper)
	var waste := Vector3(4, 0, 45).normalized()
	await _saw(sdf, tag, Vector3(45, 6, 1) + waste * 0.45, slope, Vector3(0, 1, 0))
	await _offcut_off(offcuts + 2)
	await _checked(wedge, "wedge")
	_row("wedge", started, sdf)
	return wedge


## The parts put together, glued, wedged and sawn flush.
func _put_together(head: RigidBody3D, handle: RigidBody3D, wedge: RigidBody3D) -> void:
	var started := Time.get_ticks_msec()
	_tally = {"play": 0.0, "strokes": 0, "turns": 0.0, "blows": 0}
	workshop.leave_work()
	if workshop.clamped != null:
		var was: RigidBody3D = workshop.clamped
		workshop._unclamp()
		_set_aside(was, Vector3(-0.6, 0.05, -0.25))
	_clear_bench()
	workshop._put_in_vise(head)
	await _frames(2)
	var head_sdf = head.get_meta("sdf")
	var handle_sdf = handle.get_meta("sdf")
	var wedge_sdf = wedge.get_meta("sdf")
	var head_tag: Dictionary = head.get_meta("part")
	var handle_tag: Dictionary = handle.get_meta("part")
	handle.freeze = false
	handle.global_position = Vector3(0.3, 0.1, 0.25)
	workshop.pick_up(handle)
	await _look_at(_world(head_sdf, head_tag, Vector3(55, 35, 0)))
	_key(KEY_E)
	_check(assembling.offering(), "the handle offered up to the head: " + workshop.prompt())
	if not assembling.offering():
		return
	_key(KEY_G)
	for i in 62:
		_wheel(true)
	var pushed: float = assembling.offer.t
	var blows := 0
	while assembling.offer.t < assembling.offer.joint.travel - 0.01 and blows < 60:
		_click()
		blows += 1
		await get_tree().create_timer(assembling.BLOW_INTERVAL + 0.02).timeout
	print("mallet build: the handle into the head: %s; pushed %.1f mm by hand, %d blows: %s" % [
			assembling.offer.fit.kind, pushed, blows, assembling.offer.said])
	_tally.blows += blows
	_tally.play += blows * assembling.BLOW_INTERVAL
	_check(assembling.offer.t >= assembling.offer.joint.travel - 0.01, "the handle home in the head")
	_key(KEY_E)
	await _frames(1)
	assembling.clock += assembling.GLUE_SET + 1.0
	var member: Dictionary = _member(head, handle_sdf)
	_check(not member.is_empty() and assembling.why_locked(member) == "glued", "joined, and the glue set")

	# Out of the vise, tipped handle down, back in: the tenon's end up.
	await _look_at(_world(head_sdf, head_tag, Vector3(100, 35, 0)))
	_key(KEY_F)
	await _frames(1)
	_check(workshop.held == head, "the head out of the vise")
	_key(KEY_T)
	_key(KEY_T)
	_clear_bench() # (the waste of the flush cuts to come aside, too)
	await _seconds(1.5)
	await _look_at(Vector3(0.0, 0.0, 0.05))
	var over_vise: String = workshop.prompt()
	_key(KEY_E)
	await _frames(2)
	_check(workshop.clamped == head, "the mallet stood on its handle in the vise: " + over_vise)

	# The wedge, driven into the kerf.
	var tip: Vector3 = _world(handle_sdf, handle_tag, Vector3(0, 17.5, 14))
	wedge.freeze = false
	wedge.global_position = Vector3(0.3, 0.1, 0.25)
	workshop.pick_up(wedge)
	await _look_at(tip)
	_key(KEY_E)
	_check(assembling.offering() and assembling.offer.joint.kind == "wedge", "the wedge on the kerf: " + workshop.prompt())
	if not assembling.offering():
		return
	_wheel(true)
	blows = 0
	while assembling.offer.t < assembling.offer.joint.travel - 0.01 and blows < 80:
		_click()
		blows += 1
		await get_tree().create_timer(assembling.BLOW_INTERVAL + 0.02).timeout
	print("mallet build: the wedge driven in %d blows: %s" % [blows, assembling.offer.said])
	_tally.blows += blows
	_tally.play += blows * assembling.BLOW_INTERVAL
	_key(KEY_E)
	await _frames(1)
	var wedged: Dictionary = _member(head, wedge_sdf)
	_check(not wedged.is_empty() and assembling.why_locked(wedged) == "wedged", "the wedge home and holding")

	# Sawn flush with the head's back (the handle's x 3), the kerf on the proud side.
	var offcuts: int = workshop.offcuts.size()
	await _saw(wedge_sdf, handle_tag, Vector3(2.55, 17.5, 0), Vector3(0, 0, -1), Vector3(0, 1, 0), INF, handle_sdf)
	await _saw(handle_sdf, handle_tag, Vector3(2.55, 17.5, 8), Vector3(0, 0, -1), Vector3(0, 1, 0))
	await _offcut_off(offcuts + 2)
	_check(head.get_meta("members", []).size() == 2, "sawn flush, the mallet still one piece")
	workshop.debris.clear()
	await _look_at(head_sdf.global_position)
	await _shot("mallet_built")
	_row("together", started, head_sdf)

	# Finished: taken up as your mallet.
	var made: Dictionary = assembling.finished(head)
	_check(made.get("plan", "") == "mallet", "recognised as the finished mallet")
	workshop.take_up(head)
	print("mallet build: taken up as %s: blows %.2f times the workshop mallet's" % [workshop.mallet.get("name", "?"),
			workshop.blow_weight()])
	_check(workshop.mallet.get("piece") == head and not head.visible, "and yours")


# --- the tools ---------------------------------------------------------------------------

## A part's stock from its stack on the rack, let go over the vise (what was in it set
## aside), its sheet scribed on at `corner` (body mm, on the top face near the part's
## reference corner). Returns the piece.
func _from_rack(kind: String, part: String, corner: Vector3) -> RigidBody3D:
	workshop.leave_work()
	if workshop.clamped != null:
		var was: RigidBody3D = workshop.clamped
		workshop._unclamp()
		_set_aside(was, Vector3(0.45 + 0.2 * _rows.size(), 0.05, -0.2))
	_clear_bench()
	var piece: RigidBody3D = workshop.take_stock(workshop.room.stacks[kind])
	await _look_at(Vector3(0.05, 0.0, -0.03))
	await _seconds(0.6)
	workshop.let_go()
	await _frames(2)
	_check(workshop.clamped == piece, "the %s from the rack in the vise: %s" % [kind, workshop.prompt()])
	if workshop.clamped != piece:
		return piece
	workshop.enter_work(false)
	await _frames(2)
	workshop.plans.take_sheet("mallet", part)
	workshop.select_tool("layout")
	workshop.set_setting("layout", "scribe", true)
	_click(corner)
	await _settle(workshop.board)
	_check(piece.get_meta("part", {}).get("part", "") == part, "the %s's sheet scribed on" % part)
	return piece


## The waste lying about (offcuts, not parts) put out of the way, as a player clears the
## bench before the next piece goes in the vise.
func _clear_bench() -> void:
	var k := 0
	for piece in workshop.pieces:
		if piece != workshop.clamped and not piece.has_meta("part") and piece.get_meta("members", []).is_empty():
			_set_aside(piece, Vector3(-1.2 + 0.15 * (k % 8), 0.3 + 0.1 * (k / 8), 1.4))
			k += 1


## A piece set down out of the way, held still.
func _set_aside(piece: RigidBody3D, at: Vector3) -> void:
	piece.freeze = true
	piece.linear_velocity = Vector3.ZERO
	piece.angular_velocity = Vector3.ZERO
	piece.global_position = at


## A saw stroke straight on a part's body (its `tag`'s placement: part space to body), at
## its real rate and the pace: set at `at` (part mm, on the face it goes into), going in
## along -`normal`, its line along `along`, worked 200 mm back and forth until it goes no
## deeper: through, its back, or `depth` mm down (a line it is held to). `frame` is the body
## the part space is placed in (the saw's own body by default).
func _saw(sdf, tag: Dictionary, at: Vector3, normal: Vector3, along: Vector3, depth := INF, frame = null) -> void:
	if frame == null:
		frame = sdf
	var place: Transform3D = tag.placement
	var b_at: Vector3 = sdf.to_local(frame.to_global(place * at))
	var b_n: Vector3 = sdf.global_basis.inverse() * (frame.global_basis * (place.basis * normal)).normalized()
	var b_along: Vector3 = sdf.global_basis.inverse() * (frame.global_basis * (place.basis * along))
	b_n = b_n.normalized()
	b_along = b_along.normalized()
	var s := {"pressure": 1.0, "pace": PACE}
	if depth < INF:
		var floor_at: Vector3 = b_at - b_n * depth
		s["limits"] = {"floors": [Vector4(b_n.x, b_n.y, b_n.z, -b_n.dot(floor_at))]}
	await _settle(sdf)
	var basis: Basis = sdf.global_basis
	sdf.begin_stroke("saw", sdf.to_global(b_at), (basis * b_n).normalized(), (basis * b_along).normalized(), s)
	var last := -1.0
	var strokes := 0
	sdf.move_stroke(sdf.to_global(b_at + b_along * 100.0))
	while strokes < 600:
		sdf.move_stroke(sdf.to_global(b_at - b_along * 100.0))
		sdf.move_stroke(sdf.to_global(b_at + b_along * 100.0))
		strokes += 1
		var st: Dictionary = sdf.get_stroke_state()
		if st.get("limit", "") != "" or st.get("depth", 0.0) <= last + 1e-4:
			break
		last = st.get("depth", 0.0)
	var reached: Dictionary = sdf.get_stroke_state()
	sdf.end_stroke()
	_tally.strokes += strokes
	_tally.play += strokes * 400.0 / (workshop.WORKING_SPEED.saw * PACE)
	await _settle(sdf)
	if depth < INF:
		_check(absf(reached.get("depth", 0.0) - depth) < 0.3, "sawn %.1f mm down, to the line at %.1f" % [
				reached.get("depth", 0.0), depth])


## The brace, from the workshop's press and drag: set at `at` (part mm, on the face side)
## and the pointer turned round it, clockwise, until it is through.
func _bore(sdf, tag: Dictionary, at: Vector3) -> void:
	await _settle(sdf) # (a body answers no ray while an edit, or a split, is being applied)
	var screen: Vector2 = workshop.camera.unproject_position(sdf.to_global(tag.placement * at))
	workshop.hover_screen(screen)
	workshop.press(screen)
	_check(workshop.is_engaged(), "the brace set on the head")
	if not workshop.is_engaged():
		return
	var lock: Dictionary = workshop._lock
	var n: Vector3 = lock.normal
	var x: Vector3 = lock.along
	var k := 0
	while k < 8 * 60 and workshop.stroke_state().get("limit", "") == "":
		var a := -TAU * k / 8.0
		workshop.drag_screen(workshop.camera.unproject_position(lock.point + (x * cos(a) + n.cross(x) * sin(a)) * 0.03))
		await catch_up(workshop)
		k += 1
	var state: Dictionary = workshop.stroke_state()
	var turns: float = state.get("depth", 0.0) / (workshop.variant().get("pitch", 1.6) * PACE)
	_tally.turns += turns
	_tally.play += turns / workshop.BRACE_TURNS
	_check(state.get("limit", "") == "through", "bored through")
	workshop.release()
	await _settle(sdf)


## Chops down along a marked line (part space: `at` on the face, `path` into the waste,
## `out` the face's outward normal) until no wood stands within `band` mm of it down to
## `depth`: a firm blow each time, the edge on the line or moved in onto what is left, where
## it rests (as act() strikes: a chisel stroke straight on the body).
func _chop_down(sdf, tag: Dictionary, at_part: Vector3, path_part: Vector3, out_part: Vector3, depth: float,
		band: float) -> void:
	var place: Transform3D = tag.placement
	var at: Vector3 = place * at_part
	var path: Vector3 = (place.basis * path_part).normalized()
	var n: Vector3 = (place.basis * out_part).normalized()
	var basis: Basis = sdf.global_basis
	var blows := 0
	while blows < 60:
		var struck := false
		var inward := 0.0
		while inward <= band and not struck:
			var line: Vector3 = at + path * inward
			var d := _resting(sdf, line, path, n)
			inward += 0.6
			if d >= depth:
				continue
			_seed += 1
			var start: Vector3 = sdf.to_global(line - n * d)
			sdf.begin_stroke("chisel", start, (basis * n).normalized(), (basis * path).normalized(),
					{"variant": "bench_12", "angle": 90.0, "blow": 1.0, "seed": _seed, "length": 6.0})
			sdf.move_stroke(start)
			sdf.end_stroke()
			await _settle(sdf)
			blows += 1
			struck = true
		if not struck:
			break
	_tally.blows += blows
	_tally.play += blows * workshop.BLOW_INTERVAL


## How deep below the face (outward `n`, body space) a 12 mm chisel's edge on `line` (its
## middle; its back to `path`) first meets wood across its width: rays straight onto it.
func _resting(sdf, line: Vector3, path: Vector3, n: Vector3) -> float:
	var basis: Basis = sdf.global_basis
	var across := n.cross(path)
	var least := 60.0
	for j in range(-4, 5):
		var q: Vector3 = line + path * 0.3 + across * (5.4 * j / 4.0)
		var hit: Dictionary = sdf.raycast(sdf.to_global(q + n * 5.0), (basis * -n).normalized(), 0.2)
		if not hit.is_empty():
			least = minf(least, maxf((q - sdf.to_local(hit.position)).dot(n), 0.0))
	return least


## K on the part in the vise: nothing off its drawing by a millimetre.
func _checked(piece: RigidBody3D, name: String) -> void:
	await _settle(piece.get_meta("sdf"))
	workshop.debris.clear()
	workshop.checking.check()
	var worst := 0.0
	for s in workshop.checking.result.get("spots", []):
		worst = maxf(worst, s.get("most", 0.0))
	print("mallet build: the %s checked: %s" % [name, "; ".join(workshop.checking.lines())])
	_check(workshop.checking.result.has("spots") and worst < 1.0, "the %s as drawn to within a millimetre (%.2f off)" % [
			name, worst])
	workshop.checking.clear()


func _row(part: String, started: int, sdf) -> void:
	_rows.append("%-8s %8.1f  %8.1f  %11d  %5.1f  %5d  %5d" % [part, _tally.play, (Time.get_ticks_msec() - started) / 1000.0,
			_tally.strokes, _tally.turns, _tally.blows, sdf.get_stats().get("edits", 0)])


## Until `count` pieces have come away (or ten seconds of frames).
func _offcut_off(count: int) -> void:
	for i in 600:
		if workshop.offcuts.size() >= count:
			break
		await _frames(1)
	await _frames(2)
	if workshop.board != null:
		await _settle(workshop.board)
	_check(workshop.offcuts.size() >= count, "the waste came away (%d of %d)" % [workshop.offcuts.size(), count])


func _member(piece: RigidBody3D, sdf) -> Dictionary:
	for m in piece.get_meta("members", []):
		if m.sdf == sdf:
			return m
	return {}


func _world(sdf, tag: Dictionary, part_point: Vector3) -> Vector3:
	return sdf.to_global(tag.placement * part_point)


func _look_at(point: Vector3) -> void:
	workshop.player.face(point)
	await _frames(2)


## A click (no drag) at a point on the piece in the vise (body mm).
func _click(body := Vector3.INF) -> void:
	if body == Vector3.INF:
		var e := InputEventMouseButton.new()
		e.button_index = MOUSE_BUTTON_LEFT
		e.pressed = true
		workshop._unhandled_input(e)
		return
	var screen: Vector2 = workshop.camera.unproject_position(workshop.board.to_global(body))
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()


func _key(code: Key) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	workshop._unhandled_input(key)


func _wheel(up: bool) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_WHEEL_UP if up else MOUSE_BUTTON_WHEEL_DOWN
	e.pressed = true
	workshop._unhandled_input(e)


## Until a body has nothing left to do: its edits applied, and any waste its cuts freed found
## and come away (it looks once it falls idle).
func _settle(sdf) -> void:
	sdf.flush()
	for i in 900:
		if sdf.is_idle():
			break
		await _frames(1)
	await _frames(1)


func _seconds(seconds: float) -> void:
	var waited := 0.0
	while waited < seconds:
		await get_tree().physics_frame
		waited += get_physics_process_delta_time()


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
		push_error("mallet build: " + what)
