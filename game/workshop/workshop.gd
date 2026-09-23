extends Node3D

## The workshop: a board on a bench and three hand tools, every one an SDF body. Pick a
## tool (click it on the bench, or 1 / 2 / 3), point at the board to see where it will go,
## then hold the left button and drag to work it:
##   chisel         pares along the drag, at the depth set in the panel
##   saw            stroke it back and forth along its line: the kerf deepens as it goes
##   sanding block  takes the surface down wherever it rubs
## Q / E turn the tool, Esc drops the stroke in progress, Ctrl+Z / Ctrl+Shift+Z undo and
## redo. Right-drag orbits the camera, middle-drag pans, the wheel zooms.
##
## The world is in metres with y up. Bodies are in millimetres with z up: each body node is
## scaled by 0.001 and turned -90 degrees about x.

const MM := 0.001
const HOVER_LIFT := 15.0 # mm the tool floats above where it will engage
const OrbitCamera := preload("res://workshop/orbit_camera.gd")
const WorkshopUi := preload("res://workshop/workshop_ui.gd")

const TOOL_NAMES: Array[String] = ["chisel", "saw", "sanding_block"]
const TOOL_COLOURS := {
	"chisel": Color(1.0, 0.82, 0.25),
	"saw": Color(0.4, 0.85, 1.0),
	"sanding_block": Color(0.6, 1.0, 0.45),
}

## Per tool: chisel width and depth (mm); saw feed (mm deeper per mm of stroke); grit.
var settings := {
	"chisel": {"width": 12.0, "depth": 1.0},
	"saw": {"feed": 0.03},
	"sanding_block": {"grit": 120},
}
var wood := "board" ## board, board_oak or board_walnut

var board                 # SdfBody
var tools := {}           # name -> SdfBody
var current := ""         # the tool in hand, or ""
var yaw := 0.0            # the tool's turn about the surface normal, radians
var camera: Camera3D

var _rest := {}           # name -> Transform3D where each tool lies on the bench
var _outline: MeshInstance3D
var _ui
var _pointer := Vector2.ZERO
var _hit := {}            # the board under the pointer (SdfBody.raycast)
var _engaged := false
var _plane := Plane()
var _engage_pose := Transform3D()
var _engage_time := 0.0


func _ready() -> void:
	_build_world()
	board = _new_body()
	board.load_demo(wood)
	board.edited.connect(func(_stats): _ui.refresh())
	for tool in TOOL_NAMES:
		var body = _new_body()
		body.load_tool(tool, settings[tool])
		tools[tool] = body
	_place_rests()
	for tool in TOOL_NAMES:
		tools[tool].global_transform = _rest[tool]

	var orbit = OrbitCamera.new()
	orbit.fov = 40.0
	orbit.near = 0.005
	orbit.far = 20.0
	orbit.target = Vector3(0.0, 0.012, 0.0)
	orbit.distance = 0.42
	orbit.pitch = -0.8
	add_child(orbit)
	camera = orbit

	_outline = MeshInstance3D.new()
	_outline.mesh = ImmediateMesh.new()
	_outline.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var lines := StandardMaterial3D.new()
	lines.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	lines.vertex_color_use_as_albedo = true
	_outline.material_override = lines
	add_child(_outline)

	_ui = WorkshopUi.new()
	_ui.workshop = self
	add_child(_ui)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	select_tool("chisel")


# --- driving the tools (input handlers call these; tests do too) ----------------------

func select_tool(tool: String) -> void:
	if _engaged:
		release()
	current = tool
	_ui.refresh()


## Moves the pointer to a screen position and looks at what is under it.
func hover_screen(position: Vector2) -> void:
	_pointer = position
	if not _engaged:
		_hit = _surface_at(position)


## The board under a screen position: the point hit, with the normal of the surface around
## it at the scale of a tool (a plane through hits 4 mm either side), so that a kerf or a
## groove under the pointer does not turn the tool on its side.
func _surface_at(position: Vector2) -> Dictionary:
	var hit: Dictionary = board.raycast(camera.project_ray_origin(position), camera.project_ray_normal(position), 10.0)
	if hit.is_empty():
		return hit
	var pixel := 2.0 * float(hit.distance) * tan(deg_to_rad(camera.fov) * 0.5) / get_viewport().get_visible_rect().size.y
	var k: float = 4.0 * MM / maxf(pixel, 1e-6)
	var around: Array[Vector3] = []
	for offset in [Vector2(k, 0), Vector2(-k, 0), Vector2(0, k), Vector2(0, -k)]:
		var at: Vector2 = position + offset
		var h: Dictionary = board.raycast(camera.project_ray_origin(at), camera.project_ray_normal(at), 10.0)
		if h.is_empty():
			return hit # at the board's edge: the point's own normal
		around.append(h.position)
	var n := (around[0] - around[1]).cross(around[2] - around[3]).normalized()
	if n.dot(hit.normal) < 0.0:
		n = -n
	hit.normal = n
	return hit


