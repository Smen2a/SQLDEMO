extends "res://tests/harness.gd"

## What the tools take off comes away (workshop/debris.gd), driven through the workshop's
## own pointer methods:
## - a chisel pared along the board from near its end (a left-drag, no plan): its shaving grows
##   at the edge during the drag, and comes away as one body when the stroke ends, as much
##   wood as the board lost; it comes to rest on the board or the bench, lies there a couple
##   of seconds, fades and is gone;
## - pared along the front edge with half the chisel off the board, the shaving is only as wide
##   as the wood under the edge;
## - pared across the grain, the shaving comes off in pieces;
## - against the grain in oak (planned, uphill), the tear-out comes off as chips, which fade
##   and go too;
## - a chop near the end pops a chip off;
## - undo takes the last stroke's debris back, and Sweep clears the rest.
## Prints what came off and what taking it in cost. Rendered, with --out=<dir>, saves close
## views: <dir>/debris_shaving_working.png (the shaving curling off the edge mid-stroke),
## debris_shaving.png (where it came to rest), debris_chips.png.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false
var _feed_usec := 0
var _out := ""


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "600"))).timeout.connect(func():
		push_error("debris: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	_out = user_arg("--out", "")
	await _frames(3)

	# Along the grain from the end: one shaving, growing as the chisel goes.
	workshop.set_setting("chisel", "depth", 0.5)
	workshop.set_setting("chisel", "angle", 30.0)
	var before := _depths(-0.08, -0.028, -0.008, 0.008)
	var made_before: int = workshop.debris.made.shaving
	var growth := await _stroke("chisel", _on_top(-0.0785, 0.0), [_on_top(-0.03, 0.0)], 20, false,
			"debris_shaving_working")
	workshop.board.flush()
	var lost := _lost(before, _depths(-0.08, -0.028, -0.008, 0.008))
	var shavings: int = workshop.debris.made.shaving - made_before
	_check(growth.size() > 3 and growth[growth.size() - 1] > growth[0], "the shaving grows during the drag (%s)" % [growth])
	_check(shavings == 1, "along the grain it comes away in one piece (%d)" % shavings)
	if shavings >= 1:
		var shaving: Dictionary = workshop.debris.pieces.back()
		print("debris: a 50 mm paring cut, 0.5 mm deep: a shaving of %.1f mm^3 (the board lost %.1f mm^3), %.2f g; taking it in cost at most %d us a frame" % [
				shaving.volume, lost, shaving.volume * 0.67e-3, _feed_usec])
		_check(absf(shaving.volume - lost) < 0.2 * lost, "the shaving is the wood the board lost")
		var body: RigidBody3D = shaving.body
		var clock := 0.0
		while shaving.rest < 0.0 and clock < 5.0:
			await _frames(1)
			clock += get_process_delta_time()
		var at := body.global_position
		print("debris: %.1f s after it came away it rests %.1f mm above the bench" % [clock, at.y * 1000.0])
		_check(shaving.rest >= 0.0 and clock < 3.5 and at.y > 0.0 and at.y < 0.05, "it comes to rest on the board or the bench")
		await _shot("debris_shaving", at)
		# Lies there a while, fades, and goes.
		var alpha := 1.0
		while is_instance_valid(body) and clock < 10.0:
			await _frames(1)
			clock += get_process_delta_time()
			if shaving.fade >= 0.0 and is_instance_valid(body):
				alpha = minf(alpha, (shaving.view.material_override as StandardMaterial3D).albedo_color.a)
		var faded := clock
		print("debris: it faded and was gone %.1f s after it came away" % faded)
		_check(not is_instance_valid(body) and not workshop.debris.pieces.has(shaving) and alpha < 1.0,
				"it fades and goes")

	# Pared along the board's front edge with half the chisel off it (Alt: no edge lock): the
	# shaving is only as wide as the wood under the edge.
	workshop.set_setting("chisel", "depth", 0.3)
	var view: Camera3D = workshop.camera
	var half_from := _on_top(-0.0785, 0.0485)
	workshop.select_tool("chisel")
	await _frames(8)
	await _idle()
	workshop.hover_screen(view.unproject_position(half_from))
	await _frames(2)
	workshop.press(view.unproject_position(half_from), true)
	workshop.drag_screen(view.unproject_position(half_from + Vector3(0.004, 0, 0)))
	workshop.drag_screen(view.unproject_position(half_from + Vector3(0.035, 0, 0)))
	var widest := 0.0
	var half_waited := 0.0
	while workshop.lagging() and half_waited < 60.0:
		await _frames(1)
		half_waited += get_process_delta_time()
		if workshop.debris.live_samples() > 0:
			for w in workshop.debris._live.width:
				widest = maxf(widest, w)
	workshop.release()
	workshop.board.flush()
	var chisel_width: float = workshop.variant().width * 0.001
	print("debris: pared half off the front edge, the shaving %.1f mm wide at most (the chisel %.0f)" % [widest * 1000.0,
			chisel_width * 1000.0])
	_check(widest > 0.3 * chisel_width and widest < 0.7 * chisel_width, "the shaving is as wide as the wood under the edge")
	workshop.undo()
	workshop.board.flush()

	# Across the grain from the side: it crumbles into pieces.
	var pieces_before := _count("shaving")
	made_before = workshop.debris.made.shaving
	workshop.set_setting("chisel", "depth", 0.3)
	await _stroke("chisel", _on_top(0.0, 0.03), [_on_top(0.0, -0.005)], 12, false)
	var across: int = workshop.debris.made.shaving - made_before
	print("debris: 30 mm across the grain: the shaving came off in %d pieces" % across)
	_check(across >= 2, "across the grain the shaving breaks")

	# Undo takes the last stroke's shavings back.
	workshop.undo()
	workshop.board.flush()
	await _frames(1)
	_check(_count("shaving") == pieces_before, "undo takes the stroke's shavings back (%d left)" % _count("shaving"))

	# Oak, against the grain (planned): the tear-out comes off as chips.
	workshop.set_wood("board_oak")
	await _frames(2)
	_check(workshop.debris.pieces.is_empty(), "a new board sweeps the bench")
	workshop.set_setting("chisel", "angle", 33.0)
	workshop.set_setting("chisel", "depth", 0.3)
	var chips := 0
	var tries := 0
	var chips_made: int = workshop.debris.made.chip
	while chips == 0 and tries < 4: # the plan's seed is random: tear-out is likely, not certain
		await _stroke("chisel", _on_top(0.03 - 0.012 * tries, 0.02 - 0.012 * tries),
				[_on_top(-0.03 - 0.012 * tries, 0.02 - 0.012 * tries)], 20, true)
		chips = workshop.debris.made.chip - chips_made
		tries += 1
	print("debris: uphill in oak, %d chips in %d strokes" % [chips, tries])
	_check(chips > 0, "tear-out comes off as chips")
	if _count("chip") > 0:
		var chip: Dictionary = workshop.debris.pieces.filter(func(p): return p.kind == "chip").back()
		await _shot("debris_chips", chip.body.global_position)
	# They fade and go too.
	var waited := 0.0
	while _count("chip") > 0 and waited < 10.0:
		await _frames(1)
		waited += get_process_delta_time()
	_check(_count("chip") == 0, "the chips fade and go (%d left after %.1f s)" % [_count("chip"), waited])

	# A chop 2.5 mm from the end, the waste towards it: the chip between pops off.
	var chips_before: int = workshop.debris.made.chip
	workshop.set_setting("chisel", "angle", 90.0)
	var cam: Camera3D = workshop.camera
	var chop := _on_top(-0.0775, -0.02)
	workshop.select_tool("chisel")
	await _frames(8)
	await _idle()
	workshop.hover_screen(cam.unproject_position(chop))
	await _frames(2)
	workshop.lock(cam.unproject_position(chop))
	workshop.aim(cam.unproject_position(chop + Vector3(-0.01, 0, 0)))
	await _frames(1)
	var planned: Dictionary = workshop._plan
	workshop.press(cam.unproject_position(chop))
	workshop.unlock()
	workshop.board.flush()
	var popped: int = workshop.debris.made.chip - chips_before
	print("debris: a chop by the end (%s): %d chip(s)" % [", ".join(planned.get("warnings", [])), popped])
	_check("pops off" in planned.get("warnings", []) and popped == 1, "a chop by the end pops its chip off")
	workshop.set_setting("chisel", "angle", 30.0)

	workshop.sweep()
	await _frames(1)
	_check(workshop.debris.pieces.is_empty(), "Sweep clears the bench")
	print("debris: %s" % ("shavings and chips come away" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## Picks `tool` and makes a stroke from `start` through `path` (each leg in `steps` pointer
## moves, a frame apart), as workshop_drive.gd does; with `shot`, a close view of the tool
## at work two thirds of the way. Returns the live shaving's sample count after each move.
func _stroke(tool: String, start: Vector3, path: Array[Vector3], steps: int, planned: bool,
		shot := "") -> Array[int]:
	var growth: Array[int] = []
	workshop.select_tool(tool)
	await _frames(8)
	await _idle()
	var cam: Camera3D = workshop.camera
	var at := cam.unproject_position(start)
	workshop.hover_screen(at)
	await _frames(2)
	if planned:
		workshop.lock(at)
		workshop.aim(cam.unproject_position(path[0]))
		await _frames(1)
	workshop.press(at)
	var from := start
	for leg in path:
		for k in steps:
			workshop.drag_screen(cam.unproject_position(from.lerp(leg, float(k + 1) / steps)))
			await _frames(1)
			var live: int = workshop.debris.live_samples()
			if live > 0:
				growth.append(live)
				_feed_usec = maxi(_feed_usec, workshop.debris.feed_usec)
			if shot != "" and k == steps * 2 / 3:
				var edge: Vector3 = workshop.board.get_tool_pose().origin
				await _shot(shot, edge)
				workshop.camera.target = Vector3(0.0, 0.012, 0.0)
				workshop.camera.distance = 0.42
				workshop.camera.pitch = -0.8
				workshop.camera._apply()
		from = leg
	# A push tool follows at its working speed: until it has caught up (in game time, which
	# the engine slows down where frames take seconds).
	var waited := 0.0
	while workshop.lagging() and waited < 60.0:
		await _frames(1)
		waited += get_process_delta_time()
		var live: int = workshop.debris.live_samples()
		if live > 0:
			growth.append(live)
	if not workshop.is_engaged():
		_check(false, "%s did not engage at %s" % [tool, start])
	workshop.release()
	if planned:
		workshop.unlock()
	workshop.board.flush()
	await _frames(2)
	return growth


## The board's top surface over x0..x1, z0..z1 (world): how far down it is, looking straight
## down, every 0.5 mm.
func _depths(x0: float, x1: float, z0: float, z1: float) -> PackedFloat32Array:
	var out := PackedFloat32Array()
	var h := 0.0005
	for i in int((x1 - x0) / h):
		for j in int((z1 - z0) / h):
			var hit: Dictionary = workshop.board.raycast(Vector3(x0 + (i + 0.5) * h, 0.03, z0 + (j + 0.5) * h),
					Vector3.DOWN, 0.02)
			out.push_back(hit.get("distance", 0.0))
	return out


## The volume (mm^3) the surface went down by between two _depths().
func _lost(before: PackedFloat32Array, after: PackedFloat32Array) -> float:
	var v := 0.0
	for i in before.size():
		v += after[i] - before[i]
	return v * 0.5 * 0.5 * 1000.0 # m x mm^2 per column


## Rendered, with --out: a close view of `focus` saved as <out>/<name>.png.
func _shot(name: String, focus: Vector3) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	var cam = workshop.camera
	cam.target = focus
	cam.distance = 0.07
	cam.pitch = -0.45
	cam._apply()
	for i in 3:
		await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_out)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_out, name])


func _count(kind: String) -> int:
	return workshop.debris.pieces.filter(func(p): return p.kind == kind).size()


func _on_top(x: float, z: float) -> Vector3:
	return Vector3(x, 0.025, z)


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("debris: " + what)


## Until the board is idle (edits applied, refined): while it is busy, the pointer's rays
## answer with their last hit, and nothing can be locked on it.
func _idle() -> void:
	workshop.board.flush()
	for i in 1200:
		if not workshop.board.is_busy() and not workshop.board.get_stats().get("refine_pending", false):
			return
		await _frames(1)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame
