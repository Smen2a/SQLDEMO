extends "res://tests/harness.gd"

## The workshop as a place to walk about in, driven through the player's and the workshop's
## own methods, in game time:
## - it starts walking, the hands empty, every tool out of sight;
## - the hotbar: a key puts a tool in hand, held in view in front of the eyes; 0 empties the
##   hands; the wheel steps along it;
## - walking: back from the bench, then forward until the bench stops the player;
## - looking at the bench, E steps up to it (the view over the vise, the tools' controls);
##   Esc steps back;
## - carrying: F takes the board out of the vise (then there is no stepping up to the bench);
##   it is held in front of the eyes, comes round as the player turns, R turns it; E lets it
##   go and it lies flat on the floor; E picks it up again;
## - the vise: the board carried back and let go with the eyes on the vise (held askew) goes
##   in squared: flat, along the bench, on the bench top between the jaws, which close on it;
## - the rack: E on its oak stack puts a new oak board in the hands. Let go over the vise
##   while the ash board is in it, it just drops; the ash board goes out onto the side table,
##   the oak board into the vise; at the bench a chisel pares it; out again it keeps its cut
##   (and its undo), and let go over the floor it lands and rests there.

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

	# Carrying: F takes the board out of the vise, into the hands; the vise is empty.
	var ash: RigidBody3D = workshop.clamped
	player.face(ash.global_position)
	await _frames(2)
	_check(workshop.prompt().contains("take it out"), "the line under the crosshair offers it: " + workshop.prompt())
	_key(KEY_F)
	await _frames(1)
	_check(workshop.held == ash and workshop.clamped == null and workshop.board == null and not ash.freeze,
			"F takes the board out of the vise, into the hands")
	_check(not workshop.enter_work(false), "with the vise empty, no stepping up to the bench")
	await _seconds(0.6)
	var off: float = ash.global_position.distance_to(player.eye() + player.forward() * workshop._hold)
	_check(off < 0.05, "carried in front of the eyes (%.3f m off)" % off)
	_key(KEY_1)
	await _frames(2)
	_check(workshop.current == "chisel" and not workshop.tools["chisel"].visible, "hands full: the chisel stays out of sight")
	_key(KEY_0)
	# It comes round as the player turns; R turns it a quarter.
	player.look(-PI / 2 / player.SENSITIVITY, 0.0)
	await _seconds(0.8)
	off = ash.global_position.distance_to(player.eye() + player.forward() * workshop._hold)
	_check(off < 0.08, "it comes round with the player (%.3f m off)" % off)
	var along: Vector3 = ash.global_basis.x.normalized()
	_key(KEY_R)
	await _seconds(0.8)
	var turned := rad_to_deg(along.angle_to(ash.global_basis.x.normalized()))
	_check(absf(turned - 90.0) < 5.0, "R turns it a quarter (%.1f degrees)" % turned)
	# Let go over the floor: it falls, and lies flat on it.
	player.look(PI / 2 / player.SENSITIVITY, 0.0)
	player.move_input = Vector2(0, -1)
	await _seconds(0.7)
	player.move_input = Vector2.ZERO
	player.face(player.global_position + Vector3(0.0, 0.2, -0.8))
	await _seconds(0.6)
	_key(KEY_E)
	_check(workshop.held == null, "E lets it go")
	await _seconds(1.5)
	var lying: float = ash.global_position.y - (-0.85)
	print("walk and carry: let go over the floor, the board's middle %.2f mm above it, face up %.1f degrees off" % [
			lying * 1000.0, rad_to_deg(ash.global_basis.y.normalized().angle_to(Vector3.UP))])
	_check(absf(lying - 0.0125) < 0.002, "it lies flat on the floor (its middle %.1f mm up)" % (lying * 1000.0))
	# And up again from the floor.
	player.face(ash.global_position)
	await _frames(2)
	_check(workshop.prompt().contains("pick up the ash board"), "the line offers it: " + workshop.prompt())
	_key(KEY_E)
	await _frames(1)
	_check(workshop.held == ash, "E picks it up again")

	# The vise: carried back to the bench, held askew (tipped and turned along the bench's
	# depth), let go with the eyes on the vise.
	player.face(Vector3(0.0, 0.1, 0.0))
	player.move_input = Vector2(0, 1)
	await _seconds(1.5)
	player.move_input = Vector2.ZERO
	workshop._hold_turn = Basis(Vector3.RIGHT, 0.3) * workshop._hold_turn
	player.face(Vector3(0.05, 0.0, -0.03))
	await _seconds(0.6)
	_check(workshop.at_vise() and workshop.prompt().contains("put the ash board in the vise"),
			"the line offers the vise: " + workshop.prompt())
	_key(KEY_E)
	await _frames(2)
	var sdf = ash.get_meta("sdf")
	_check(workshop.held == null and workshop.clamped == ash and ash.freeze and workshop.board == sdf,
			"E puts it in the vise, held still, the one the tools work on")
	var face_up: float = rad_to_deg(sdf.global_basis.z.normalized().angle_to(Vector3.UP))
	var length_off: float = rad_to_deg(acos(absf(sdf.global_basis.x.normalized().dot(Vector3.RIGHT))))
	var extent: AABB = workshop._extent(sdf, sdf.global_transform)
	print("walk and carry: in the vise, its face %.2f degrees off up, its length %.2f degrees off the bench's," % [
			face_up, length_off], " its underside %.3f mm off the bench top, its middle %.2f mm off the vise's" % [
			extent.position.y * 1000.0, Vector2(extent.get_center().x, extent.get_center().z).length() * 1000.0])
	_check(face_up < 1.0 and length_off < 1.0, "squared: flat, along the bench")
	_check(absf(extent.position.y) < 0.0001 and Vector2(extent.get_center().x, extent.get_center().z).length() < 0.001,
			"set down on the bench top, in the middle of the vise")
	var jaws: Array = workshop.room.jaws
	var gap: float = absf(jaws[1].position.z - jaws[0].position.z) - workshop.room.JAW.z
	_check(absf(gap - extent.size.z) < 0.001, "the jaws closed on it (%.1f mm apart)" % (gap * 1000.0))
	await _seconds(0.5)
	_check(ash.global_transform.origin.distance_to(Vector3(0.0, 0.0125, 0.0)) < 0.001, "and it stays put")
	_check(workshop.enter_work(false), "with it in the vise, stepping up to the bench")
	workshop.leave_work()

	# The rack: an oak board from its stack, into the hands.
	await _walk_to(Vector3(-1.9, 0.0, 1.0))
	var stack: StaticBody3D = workshop.room.stacks["board_oak"]
	player.face(stack.get_meta("top") + Vector3(0.02, 0.0, 0.0))
	await _frames(2)
	_check(workshop.prompt().contains("take an oak board"), "at the rack, the line offers its stock: " + workshop.prompt())
	_key(KEY_E)
	await _frames(1)
	var oak: RigidBody3D = workshop.held
	_check(oak != null and oak.get_meta("wood_name") == "oak" and oak.get_meta("kind") == "board" and oak != ash,
			"E takes a new oak board into the hands")
	await _seconds(0.8)
	off = oak.global_position.distance_to(player.eye() + player.forward() * workshop._hold)
	_check(off < 0.05, "carried off the rack in front of the eyes (%.3f m off)" % off)

	# Back to the bench with it: with the ash board in the vise, let go there it just drops (on
	# the bench beside it).
	await _walk_to(Vector3(0.0, 0.0, 0.6))
	var beside := Vector3(0.12, 0.0, 0.12)
	player.face(beside)
	workshop._hold = player.eye().distance_to(beside) - 0.1
	await _seconds(0.6)
	_check(workshop.at_vise() and workshop.prompt().contains("the vise holds the ash board"),
			"the line says the vise is taken: " + workshop.prompt())
	_key(KEY_E)
	await _seconds(1.2)
	_check(workshop.held == null and workshop.clamped == ash and not oak.freeze, "so letting go just drops it")
	_check(absf(oak.global_position.y - 0.0125) < 0.002, "it lies on the bench (%.1f mm up)" % (oak.global_position.y * 1000.0))

	# The ash board out of the vise, onto the side table.
	player.face(ash.global_position)
	await _frames(2)
	_key(KEY_F)
	await _frames(1)
	_check(workshop.held == ash and workshop.clamped == null, "F takes the ash board out again")
	await _walk_to(Vector3(0.8, 0.0, 0.8))
	var table: Vector3 = workshop.room.TABLE_AT + Vector3(-0.05, 0.5 * workshop.room.TABLE_TOP.y, 0.05)
	player.face(table)
	workshop._hold = player.eye().distance_to(table) - 0.1
	await _seconds(0.8)
	_check(not workshop.at_vise() and workshop.prompt().begins_with("E: let go"), "over the table: " + workshop.prompt())
	_key(KEY_E)
	await _seconds(1.2)
	var on_table: float = ash.global_position.y - (workshop.room.TABLE_AT.y + 0.5 * workshop.room.TABLE_TOP.y)
	_check(workshop.held == null and absf(on_table - 0.0125) < 0.002, "it lies on the side table (%.1f mm up)" % (on_table * 1000.0))

	# The oak board into the vise.
	player.face(oak.global_position)
	await _frames(2)
	_check(workshop.prompt().contains("pick up the oak board"), "the oak board, from across the bench: " + workshop.prompt())
	_key(KEY_E)
	await _frames(1)
	await _walk_to(Vector3(0.0, 0.0, 0.6))
	player.face(Vector3.ZERO)
	await _seconds(0.6)
	_check(workshop.prompt().contains("put the oak board in the vise"), "the vise is free: " + workshop.prompt())
	_key(KEY_E)
	await _frames(2)
	var oak_sdf = oak.get_meta("sdf")
	extent = workshop._extent(oak_sdf, oak_sdf.global_transform)
	face_up = rad_to_deg(oak_sdf.global_basis.z.normalized().angle_to(Vector3.UP))
	_check(workshop.clamped == oak and oak.freeze and workshop.board == oak_sdf and face_up < 1.0 and
			absf(extent.position.y) < 0.0001, "in the vise, squared, on the bench top (%.2f degrees)" % face_up)

	# At the bench: a chisel stroke along it.
	_check(workshop.enter_work(false), "E at the bench: the view over the oak board")
	await _frames(3)
	# Where the chisel will go: a point on the top (body space, to find it again wherever the
	# board is), and how far down from just over it the surface is.
	var probe: Vector3 = oak_sdf.global_transform.affine_inverse() * Vector3(-0.05, 0.025, 0.0)
	var top := _depth_at(oak_sdf, probe)
	await _pare(Vector3(-0.0785, 0.025, 0.0), Vector3(-0.03, 0.025, 0.0)) # (from its end: no diving in)
	oak_sdf.flush()
	var cut := _depth_at(oak_sdf, probe) - top
	_check(cut > 0.2 and cut < 0.8, "a chisel stroke pares it (%.2f mm deep)" % cut)
	workshop.leave_work()
	await _frames(2)
	# Out of the vise again, its cut with it; and down on the floor.
	player.face(oak.global_position)
	await _frames(2)
	_key(KEY_F)
	await _frames(1)
	_check(workshop.held == oak and not oak.freeze and oak_sdf.get_stats().get("can_undo", false),
			"picked up again, it keeps its undo")
	await _walk_to(Vector3(0.0, 0.0, 1.6))
	player.face(player.global_position + Vector3(0.0, 0.2, -0.8))
	await _seconds(0.6)
	_key(KEY_E)
	await _seconds(2.0)
	var bottom: float = workshop._extent(oak_sdf, oak_sdf.global_transform).position.y - workshop.room.FLOOR
	var kept := _depth_at(oak_sdf, probe) - top
	print("walk and carry: the oak board pared %.2f mm deep, let go over the floor: its underside %.2f mm above it, the cut %.2f mm deep there" % [
			cut, bottom * 1000.0, kept])
	_check(absf(bottom) < 0.001 and oak.linear_velocity.length() < 0.01, "it lands on the floor and rests there")
	_check(absf(kept - cut) < 0.05, "with its cut")

	print("walk and carry: %s" % ("the workshop is walked and worked in" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## Walks the player to `point` (its x, z: the floor under it), turning to face that way
## (the head's tilt kept), at a walk, or until a few seconds have passed (something in the way).
func _walk_to(point: Vector3) -> void:
	var player = workshop.player
	var waited := 0.0
	while waited < 5.0:
		var d: Vector3 = point - player.global_position
		d.y = 0.0
		if d.length() < 0.03:
			break
		player.yaw = atan2(-d.x, -d.z)
		player._apply()
		player.move_input = Vector2(0, clampf(d.length() / 0.2, 0.2, 1.0))
		await get_tree().physics_frame
		waited += get_physics_process_delta_time()
	player.move_input = Vector2.ZERO
	await get_tree().physics_frame


## How far (mm) a ray comes down to a piece's surface from 5 mm over `point` (body space, on
## its top: its +z), wherever the piece is.
func _depth_at(sdf, point: Vector3) -> float:
	var xf: Transform3D = sdf.global_transform
	var up: Vector3 = xf.basis.z.normalized()
	var hit: Dictionary = sdf.raycast(xf * point + up * 0.005, -up, 0.02)
	return hit.get("distance", INF) * 1000.0


## A chisel pared along the top of the piece in the vise, from `start` to `end` (world): a
## left-drag in the view over the bench, followed at the chisel's working speed.
func _pare(start: Vector3, end: Vector3) -> void:
	workshop.set_setting("chisel", "depth", 0.5)
	workshop.set_setting("chisel", "angle", 30.0)
	workshop.select_tool("chisel")
	await _frames(8)
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(start))
	await _frames(2)
	workshop.press(cam.unproject_position(start))
	for k in 16:
		workshop.drag_screen(cam.unproject_position(start.lerp(end, float(k + 1) / 16)))
		await _frames(1)
	await catch_up(workshop)
	_check(workshop.is_engaged(), "the chisel engaged")
	workshop.release()
	workshop.select_tool("")


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