## Left button down: picks up a tool from the bench, or engages the one in hand.
func press(position: Vector2) -> void:
	hover_screen(position)
	# A tool in front of the board (on the bench, usually) is picked up instead.
	var board_distance: float = _hit.distance if not _hit.is_empty() else INF
	var picked := _tool_under(position, board_distance)
	if picked != "":
		select_tool(picked)
		return
	if current == "" or _hit.is_empty():
		return
	var normal: Vector3 = _hit.normal
	_engage_pose = tools[current].global_transform
	_engage_time = Time.get_ticks_msec() / 1000.0
	_plane = Plane(normal, _hit.position)
	_engaged = board.begin_stroke(current, _hit.position, normal, _along(normal), settings[current])


## Pointer motion while engaged: the tool follows it on the plane it was engaged on.
func drag_screen(position: Vector2) -> void:
	_pointer = position
	if not _engaged:
		return
	var point = _plane.intersects_ray(camera.project_ray_origin(position), camera.project_ray_normal(position))
	if point != null:
		board.move_stroke(point)


## Left button up: the cut is finished and becomes one undo step.
func release() -> void:
	if _engaged:
		board.end_stroke()
		_engaged = false


func cancel() -> void:
	if _engaged:
		board.cancel_stroke()
		_engaged = false


func undo() -> void:
	cancel()
	board.undo()


func redo() -> void:
	cancel()
	board.redo()


func set_wood(choice: String) -> void:
	cancel()
	wood = choice
	board.load_demo(wood)
	_ui.refresh()


func reset_board() -> void:
	set_wood(wood)


func set_setting(tool: String, key: String, value) -> void:
	settings[tool][key] = value
	# The chisel's model has its width; the others' look does not change.
	if tool == "chisel" and key == "width":
		tools[tool].load_tool(tool, settings[tool])


func is_engaged() -> bool:
	return _engaged


# --- per frame ---------------------------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		if _engaged:
			drag_screen(event.position)
		else:
			hover_screen(event.position)
	elif event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			press(event.position)
		else:
			release()
	elif event is InputEventKey and event.pressed and not event.echo:
		var key := event as InputEventKey
		match key.keycode:
			KEY_1, KEY_2, KEY_3:
				select_tool(TOOL_NAMES[key.keycode - KEY_1])
			KEY_Q:
				yaw += deg_to_rad(15.0)
			KEY_E:
				yaw -= deg_to_rad(15.0)
			KEY_ESCAPE:
				cancel()
			KEY_Z:
				if key.ctrl_pressed and key.shift_pressed:
					redo()
				elif key.ctrl_pressed:
					undo()
			KEY_Y:
				if key.ctrl_pressed:
					redo()


func _process(delta: float) -> void:
	if not _engaged:
		# Keep looking under the pointer: the board may have changed under it.
		hover_screen(_pointer)
	var follow := 1.0 - exp(-delta * 18.0)
	for tool in TOOL_NAMES:
		var body = tools[tool]
		if tool == current and _engaged:
			# Drop onto the work quickly, then ride exactly where the stroke puts it.
			var t: float = clamp((Time.get_ticks_msec() / 1000.0 - _engage_time) / 0.08, 0.0, 1.0)
			body.global_transform = _engage_pose.interpolate_with(board.get_tool_pose(), t)
		elif tool == current:
			body.global_transform = body.global_transform.interpolate_with(_hover_pose(), follow)
		else:
			body.global_transform = body.global_transform.interpolate_with(_rest[tool], follow)
	_draw_outline()
	_ui.update_status()


## Where the tool in hand floats: over the board under the pointer, or over its middle.
func _hover_pose() -> Transform3D:
	if _hit.is_empty():
		var up := Vector3.UP
		return board.pose_at(board.global_position + up * 0.0125, up, _along(up), 60.0)
	return board.pose_at(_hit.position, _hit.normal, _along(_hit.normal), HOVER_LIFT)


## The tool's facing on a surface, turned by `yaw`: the chisel pushes away from the viewer;
## the saw and the block lie across the view, so the saw is seen side on and stroked left
## and right.
func _along(normal: Vector3) -> Vector3:
	var base := -camera.global_basis.z if current == "chisel" else camera.global_basis.x
	var along := base - normal * base.dot(normal)
	if along.length() < 1e-4:
		along = camera.global_basis.y - normal * camera.global_basis.y.dot(normal)
	return along.normalized().rotated(normal, yaw)


## The tool (other than the one in hand) nearer than `nearest` under a screen position.
func _tool_under(position: Vector2, nearest: float) -> String:
	var from := camera.project_ray_origin(position)
	var dir := camera.project_ray_normal(position)
	var best := ""
	for tool in TOOL_NAMES:
		if tool == current:
			continue
		var hit: Dictionary = tools[tool].raycast(from, dir, 10.0)
		if not hit.is_empty() and hit.distance < nearest and not hit.has("stale"):
			nearest = hit.distance
			best = tool
	return best


