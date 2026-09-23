extends "res://tests/harness.gd"

## Uses the workshop as a person would, through its own pointer methods: pares with the
## chisel, saws a kerf, sands a patch, then undoes the sanding. Prints what each did and,
## when rendering, saves out/workshop_<tool>.png after each (--out=<dir>).

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
	print("workshop drive: chisel %d edits, saw %d, sanding %d; after undo %d edits in %d strokes; last update %.0f ms, upload %.0f ms" % [
			chisel_edits, saw_edits, sand_edits, after_undo, stats.steps, stats.update_ms, stats.upload_ms])
	if chisel_edits < 3 or saw_edits < 3 or sand_edits < 1 or after_undo != chisel_edits + saw_edits:
		push_error("workshop drive: a tool made no cut, or undo did not take the sanding back")
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
