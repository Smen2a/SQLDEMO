extends "res://tests/harness.gd"

## A stroke previewed by the shader (SdfBody.stroke_preview: its edits drawn on top of the
## body while the tool moves) must look like the same stroke once it is committed. For each
## tool, on a fresh board, strokes through SdfBody's own calls and checks that previewing
## draws an overlay and applies nothing to the body, lifts the tool off, and checks that the
## commit lands as the edits expected and the overlay empties. Rendered (tools/run.sh
## --render), it also renders the preview and the commit and compares the two as
## tools/parity.sh compares images; headless, it checks only the edits. User args (after `--`):
##   --out=<dir>   writes <dir>/preview_<tool>_<view>_{preview,committed,diff}.png

const VIEWS := {"normals": 1, "albedo": 3, "shaded": 0}
# Mean channel difference (0..255), and the fraction of pixels differing by more than
# THRESHOLD in some channel.
const MAX_MEAN := 0.5
const MAX_OVER := 0.002
const THRESHOLD := 24
const TOP := 12.5 # the board's top face (body millimetres, z up)

var body
var _failed := false
var _render := DisplayServer.get_name() != "headless"


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "900"))).timeout.connect(func():
		push_error("stroke preview: timed out")
		get_tree().quit(1))
	var out := user_arg("--out", "")
	if _render and out != "":
		DirAccess.make_dir_recursive_absolute(out)

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.27, 0.285, 0.31)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.42, 0.47, 0.55)
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var sun := DirectionalLight3D.new()
	add_child(sun)
	sun.look_at_from_position(Vector3.ZERO, Vector3(-0.55, -0.40, -0.73), Vector3(0, 0, 1))
	var camera := Camera3D.new()
	camera.near = 1.0
	camera.far = 4000.0
	camera.fov = 38.0
	add_child(camera)
	camera.look_at_from_position(Vector3(30, -120, 120), Vector3(-5, 0, 0), Vector3(0, 0, 1))
	body = ClassDB.instantiate("SdfBody")
	add_child(body)

	# Chisel: pared along the board (as deep as a hand pushes it through ash) and off its
	# end, where the lift-out it adds on release (not previewed: the chisel is still in the
	# wood) cuts nothing.
	var push: Array[Vector3] = []
	for i in 65:
		push.append(Vector3(-44.0 + 2.0 * i, -20.0, TOP))
	await _check(out, "chisel", Vector3(-45, -20, TOP), Vector3(1, 0, 0),
			{"variant": "bench_12", "depth": 1.5, "angle": 30.0, "length": 130.0}, push, 3)

	# Saw: across the board, 12 strokes of 30 mm.
	var strokes: Array[Vector3] = []
	for i in 12:
		strokes.append(Vector3(30.0, 15.0 if i % 2 == 0 else -15.0, TOP))
	await _check(out, "saw", Vector3(30, 0, TOP), Vector3(0, 1, 0), {"feed": 0.03}, strokes, 1)

	# Sanding block, coarse grit, rubbed round a patch.
	var rub: Array[Vector3] = []
	for i in 30:
		var a := float(i) * 0.5
		rub.append(Vector3(-30.0 + 15.0 * cos(a), 20.0 + 6.0 * sin(a), TOP))
	await _check(out, "sanding_block", Vector3(-30, 20, TOP), Vector3(1, 0, 0), {"grit": 80}, rub, 1)

	if not _failed:
		print("stroke preview: every tool's %s" % ("preview matches its commit" if _render else
				"preview commits as previewed (not rendered)"))
	get_tree().quit(1 if _failed else 0)


func _check(out: String, tool: String, contact: Vector3, along: Vector3, settings: Dictionary,
		path: Array[Vector3], merged_edits: int) -> void:
	body.load_demo("board")
	if not body.begin_stroke(tool, contact, Vector3(0, 0, 1), along, settings):
		_fail("%s: did not engage" % tool)
		return
	for p in path:
		body.move_stroke(p)
	var stats: Dictionary = body.get_stats()
	var drawn: int = stats.overlay_edits
	if stats.edits != 0 or body.is_busy() or drawn == 0:
		_fail("%s: previewing applied edits (%d) or drew none (%d)" % [tool, stats.edits, drawn])
	var preview := {}
	if _render:
		preview = await _render_views()
	body.end_stroke()
	body.flush()
	stats = body.get_stats()
	if stats.edits != merged_edits or stats.overlay_edits != 0:
		_fail("%s: committed %d edits (expected %d), %d left in the overlay" % [
				tool, stats.edits, merged_edits, stats.overlay_edits])
	if not _render:
		print("%-14s %d edits previewed, %d committed" % [tool, drawn, stats.edits])
		return
	var committed := await _render_views()
	for view in VIEWS:
		var a: Image = preview[view]
		var b: Image = committed[view]
		var result := _compare(a, b)
		if out != "":
			var name := "%s/preview_%s_%s" % [out, tool, view]
			a.save_png(name + "_preview.png")
			b.save_png(name + "_committed.png")
			result.diff.save_png(name + "_diff.png")
		var ok: bool = result.mean <= MAX_MEAN and result.over <= MAX_OVER
		print("%s  %-14s %-8s mean %.3f (max %.1f), %.3f%% over %d (max %.1f%%)" % [
				"ok  " if ok else "FAIL", tool, view, result.mean, MAX_MEAN, result.over * 100.0, THRESHOLD,
				MAX_OVER * 100.0])
		if not ok:
			_fail("%s: preview and commit differ in the %s view" % [tool, view])


func _render_views() -> Dictionary:
	var images := {}
	for view in VIEWS:
		body.debug_view = VIEWS[view]
		for i in 3:
			await RenderingServer.frame_post_draw
		var image: Image = get_viewport().get_texture().get_image()
		image.convert(Image.FORMAT_RGB8)
		images[view] = image
	return images


## As sdf::compare (native/src/core/eval/image.h), plus a difference image amplified 4x.
func _compare(a: Image, b: Image) -> Dictionary:
	var da := a.get_data()
	var db := b.get_data()
	var diff := PackedByteArray()
	diff.resize(da.size())
	var total := 0
	var over := 0
	for i in range(0, da.size(), 3):
		var worst := 0
		for c in 3:
			var d := absi(da[i + c] - db[i + c])
			total += d
			worst = maxi(worst, d)
			diff[i + c] = mini(255, 4 * d)
		if worst > THRESHOLD:
			over += 1
	var pixels := da.size() / 3
	return {
		"mean": float(total) / float(da.size()),
		"over": float(over) / float(pixels),
		"diff": Image.create_from_data(a.get_width(), a.get_height(), false, Image.FORMAT_RGB8, diff),
	}


func _fail(message: String) -> void:
	_failed = true
	push_error("stroke preview: " + message)
