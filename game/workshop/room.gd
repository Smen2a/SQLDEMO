extends Node3D

## The workshop's room, built in code: the light, a floor and walls, the workbench with its
## vise, a side table and the lumber rack. The world is in metres with y up. The bench top is
## at y = 0 with its middle at the origin, where the vise holds the work; the floor is at
## y = -0.85. Statics are on physics layer 1; the floor, the bench and the table are "ground"
## (dust piles up on them: dust.gd).

const FLOOR := -0.85
const HEIGHT := 2.6 # floor to ceiling
## The room's inside: from the wall behind the bench (z = BACK) to the one with the door.
const LEFT := -3.0
const RIGHT := 3.0
const BACK := -1.2
const FRONT := 4.0
const WALL := 0.1 # thick
const BENCH_TOP := Vector3(0.9, 0.05, 0.55)
## The vise: the part of the bench top between its jaws, where a piece let go is clamped.
const VISE := Vector2(0.36, 0.3) # x by z
const JAW := Vector3(0.24, 0.035, 0.022) # a jaw's size (along the bench, up, across)
## The side table, to the bench's right, and the lumber rack against the left wall.
const TABLE_AT := Vector3(1.3, -0.1, 0.25) # the middle of its top
const TABLE_TOP := Vector3(0.6, 0.04, 0.6)
const RACK_AT := Vector3(-2.75, FLOOR, 1.0) # the middle of its foot, by the wall
const RACK_SHELVES := [0.45, 0.9, 1.35] # heights above the floor
const RACK_SIZE := Vector3(0.4, 1.6, 0.9) # deep (x), high, long (z)

var bench: StaticBody3D
var table: StaticBody3D
var jaws: Array[MeshInstance3D] = []

var _oak := _material(Color(0.3, 0.2, 0.13), 0.8)
var _pine := _material(Color(0.62, 0.48, 0.3), 0.75)
var _steel := _material(Color(0.45, 0.46, 0.48), 0.4, 0.8)


func _ready() -> void:
	_light()
	_shell()
	_bench()
	_table()
	_rack()


## Whether a point (world) is on the bench between the vise's jaws.
func in_vise(point: Vector3) -> bool:
	return absf(point.x) <= 0.5 * VISE.x and absf(point.z) <= 0.5 * VISE.y and point.y > -0.01 and point.y < 0.2


## The jaws closed on a piece `width` metres across the bench (or open, 0).
func close_jaws(width: float) -> void:
	var gap := clampf(width, 0.03, VISE.y - 2.0 * JAW.z) if width > 0.0 else VISE.y - 2.0 * JAW.z
	for i in jaws.size():
		var side := -1.0 if i == 0 else 1.0
		jaws[i].position = Vector3(0.0, 0.5 * JAW.y, side * (0.5 * gap + 0.5 * JAW.z))


func _light() -> void:
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
	# Daylight through the window, as it fell on the bench before there was a room: the walls
	# and ceiling cast no shadow, so it still reaches the bench.
	var sun := DirectionalLight3D.new()
	sun.light_color = Color(1.0, 0.96, 0.9)
	sun.light_energy = 0.9
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 3.0
	add_child(sun)
	sun.look_at_from_position(Vector3.ZERO, Vector3(0.45, -0.75, -0.5), Vector3.UP)
	for at in [Vector3(-1.2, FLOOR + HEIGHT - 0.15, 1.2), Vector3(1.2, FLOOR + HEIGHT - 0.15, 1.2)]:
		var lamp := OmniLight3D.new()
		lamp.position = at
		lamp.light_color = Color(1.0, 0.92, 0.8)
		lamp.light_energy = 0.6
		lamp.omni_range = 5.0
		add_child(lamp)


