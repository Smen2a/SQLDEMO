extends "res://tests/harness.gd"

## Uses the workshop as a person would, through its own pointer methods: pares with the
## chisel, saws a kerf, sands a patch with the block, rounds an arris over with the sponge,
## then undoes the sponge's work; then saws a strip right off the board, watches it come
## away, and undoes that to put it back. Prints what each did and, when rendering, saves
## out/workshop_<tool>.png after each (--out=<dir>).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _out := ""


func _ready() -> void:
	# Never hang a test run: give up after a generous while.
	get_tree().create_timer(float(user_arg("--timeout", "900"))).timeout.connect(func():
		push_error("workshop drive: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	var out := user_arg("--out", "")
	_out = out
	await _frames(3)

	# Chisel: a 50 mm paring cut across the board, 1.5 mm deep.
	workshop.set_setting("chisel", "depth", 1.5)
	await _stroke("chisel", _on_top(-0.045, 0.02), [_on_top(0.005, 0.02)], 12)
	var chisel_edits: int = workshop.board.get_stats().edits
	await _shot(out, "chisel")

	# Saw: set on a line across the board, stroked back and forth 12 times.
	workshop.select_tool("saw")
	workshop.set_setting("saw", "feed", 0.012)
	var saw_path: Array[Vector3] = []
	for i in 12:
		saw_path.append(_on_top(-0.03 if i % 2 == 0 else 0.03, 0.0))
	await _stroke("saw", _on_top(0.0, 0.0), saw_path, 2)
	var saw_edits: int = workshop.board.get_stats().edits - chisel_edits
	await _shot(out, "saw")

	# Sanding block: rubbed round a patch.
	var rub: Array[Vector3] = []
	for i in 16:
		var a := float(i) * 0.9
		rub.append(_on_top(-0.03 + 0.02 * cos(a), -0.025 + 0.012 * sin(a)))
	await _stroke("sanding_block", _on_top(-0.03, -0.025), rub, 2)
	var sand_edits: int = workshop.board.get_stats().edits - chisel_edits - saw_edits
	await _shot(out, "sand")

	# Sanding sponge: rubbed along the board's front top arris, back and forth.
	var arris: Array[Vector3] = []
	for i in 8:
		arris.append(Vector3(0.035 if i % 2 == 0 else -0.035, 0.025, 0.05))
	await _stroke("sanding_sponge", Vector3(-0.035, 0.025, 0.05), arris, 4)
	var sponge_edits: int = workshop.board.get_stats().edits - chisel_edits - saw_edits - sand_edits
	await _shot(out, "sponge")

	# The board with all three cuts, every tool back on the bench.
	workshop.select_tool("")
	workshop.camera.distance = 0.26
	workshop.camera.pitch = -0.95
	workshop.camera._apply()
	await _frames(4)
	await _shot(out, "result")

	workshop.undo()
	workshop.board.flush()
	var after_undo: int = workshop.board.get_stats().edits
	var stats: Dictionary = workshop.board.get_stats()
	print("workshop drive: chisel %d edits, saw %d, sanding block %d, sponge %d; after undo %d edits in %d strokes; last update %.0f ms, upload %.0f ms" % [
			chisel_edits, saw_edits, sand_edits, sponge_edits, after_undo, stats.steps, stats.update_ms, stats.upload_ms])
	# Strokes are previewed and committed merged: the chisel's ramp, run and lift-out, the
	# saw's kerf, the block's pass. The sponge's work is one smoothing layer.
	if chisel_edits != 3 or saw_edits != 1 or sand_edits != 1 or sponge_edits != 1 or \
			after_undo != chisel_edits + saw_edits + sand_edits:
		push_error("workshop drive: a tool made no cut or not its merged one, or undo did not take the sponge's work back")

	# Saw through: a kerf along the board 20 mm in from its front edge, right through. The
	# strip comes away as a rigid body and slides off the kerf; undo puts it back.
	var steps_before: int = workshop.board.get_stats().steps
	workshop.set_setting("saw", "feed", 0.1)
	var through: Array[Vector3] = []
	for i in 12:
		through.append(_on_top(-0.03 if i % 2 == 0 else 0.03, 0.03))
	await _stroke("saw", _on_top(0.0, 0.03), through, 2)
	for i in 30: # the split follows the commit that found the parts apart
		if not workshop.offcuts.is_empty():
			break
		await _frames(1)
	if workshop.offcuts.is_empty():
		push_error("workshop drive: the saw went through but the board did not come apart")
	else:
		var offcut: RigidBody3D = workshop.offcuts.back().body
		var start: Vector3 = workshop.offcuts.back().spawn.origin # (frames may span several physics steps)
		for i in 60: # a second of physics, however slowly frames render
			await get_tree().physics_frame
		var moved := offcut.global_position.distance_to(start) * 1000.0
		workshop.select_tool("")
		await _frames(4)
		await _shot(out, "sawn_through")
		var mass := offcut.mass
		workshop.undo()
		await _frames(2)
		var steps_after: int = workshop.board.get_stats().steps
		var split_ms: float = workshop.board.get_stats().get("split_ms", 0.0)
		print("workshop drive: sawn through in %.1f ms of checking and measuring (worker), split in %.1f ms (main thread); the offcut (%.0f g) moved %.1f mm in 1 s; after undo %d piece(s), %d steps (from %d)" % [
				workshop.board.get_stats().get("separation_ms", 0.0), split_ms, mass * 1000.0, moved,
				workshop.offcuts.size() + 1, steps_after, steps_before])
		# The kerf stays, one step; the split's half-space is gone with the offcut.
		if moved < 2.0 or not workshop.offcuts.is_empty() or steps_after != steps_before + 1:
			push_error("workshop drive: the offcut did not come away, or undo did not rejoin it")
		# The split takes what the worker measured: it reads nothing from the body, and waits
		# on nothing (it once stalled the frame for half a second).
		if split_ms > 50.0:
			push_error("workshop drive: the split held up the main thread for %.0f ms" % split_ms)
	get_tree().quit()


## A point on the board's top face (world metres; the top is 25 mm above the bench).
func _on_top(x: float, z: float) -> Vector3:
	return Vector3(x, 0.025, z)


## Picks `tool`, engages it at `start`, drags it through `path` (each leg in `steps`
## pointer moves, a frame apart) and lets go; then waits for the edits to land.
func _stroke(tool: String, start: Vector3, path: Array[Vector3], steps: int) -> void:
	workshop.select_tool(tool)
	await _frames(8) # the last tool flies back to the bench
	var cam: Camera3D = workshop.camera
	var at := cam.unproject_position(start)
	workshop.hover_screen(at)
	await _frames(2)
	workshop.press(at)
	if not workshop.is_engaged():
		push_error("workshop drive: %s did not engage at %s" % [tool, at])
		return
	var from := start
	for i in path.size():
		for k in steps:
			workshop.drag_screen(cam.unproject_position(from.lerp(path[i], float(k + 1) / steps)))
			await _frames(1)
		from = path[i]
		if i == path.size() / 2:
			# Mid-stroke, with the tool at work.
			workshop.board.flush()
			await _shot(_out, tool + "_working")
	workshop.release()
	workshop.board.flush()
	await _frames(2)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _shot(out: String, tool: String) -> void:
	if out == "" or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	await RenderingServer.frame_post_draw
	_save_screenshot("%s/workshop_%s.png" % [out, tool])
