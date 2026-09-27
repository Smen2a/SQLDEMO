extends "res://tests/harness.gd"

## Dust (workshop/dust.gd), driven through the workshop's own pointer methods:
## - a saw kerf across the board: as much sawdust as the kerf's width times its depth times
##   the board's width; its grains land and go, and most of its wood is heaped in a pile or
##   two on the bench beyond each of the kerf's ends;
## - the sanding block rubbed round a patch: its fine dust lands on the board's face and
##   goes (little of it piles);
## - the sponge along an arris: its dust arrives, the last of it after the stroke ends;
## - undo takes each stroke's dust back, piles too; Sweep clears the piles.
## Prints what came off and what the grains cost a frame. Rendered, with --out=<dir>, saves
## <dir>/dust_sawdust.png (the pile at a kerf's end) and dust_sanding.png (the puff).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false
var _out := ""
var _update_usec := 0
var _feed_usec := 0
var _feed_total := 0
var _feed_frames := 0


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "600"))).timeout.connect(func():
		push_error("dust: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	_out = user_arg("--out", "")
	await _frames(3)
	var dust = workshop.debris.dust

	# Saw: a kerf across the board at x = 20 mm, 10 strokes of 60 mm at 0.01 mm a mm.
	workshop.set_setting("saw", "feed", 0.01)
	var strokes: Array[Vector3] = []
	for i in 10:
		strokes.append(_on_top(0.02, 0.03 if i % 2 == 0 else -0.03))
	await _stroke("saw", _on_top(0.02, 0.0), strokes, 6, true)
	await _settle()
	var hit: Dictionary = workshop.board.raycast(Vector3(0.02, 0.05, 0.0), Vector3.DOWN, 0.1)
	var depth: float = (hit.position.y if not hit.is_empty() else 0.025)
	var kerf := 0.8 * (25.0 - depth * 1000.0) * 100.0 # mm^3: kerf x depth x the board's width
	var sawdust: float = dust.thrown
	# On the bench beyond each side of the board (its sides are 50 mm either side of z = 0).
	var ahead: Dictionary = dust.piles_in(AABB(Vector3(-0.03, -0.001, 0.051), Vector3(0.1, 0.02, 0.1)))
	var behind: Dictionary = dust.piles_in(AABB(Vector3(-0.03, -0.001, -0.151), Vector3(0.1, 0.02, 0.1)))
	var ends: float = ahead.volume + behind.volume
	var top: float = maxf(ahead.top, behind.top)
	print("dust: a kerf %.1f mm deep across the board: %.0f mm^3 of sawdust (kerf x depth x width: %.0f); %.0f%% of it in %d + %d piles on the bench beyond the kerf's ends, heaped %.1f mm high (%d piles in all); throwing it cost %d us a frame (at most %d), its grains at most %d us" % [
			25.0 - depth * 1000.0, sawdust, kerf, 100.0 * ends / maxf(sawdust, 1e-6), ahead.count, behind.count,
			top * 1000.0, dust.piles.size(), _feed_total / maxi(_feed_frames, 1), _feed_usec, _update_usec])
	_check(sawdust > 100.0 and absf(sawdust - kerf) < 0.15 * kerf, "the sawdust is the kerf taken")
	_check(dust.flying() == 0, "every grain has landed and gone (%d left)" % dust.flying())
	_check(ends > 0.5 * sawdust, "it leaves at the kerf's ends and piles up beyond them")
	_check(ahead.count <= 2 and behind.count <= 2 and ahead.count + behind.count >= 1,
			"in a pile or two at each end (%d, %d)" % [ahead.count, behind.count])
	_check(top > 0.001, "heaped over a millimetre")
	await _shot("dust_sawdust", Vector3(0.02, 0.0, 0.07))

	# Sanding block: rubbed round a patch; its fine dust lands on the face and goes.
	var before: float = dust.thrown
	var piled_before: float = dust.piled()
	var rub: Array[Vector3] = []
	for i in 16:
		var a := float(i) * 0.9
		rub.append(_on_top(-0.03 + 0.02 * cos(a), -0.02 + 0.012 * sin(a)))
	workshop.pace = 8.0 # (a real rate takes a minute's rubbing to make a cubic millimetre)
	await _stroke("sanding_block", _on_top(-0.03, -0.02), rub, 2, false)
	workshop.pace = 1.0
	await _shot("dust_sanding", _on_top(-0.03, -0.02)) # (the last of its puffs)
	await _settle()
	var sanded: float = dust.thrown - before
	var piled: float = dust.piled() - piled_before
	print("dust: the sanding block: %.1f mm^3 of fine dust, %.0f%% of it piled on the ground" % [sanded,
			100.0 * piled / maxf(sanded, 1e-6)])
	_check(sanded > 1.0, "the sanding block throws dust")
	_check(dust.flying() == 0, "it goes as it lands (%d left)" % dust.flying())
	_check(piled < 0.5 * sanded, "landing on the face, little of it piles")

	# Sponge: along the front top arris; the last of its dust lands after the stroke ends.
	var before_sponge: float = dust.thrown
	var arris: Array[Vector3] = []
	for i in 6:
		arris.append(Vector3(0.035 if i % 2 == 0 else -0.035, 0.025, 0.05))
	await _stroke("sanding_sponge", Vector3(-0.035, 0.025, 0.05), arris, 4, false)
	await _settle()
	var sponged: float = dust.thrown - before_sponge
	print("dust: the sponge along an arris: %.2f mm^3" % sponged)
	_check(sponged > 0.05, "the sponge's dust arrives")

	# Undo takes each stroke's dust back, and the saw's piles with it.
	workshop.undo()
	workshop.board.flush()
	await _frames(1)
	_check(absf(dust.thrown - before_sponge) < 1e-3, "undo takes the sponge's dust back")
	workshop.undo()
	workshop.board.flush()
	await _frames(1)
	_check(absf(dust.thrown - before) < 1e-3, "and the sanding block's")
	workshop.undo()
	workshop.board.flush()
	await _frames(1)
	_check(dust.thrown < 1e-3 and dust.piles.is_empty(), "and the saw's, and its piles (%d left)" % dust.piles.size())

	# Sweep clears the piles.
	strokes.resize(4)
	await _stroke("saw", _on_top(0.02, 0.0), strokes, 6, true)
	await _settle()
	var made: int = dust.piles.size()
	workshop.sweep()
	await _frames(1)
	_check(made > 0 and dust.piles.is_empty() and dust.thrown == 0.0, "Sweep clears the piles (%d made)" % made)
	print("dust: %s" % ("the dust comes away and settles" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## As debris.gd's.
func _stroke(tool: String, start: Vector3, path: Array[Vector3], steps: int, planned: bool) -> void:
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
			_update_usec = maxi(_update_usec, workshop.debris.dust.update_usec)
			_feed_usec = maxi(_feed_usec, workshop.debris.feed_usec)
			if workshop.debris.dust.flying() > 0:
				_feed_total += workshop.debris.feed_usec
				_feed_frames += 1
		await catch_up(workshop) # (it follows at its working speed)
		from = leg
	if not workshop.is_engaged():
		_check(false, "%s did not engage at %s" % [tool, start])
	workshop.release()
	if planned:
		workshop.unlock()
	workshop.board.flush()
	await _frames(2)


## A second and a half for the grains to land and go (and the sponge's last work to report),
## or as long as they take: in game time, which the engine slows where frames take seconds.
func _settle() -> void:
	var waited := 0.0
	while waited < 1.5 or (workshop.debris.dust.flying() > 0 and waited < 10.0):
		await _frames(1)
		waited += get_process_delta_time()
		_update_usec = maxi(_update_usec, workshop.debris.dust.update_usec)


## Rendered, with --out: a close view of `focus` saved as <out>/<name>.png.
func _shot(name: String, focus: Vector3) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	var cam = workshop.camera
	cam.target = focus
	cam.distance = 0.09
	cam.pitch = -0.55
	cam._apply()
	var shown := {}
	for tool in workshop.tools:
		shown[tool] = workshop.tools[tool].visible
		workshop.tools[tool].visible = false # (the last one may still be on its way back)
	for i in 3:
		await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_out)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_out, name])
	for tool in shown:
		workshop.tools[tool].visible = shown[tool]
	cam.target = Vector3(0.0, 0.012, 0.0)
	cam.distance = 0.42
	cam.pitch = -0.8
	cam._apply()


func _on_top(x: float, z: float) -> Vector3:
	return Vector3(x, 0.025, z)


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("dust: " + what)


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
