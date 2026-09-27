extends CharacterBody3D

## A person in the workshop, seen from their own eyes: WASD (or the arrows) to walk, Shift to
## hurry, the mouse to look (captured while walking). Its origin is at their feet. Physics
## layer 2, meeting only the room (layer 4, room.gd): pieces lying about neither block them
## nor are shoved by them, and the tools' push on loose pieces (layer 1) never moves them.
## Tests (and the workshop, when it takes over) drive it with `move_input` and `look()`;
## `active` says whether it takes the keyboard and mouse itself.

const HEIGHT := 1.75
const RADIUS := 0.25
const EYE := 1.6 # m above the feet
const WALK := 1.5 # m/s
const HURRY := 3.0
const GRAVITY := 9.8
const SENSITIVITY := 0.0025 # radians a pixel

var camera: Camera3D
## Whether the player takes the keyboard and mouse (walking), or stands still (working).
var active := true
## Movement asked for besides the keys: x to the right, y forward (each -1 to 1).
var move_input := Vector2.ZERO
var hurry := false
var yaw := 0.0 # about the vertical; 0 faces -z
var pitch := 0.0 # up positive


func _ready() -> void:
	collision_layer = 2
	collision_mask = 8 # (room.gd ROOM_LAYER)
	var shape := CollisionShape3D.new()
	var capsule := CapsuleShape3D.new()
	capsule.height = HEIGHT
	capsule.radius = RADIUS
	shape.shape = capsule
	shape.position = Vector3(0.0, 0.5 * HEIGHT, 0.0)
	add_child(shape)
	camera = Camera3D.new()
	camera.fov = 70.0
	camera.near = 0.02
	camera.far = 30.0
	camera.position = Vector3(0.0, EYE, 0.0)
	add_child(camera)
	_keys()
	_apply()


## Turns the view by a mouse movement (pixels).
func look(dx: float, dy: float) -> void:
	yaw -= dx * SENSITIVITY
	pitch = clampf(pitch - dy * SENSITIVITY, -1.45, 1.45)
	_apply()


## Faces a point (world), turning and tilting the head.
func face(point: Vector3) -> void:
	var d := point - camera.global_position
	yaw = atan2(-d.x, -d.z)
	pitch = atan2(d.y, Vector2(d.x, d.z).length())
	_apply()


## The point the eyes look along, and which way (world).
func eye() -> Vector3:
	return camera.global_position


func forward() -> Vector3:
	return -camera.global_basis.z


func _unhandled_input(event: InputEvent) -> void:
	if active and event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		var motion := event as InputEventMouseMotion
		look(motion.relative.x, motion.relative.y)


func _physics_process(delta: float) -> void:
	var wish := move_input
	var fast := hurry
	if active:
		wish += Input.get_vector("walk_left", "walk_right", "walk_back", "walk_forward")
		fast = fast or Input.is_action_pressed("hurry")
	if wish.length() > 1.0:
		wish = wish.normalized()
	var way := Basis(Vector3.UP, yaw) * Vector3(wish.x, 0.0, -wish.y)
	var speed := HURRY if fast else WALK
	velocity.x = way.x * speed
	velocity.z = way.z * speed
	if is_on_floor():
		velocity.y = 0.0
	else:
		velocity.y -= GRAVITY * delta
	move_and_slide()


func _apply() -> void:
	rotation = Vector3(0.0, yaw, 0.0)
	if camera:
		camera.rotation = Vector3(pitch, 0.0, 0.0)


## The walking keys, added once (WASD and the arrows, Shift to hurry).
static func _keys() -> void:
	var keys := {"walk_forward": [KEY_W, KEY_UP], "walk_back": [KEY_S, KEY_DOWN], "walk_left": [KEY_A, KEY_LEFT],
			"walk_right": [KEY_D, KEY_RIGHT], "hurry": [KEY_SHIFT]}
	for action in keys:
		if InputMap.has_action(action):
			continue
		InputMap.add_action(action)
		for code in keys[action]:
			var key := InputEventKey.new()
			key.physical_keycode = code
			InputMap.action_add_event(action, key)