## The floor, four walls (a door in the front one), the ceiling and a window over the bench.
func _shell() -> void:
	var planks := _material(Color(0.36, 0.3, 0.24), 0.9)
	var plaster := _material(Color(0.72, 0.7, 0.66), 0.95)
	var width := RIGHT - LEFT
	var depth := FRONT - BACK
	var middle := Vector3(0.5 * (LEFT + RIGHT), 0.0, 0.5 * (BACK + FRONT))
	var ground := _box(Vector3(width, 0.1, depth), middle + Vector3(0, FLOOR - 0.05, 0), planks, true)
	ground.set_meta("ground", true)
	var up := FLOOR + 0.5 * HEIGHT
	_box(Vector3(width + 2.0 * WALL, HEIGHT, WALL), Vector3(middle.x, up, BACK - 0.5 * WALL), plaster, true, false)
	_box(Vector3(WALL, HEIGHT, depth), Vector3(LEFT - 0.5 * WALL, up, middle.z), plaster, true, false)
	_box(Vector3(WALL, HEIGHT, depth), Vector3(RIGHT + 0.5 * WALL, up, middle.z), plaster, true, false)
	# The front wall, either side of a door 1 m wide in its middle, and over it.
	var door := 1.0
	var side := 0.5 * (width - door)
	_box(Vector3(side, HEIGHT, WALL), Vector3(LEFT + 0.5 * side, up, FRONT + 0.5 * WALL), plaster, true, false)
	_box(Vector3(side, HEIGHT, WALL), Vector3(RIGHT - 0.5 * side, up, FRONT + 0.5 * WALL), plaster, true, false)
	_box(Vector3(door, 0.5, WALL), Vector3(middle.x, FLOOR + HEIGHT - 0.25, FRONT + 0.5 * WALL), plaster, true, false)
	_box(Vector3(width, WALL, depth), middle + Vector3(0, FLOOR + HEIGHT + 0.5 * WALL, 0), plaster, true, false)
	# The window: a pane of daylight in the wall behind the bench.
	var sky := StandardMaterial3D.new()
	sky.albedo_color = Color(0.8, 0.88, 1.0)
	sky.emission_enabled = true
	sky.emission = Color(0.75, 0.85, 1.0)
	sky.emission_energy_multiplier = 1.2
	_box(Vector3(1.2, 0.7, 0.01), Vector3(0.0, 0.55, BACK + 0.006), sky, false, false)


## The workbench (its top at y = 0, as the board has always lain on it) and its vise: two
## jaws across the bench, the work clamped between them.
func _bench() -> void:
	var top := _box(BENCH_TOP, Vector3(0.0, -0.5 * BENCH_TOP.y, 0.0), _oak, true)
	bench = top
	bench.set_meta("ground", true)
	bench.set_meta("bench", true)
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			_box(Vector3(0.06, 0.8, 0.06), Vector3(sx * 0.38, -0.45, sz * 0.22), _oak, false)
	# The vise's jaws: meshes only (the work between them rests on the bench).
	for i in 2:
		var jaw := MeshInstance3D.new()
		var mesh := BoxMesh.new()
		mesh.size = Vector3(JAW.x, JAW.y, JAW.z)
		jaw.mesh = mesh
		jaw.material_override = _pine
		add_child(jaw)
		jaws.append(jaw)
	close_jaws(0.0)
	# Its screw, out in front.
	var screw := MeshInstance3D.new()
	var rod := CylinderMesh.new()
	rod.top_radius = 0.008
	rod.bottom_radius = 0.008
	rod.height = 0.16
	screw.mesh = rod
	screw.material_override = _steel
	screw.rotation = Vector3(PI / 2, 0.0, 0.0)
	screw.position = Vector3(0.0, -0.02, 0.5 * BENCH_TOP.z + 0.05)
	add_child(screw)


## A side table, to set pieces down on.
func _table() -> void:
	table = _box(TABLE_TOP, TABLE_AT, _oak, true)
	table.set_meta("ground", true)
	var leg_height := TABLE_AT.y - 0.5 * TABLE_TOP.y - FLOOR
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			_box(Vector3(0.05, leg_height, 0.05), Vector3(TABLE_AT.x + sx * 0.26, FLOOR + 0.5 * leg_height,
					TABLE_AT.z + sz * 0.26), _oak, false)


## The lumber rack against the left wall: two uprights and a shelf at each height.
func _rack() -> void:
	for sz in [-1.0, 1.0]:
		_box(Vector3(RACK_SIZE.x, RACK_SIZE.y, 0.05), RACK_AT + Vector3(0.0, 0.5 * RACK_SIZE.y, sz * 0.5 * RACK_SIZE.z),
				_pine, true)
	for h in RACK_SHELVES:
		var shelf := _box(Vector3(RACK_SIZE.x, 0.025, RACK_SIZE.z), RACK_AT + Vector3(0.0, h - 0.0125, 0.0), _pine, true)
		shelf.set_meta("ground", true)


## A box: its mesh, and (`solid`) a static body round it. `shadows`: whether it casts them
## (walls and ceiling do not: the daylight reaches the bench as before).
func _box(size: Vector3, at: Vector3, material: Material, solid: bool, shadows := true) -> StaticBody3D:
	var mesh := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = size
	mesh.mesh = box
	mesh.material_override = material
	mesh.position = at
	if not shadows:
		mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mesh)
	if not solid:
		return null
	var body := StaticBody3D.new()
	body.collision_layer = 1
	var shape := CollisionShape3D.new()
	var box_shape := BoxShape3D.new()
	box_shape.size = size
	shape.shape = box_shape
	shape.position = at
	body.add_child(shape)
	add_child(body)
	return body


static func _material(colour: Color, roughness: float, metallic := 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = colour
	m.roughness = roughness
	m.metallic = metallic
	return m