## The footprint of the tool in hand where it would engage: the chisel's edge and push
## direction, the saw's line, the sanding block's face.
func _draw_outline() -> void:
	var mesh: ImmediateMesh = _outline.mesh
	mesh.clear_surfaces()
	if current == "" or _engaged or _hit.is_empty():
		return
	var n: Vector3 = _hit.normal
	var p: Vector3 = _hit.position + n * 0.0003
	var a := _along(n)
	var s := n.cross(a)
	mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	mesh.surface_set_color(TOOL_COLOURS[current])
	var segments: Array[Vector3] = []
	match current:
		"chisel":
			var w: float = settings.chisel.width * 0.5 * MM
			segments = [p - s * w, p + s * w, p, p + a * 0.012, p + a * 0.012, p + a * 0.009 + s * 0.002,
					p + a * 0.012, p + a * 0.009 - s * 0.002]
		"saw":
			var l := 0.07
			segments = [p - a * l, p + a * l, p - a * l - s * 0.003, p - a * l + s * 0.003,
					p + a * l - s * 0.003, p + a * l + s * 0.003]
		"sanding_block":
			var hl := 0.035
			var hb := 0.020
			var c := [p - a * hl - s * hb, p + a * hl - s * hb, p + a * hl + s * hb, p - a * hl + s * hb]
			for i in 4:
				segments.append(c[i])
				segments.append(c[(i + 1) % 4])
	for v in segments:
		mesh.surface_add_vertex(v)
	mesh.surface_end()


# --- the world ---------------------------------------------------------------------------

func _new_body():
	var body = ClassDB.instantiate("SdfBody")
	add_child(body)
	# Body millimetres, z up -> world metres, y up; the board's underside on the bench top.
	body.transform = Transform3D(Basis(Vector3.RIGHT, -PI / 2) * Basis.from_scale(Vector3.ONE * MM),
			Vector3(0.0, 0.0125, 0.0))
	return body


## A tool model's frame (see core tools.h: z away from the work, x its working direction)
## laid out in the world, `lift` metres up.
func _frame(origin: Vector3, z_axis: Vector3, x_axis: Vector3) -> Transform3D:
	var basis := Basis(x_axis, z_axis.cross(x_axis), z_axis)
	return Transform3D(basis * Basis.from_scale(Vector3.ONE * MM), origin)


func _place_rests() -> void:
	# In front of the board, lying as they would: the chisel with its blade flat, the saw
	# on its side, the block on its paper.
	var chisel := _frame(Vector3(-0.07, 0.0115, 0.1), Vector3.UP, Vector3.LEFT)
	chisel.basis = chisel.basis * Basis(Vector3(0, 1, 0), -deg_to_rad(20.0))
	_rest["chisel"] = chisel
	var saw := Transform3D(Basis(Vector3.RIGHT, Vector3.UP, Vector3.BACK) * Basis.from_scale(Vector3.ONE * MM),
			Vector3(0.0, 0.011, -0.12))
	_rest["saw"] = saw
	_rest["sanding_block"] = _frame(Vector3(0.1, 0.0, 0.1), Vector3.UP, Vector3.RIGHT)


func _build_world() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.16, 0.17, 0.19)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.53, 0.6)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.light_color = Color(1.0, 0.96, 0.9)
	sun.light_energy = 0.9
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 3.0
	add_child(sun)
	sun.look_at_from_position(Vector3.ZERO, Vector3(0.45, -0.75, -0.5), Vector3.UP)

	# A bench: a thick top on four legs, the top's surface at y = 0.
	var oak := StandardMaterial3D.new()
	oak.albedo_color = Color(0.3, 0.2, 0.13)
	oak.roughness = 0.8
	var top := MeshInstance3D.new()
	var slab := BoxMesh.new()
	slab.size = Vector3(0.9, 0.05, 0.55)
	top.mesh = slab
	top.material_override = oak
	top.position = Vector3(0.0, -0.025, 0.0)
	add_child(top)
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			var leg := MeshInstance3D.new()
			var post := BoxMesh.new()
			post.size = Vector3(0.06, 0.8, 0.06)
			leg.mesh = post
			leg.material_override = oak
			leg.position = Vector3(sx * 0.38, -0.45, sz * 0.22)
			add_child(leg)
	var ground := MeshInstance3D.new()
	var sheet := PlaneMesh.new()
	sheet.size = Vector2(6.0, 6.0)
	ground.mesh = sheet
	var grey := StandardMaterial3D.new()
	grey.albedo_color = Color(0.32, 0.32, 0.33)
	ground.material_override = grey
	ground.position = Vector3(0.0, -0.85, 0.0)
	add_child(ground)
