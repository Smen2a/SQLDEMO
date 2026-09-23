extends "res://tests/harness.gd"

## Renders demo bodies through the Live raymarch path, one screenshot per case, for
## tools/parity.sh to compare with the CPU reference renderer. User args (after `--`):
##   --cases=<demo>:<view>,...   view is normals or albedo (unlit, so renderer lighting
##                               does not enter the comparison)
##   --out=<dir>                 writes <dir>/<demo>_<view>.png

const VIEWS := {"normals": 1, "albedo": 3}


func _ready() -> void:
	if DisplayServer.get_name() == "headless":
		printerr("parity needs a renderer: run it with tools/run.sh --render")
		get_tree().quit(1)
		return
	var out := user_arg("--out", "")
	var cases := user_arg("--cases", "").split(",", false)
	DirAccess.make_dir_recursive_absolute(out)

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color.BLACK
	env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)
	var camera := Camera3D.new()
	camera.near = 1.0
	camera.far = 4000.0
	add_child(camera)
	var body = ClassDB.instantiate("SdfBody")
	add_child(body)

	var failed := false
	for c in cases:
		var parts: PackedStringArray = c.split(":")
		if parts.size() != 2 or not VIEWS.has(parts[1]) or not body.load_demo(parts[0]):
			printerr("bad parity case '%s'" % c)
			failed = true
			continue
		body.debug_view = VIEWS[parts[1]]
		var framing: Dictionary = body.get_demo_camera()
		camera.fov = framing.fov
		camera.look_at_from_position(framing.eye, framing.target, framing.up)
		for i in 3:
			await RenderingServer.frame_post_draw
		var path := "%s/%s_%s.png" % [out, parts[0], parts[1]]
		if get_viewport().get_texture().get_image().save_png(path) != OK:
			printerr("could not save %s" % path)
			failed = true
		else:
			print("rendered ", path)
	get_tree().quit(1 if failed else 0)
