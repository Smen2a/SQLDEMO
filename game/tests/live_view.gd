extends "res://tests/harness.gd"

## Renders one SdfBody demo through the Live raymarch path. User args (after `--`):
##   --demo=<name>        SdfBody.load_demo name (default carved_panel)
##   --view=<v>           shaded | normals | steps | albedo (default shaded)
##   --eye=x,y,z --target=x,y,z --fov=deg   camera in body millimetres, z up (default:
##                        the demo's own camera, as sdf_render uses)
##   --scale=<s>          node scale, e.g. 0.001 for a metre world (default 1)
##   --ortho=<height>     orthographic camera showing <height> millimetres vertically
##   --floor              add a floor under the body and a bar through it, to check
##                        shadows and depth composition against ordinary meshes
## plus the harness's --screenshot and --frames.

const LIGHT_DIR := Vector3(-0.55, -0.40, 0.73)
const LIGHT_COLOUR := Color(1.00, 0.95, 0.86)
const SKY_COLOUR := Color(0.42, 0.47, 0.55)


func _ready() -> void:
	var scale := float(user_arg("--scale", "1"))
	var view := user_arg("--view", "shaded")

	var body = ClassDB.instantiate("SdfBody")
	add_child(body)
	body.scale = Vector3.ONE * scale
	if not body.load_demo(user_arg("--demo", "carved_panel")):
		get_tree().quit(1)
		return
	body.debug_view = ["shaded", "normals", "steps", "albedo"].find(view)
	print("stats: ", body.get_stats())

	var camera := Camera3D.new()
	add_child(camera)
	var framing: Dictionary = body.get_demo_camera()
	var eye := _vec(user_arg("--eye", ""), framing.eye) * scale
	var target := _vec(user_arg("--target", ""), framing.target) * scale
	camera.fov = float(user_arg("--fov", str(framing.fov)))
	camera.near = 1.0 * scale
	camera.far = 4000.0 * scale
	camera.look_at_from_position(eye, target, framing.up)
	var ortho := float(user_arg("--ortho", "0"))
	if ortho > 0.0:
		camera.projection = Camera3D.PROJECTION_ORTHOGONAL
		camera.size = ortho * scale

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	if view == "shaded":
		env.background_color = Color(0.27, 0.285, 0.31)
		env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		env.ambient_light_color = SKY_COLOUR
		env.ambient_light_energy = 1.0
		var sun := DirectionalLight3D.new()
		add_child(sun)
		sun.light_color = LIGHT_COLOUR
		sun.shadow_enabled = true
		sun.directional_shadow_max_distance = 400.0 * scale
		sun.look_at_from_position(Vector3.ZERO, -LIGHT_DIR, Vector3(0, 0, 1))
	else:
		env.background_color = Color.BLACK
		env.ambient_light_source = Environment.AMBIENT_SOURCE_DISABLED
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	if "--floor" in OS.get_cmdline_user_args():
		_add_floor(body.get_body_bounds(), scale)

	capture_and_quit()


## A floor just under the body (receives its shadow) and a bar pushed half into it (the
## raymarched depth must cut the bar exactly where it enters the surface).
func _add_floor(bounds: AABB, scale: float) -> void:
	var grey := StandardMaterial3D.new()
	grey.albedo_color = Color(0.55, 0.55, 0.52)
	var floor_mesh := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(400, 400) * scale
	floor_mesh.mesh = plane
	floor_mesh.material_override = grey
	floor_mesh.rotation = Vector3(PI / 2, 0, 0)  # PlaneMesh faces +y; the body's up is +z
	floor_mesh.position = Vector3(0, 0, bounds.position.z - 2.0) * scale
	add_child(floor_mesh)

	var red := StandardMaterial3D.new()
	red.albedo_color = Color(0.8, 0.15, 0.1)
	var bar := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(bounds.size.x * 1.4, 6, 6) * scale
	bar.mesh = box
	bar.material_override = red
	# Centred 3mm under the body's top, so its upper half stands clear of the surface.
	bar.position = (bounds.get_center() + Vector3(0, 0, bounds.size.z * 0.5 - 3.0)) * scale
	add_child(bar)


func _vec(s: String, default: Vector3) -> Vector3:
	if s == "":
		return default
	var p := s.split(",")
	return Vector3(float(p[0]), float(p[1]), float(p[2]))
