extends "res://tests/harness.gd"

## The workshop as a place to walk about in, driven through the player's and the workshop's
## own methods, in game time:
## - it starts walking, the hands empty, every tool out of sight;
## - the hotbar: a key puts a tool in hand, held in view in front of the eyes; 0 empties the
##   hands; the wheel steps along it;
## - walking: back from the bench, then forward until the bench stops the player;
## - looking at the bench, E steps up to it (the view over the vise, the tools' controls);
##   Esc steps back.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "300"))).timeout.connect(func():
		push_error("walk and carry: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	var player = workshop.player

	# Walking, empty-handed; every tool out of sight.
	_check(workshop.mode == workshop.Mode.WALK and workshop.camera == player.camera, "it starts walking")
	_check(workshop.current == "" and workshop.tools.values().all(func(t): return not t.visible),
			"with empty hands, no tool in sight")

	# The hotbar: 3 is the saw, held in view in front of the eyes; 0 empties the hands.
	_key(KEY_3)
	await _frames(2)
	var saw = workshop.tools["saw"]
	var held: Vector3 = saw.global_position - player.eye()
	_check(workshop.current == "saw" and saw.visible, "3 puts the saw in hand")
	_check(held.length() < 0.8 and held.dot(player.forward()) > 0.2, "held in view, in front of the eyes (%.2f m)"
			% held.length())
	_key(KEY_0)
	await _frames(1)
	_check(workshop.current == "" and not saw.visible, "0 empties the hands")
	workshop.next_slot(1)
	_check(workshop.current == "chisel", "the wheel steps along the hotbar (%s)" % workshop.current)
	workshop.next_slot(-1)
	_check(workshop.current == "", "and back")

	# Walking: back a way, then forward until the bench stops the player.
	var from: Vector3 = player.global_position
	player.move_input = Vector2(0, -1)
	await _seconds(0.8)
	player.move_input = Vector2.ZERO
	var back: float = player.global_position.z - from.z
	_check(back > 0.8 and back < 1.4, "walking back 0.8 s at 1.5 m/s (%.2f m)" % back)
	player.move_input = Vector2(0, 1)
	await _seconds(2.0)
	player.move_input = Vector2.ZERO
	var front: float = player.global_position.z
	print("walk and carry: walked back %.2f m, then forward to %.2f m from the bench's middle" % [back, front])
	_check(front > 0.275 + 0.2 and front < 0.7, "until the bench stops them (%.2f)" % front)
	_check(absf(player.global_position.y - (-0.85)) < 0.02, "on the floor (%.3f)" % player.global_position.y)

	# Looking at the bench, E steps up to it; Esc steps back.
	player.face(Vector3(0.1, 0.0, 0.1))
	await _frames(2)
	_check(workshop.prompt().contains("work at the bench"), "the line under the crosshair says so: " + workshop.prompt())
	_key(KEY_E)
	await _frames(2)
	_check(workshop.mode == workshop.Mode.WORK and workshop.camera != player.camera and not player.active,
			"E steps up to the bench")
	_key(KEY_ESCAPE)
	await _frames(2)
	_check(workshop.mode == workshop.Mode.WALK and workshop.camera == player.camera and player.active,
			"Esc steps back")

	print("walk and carry: %s" % ("the workshop is walked and worked in" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A key pressed and let go, as the workshop receives it.
func _key(code: Key) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	workshop._unhandled_input(key)


## `seconds` of game time.
func _seconds(seconds: float) -> void:
	var waited := 0.0
	while waited < seconds:
		await get_tree().physics_frame
		waited += get_physics_process_delta_time()


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("walk and carry: " + what)
