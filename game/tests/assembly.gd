extends "res://tests/harness.gd"

## Putting parts together (workshop/assembling.gd), the mallet's head and handle made as drawn
## (workshop.part_as_drawn), driven through the workshop's own keys, wheel and clicks:
## - the head in the vise, the handle carried and the head looked at: E offers it up, at the
##   joint's mouth (the tenon's end on the mortise's, on the face side);
## - the wheel pushes it home (58 mm, seated: snug); E lets go: one rigid body, the handle's
##   SdfBody in the head's, its part with it, colliders from both;
## - undo straight after: the handle comes out into the hands; offered and pushed home again;
## - at the bench, the tools on the part under the pointer: the handle, the head; K checks the
##   handle against its drawing (as drawn);
## - F on the handle: drawn back out along the joint, the wheel back, into the hands;
## - a handle whose tenon is 0.2 mm fat binds near the mouth: pushing stops, taps with the
##   mallet drive it home;
## - out of the vise and carried, the two go together.
## Rendered, with --out=<dir>: the handle offered up (assembly_offer.png), and home in the
## head (assembly_home.png).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var assembling
var _failed := false
var _out := ""


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("assembly: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	assembling = workshop.assembling

	# The head in the vise (the ash board it starts with put away), the handle in the hands.
	var head: RigidBody3D = await _head_in_vise()
	var handle: RigidBody3D = workshop.part_as_drawn("mallet", "handle")
	handle.global_position = Vector3(0.3, 0.1, 0.25)
	workshop.pick_up(handle)
	workshop.player.face(head.global_position + Vector3(0, 0.03, 0))
	await _frames(2)
	_check(workshop.prompt().begins_with("E: offer the handle up to the head"), "offered up: " + workshop.prompt())

	# E: at the joint's mouth, the tenon's end on the mortise's mouth.
	_key(KEY_E)
	await _frames(1)
	_check(assembling.offering() and workshop.held == null and assembling.offer.t == 0.0, "E puts it on the joint")
	var head_sdf = head.get_meta("sdf")
	var handle_sdf = handle.get_meta("sdf")
	var end: Vector3 = head_sdf.global_transform.affine_inverse() * (handle_sdf.global_transform * Vector3(0, 2.5, 8))
	print("assembly: offered up, the tenon's corner at (%.2f %.2f %.2f) in the head; the fit %s in %.0f ms" % [
			end.x, end.y, end.z, assembling.offer.fit.kind, assembling.offer.fit.ms])
	_check(absf(end.z) < 0.1 and (absf(end.x - 40) < 0.1 or absf(end.x - 70) < 0.1) and
			(absf(end.y - 29) < 0.1 or absf(end.y - 41) < 0.1), "the tenon's end on the mortise's mouth")
	await _shot("assembly_offer")

	# The wheel: home, seated. E: one body.
	for i in 62:
		_wheel(true)
	_check(absf(assembling.offer.t - 58.0) < 0.01 and assembling.offer.said.begins_with("Home"),
			"pushed home: %s" % assembling.offer.get("said", ""))
	_key(KEY_E)
	await _frames(2)
	_check(not assembling.offering() and not is_instance_valid(handle) and handle_sdf.get_parent() == head and
			head.get_meta("members", []).size() == 1 and handle_sdf.get_meta("part", {}).get("part", "") == "handle",
			"let go: one body, the handle's part with it")
	_check(workshop._shapes_of(head).size() == 2 and workshop.pieces.size() == 1, "its colliders from both parts")

	# Undo straight after: out into the hands; again, home.
	workshop.undo()
	await _frames(1)
	handle = workshop.held
	_check(handle != null and handle.get_meta("sdf") == handle_sdf and head.get_meta("members", []).is_empty() and
			handle.get_meta("part", {}).get("part", "") == "handle", "undo takes the handle out into the hands")
	workshop.player.face(head.global_position + Vector3(0, 0.03, 0))
	await _frames(2)
	_key(KEY_E)
	for i in 60:
		_wheel(true)
	_key(KEY_E)
	await _frames(2)
	_check(handle_sdf.get_parent() == head, "offered, pushed and joined again")
	await _shot("assembly_home")

	# At the bench: the tools work on the part under the pointer; K checks the handle.
	workshop.enter_work(false)
	await _frames(2)
	workshop.hover_screen(workshop.camera.unproject_position(handle_sdf.to_global(Vector3(150, 17.5, 14))))
	_check(workshop.board == handle_sdf and workshop.holder() == handle_sdf, "the pointer on the handle: the tools work on it")
	workshop._unhandled_input(_key_event(KEY_K))
	await _frames(1)
	print("assembly: the handle checked: %s" % "; ".join(workshop.checking.lines()))
	_check(workshop.checking.result.get("spots", [1]).is_empty(), "the handle as drawn")
	workshop.hover_screen(workshop.camera.unproject_position(head_sdf.to_global(Vector3(8, 35, 0))))
	_check(workshop.board == head_sdf and workshop.holder() == head, "on the head: the head")
	workshop.leave_work()
	await _frames(2)

	# F on the handle: back on the joint, drawn out with the wheel into the hands.
	workshop.player.face(handle_sdf.to_global(Vector3(150, 17.5, 14)))
	await _frames(2)
	_check(workshop.prompt().contains("F: draw the handle out"), "the handle looked at: " + workshop.prompt())
	_key(KEY_F)
	await _frames(1)
	_check(assembling.offering() and absf(assembling.offer.t - 58.0) < 0.01, "F: back on the joint, home")
	for i in 60:
		_wheel(false)
	await _frames(1)
	_check(not assembling.offering() and workshop.held != null and workshop.held.get_meta("sdf") == handle_sdf and
			head.get_meta("members", []).is_empty(), "the wheel back: drawn out into the hands")

	# A handle whose tenon is 0.2 mm fat: it binds, and the mallet drives it home.
	var loose_handle: RigidBody3D = workshop.held
	workshop.let_go()
	await _frames(1)
	workshop.pieces.erase(loose_handle)
	loose_handle.queue_free()
	var fat: Dictionary = workshop.plans.part("mallet", "handle").duplicate(true)
	fat.features[0].y = [2.4, 32.6]
	var tight: RigidBody3D = workshop.part_as_drawn("mallet", "handle", fat)
	tight.global_position = Vector3(0.3, 0.1, 0.25)
	workshop.pick_up(tight)
	workshop.player.face(head.global_position + Vector3(0, 0.03, 0))
	await _frames(2)
	_key(KEY_E)
	for i in 62:
		_wheel(true)
	var pushed: float = assembling.offer.get("t", 0.0)
	print("assembly: a tenon 0.2 mm fat: %s; pushed %.1f mm: %s" % [assembling.offer.fit.kind, pushed, assembling.offer.said])
	_check(assembling.offer.fit.kind == "drives" and pushed < 58.0 and assembling.offer.said.begins_with("It binds"),
			"a fat tenon binds")
	var blows := 0
	while assembling.offer.t < 58.0 - 0.01 and blows < 40:
		_click()
		blows += 1
		await get_tree().create_timer(assembling.BLOW_INTERVAL + 0.02).timeout
	print("assembly: driven home in %d blows: %s" % [blows, assembling.offer.said])
	_check(absf(assembling.offer.t - 58.0) < 0.01 and assembling.offer.said.begins_with("Home, driven"), "tapped home")
	_key(KEY_E)
	await _frames(2)
	var tight_sdf = tight.get_meta("sdf") if is_instance_valid(tight) else null
	_check(head.get_meta("members", []).size() == 1, "and joined")

	# Out of the vise, carried: together.
	var joined_sdf = head.get_meta("members")[0].sdf
	var relative: Transform3D = head_sdf.global_transform.affine_inverse() * joined_sdf.global_transform
	workshop.player.face(head_sdf.to_global(Vector3(8, 35, 27)))
	await _frames(2)
	workshop.take_out()
	for i in 30:
		await get_tree().physics_frame
	var after: Transform3D = head_sdf.global_transform.affine_inverse() * joined_sdf.global_transform
	_check(workshop.held == head and after.origin.distance_to(relative.origin) < 1e-3 and
			workshop.clamped == null, "carried out of the vise, the parts go together")

	print("assembly: %s" % ("the handle goes into the head" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The mallet's head, as drawn, in the vise (whatever was there put away).
func _head_in_vise() -> RigidBody3D:
	var old: RigidBody3D = workshop.clamped
	workshop._unclamp()
	workshop.pieces.erase(old)
	old.queue_free()
	var head: RigidBody3D = workshop.part_as_drawn("mallet", "head")
	workshop._put_in_vise(head)
	await _frames(2)
	return head


func _key_event(code: Key) -> InputEventKey:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	return key


func _key(code: Key) -> void:
	workshop._unhandled_input(_key_event(code))


func _wheel(up: bool) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_WHEEL_UP if up else MOUSE_BUTTON_WHEEL_DOWN
	e.pressed = true
	workshop._unhandled_input(e)


func _click() -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_LEFT
	e.pressed = true
	workshop._unhandled_input(e)


func _shot(name: String) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	for i in 3:
		await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_out)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_out, name])


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("assembly: " + what)
