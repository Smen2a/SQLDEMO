extends "res://tests/harness.gd"

## Your mallet (workshop.take_up, assembling.finished): the mallet's head, handle and wedge
## made as drawn (workshop.part_as_drawn) and put together as a player does (wedge_glue's
## way: the handle glued home and set, the mallet stood on its handle, the wedge driven home):
## - glued but not yet wedged it is not finished; wedged, it is: the mallet;
## - out of the vise and let go on the bench, the line under the crosshair offers it: E takes
##   it up as yours: it leaves the bench (hidden, no longer a piece of work), and the chisel's
##   panel names it with its weight;
## - a firm chop with it goes as deep as its weight says against the workshop's mallet's
##   (0.6 kg): the plan's blow, before and after, in that ratio;
## - a new board in the vise leaves it yours.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var assembling
var _failed := false


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("your mallet: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	assembling = workshop.assembling

	# A firm chop on the ash board in the vise, with the workshop's mallet.
	var before: float = await _chop_blow()
	_check(before > 0.0 and workshop.blow_weight() == 1.0, "a firm chop with the workshop's mallet: %.2f mm" % before)

	# The mallet made as drawn: the handle glued into the head and set.
	var head: RigidBody3D = await _head_in_vise()
	var head_sdf = head.get_meta("sdf")
	var handle: RigidBody3D = _carried("handle")
	var handle_sdf = handle.get_meta("sdf")
	await _look_at(head_sdf.to_global(Vector3(55, 35, 0)))
	_key(KEY_E)
	_key(KEY_G)
	for i in 62:
		_wheel(true)
	_key(KEY_E)
	await _frames(1)
	assembling.clock += assembling.GLUE_SET + 1.0
	_check(head.get_meta("members", []).size() == 1 and assembling.finished(head).is_empty(),
			"glued, the wedge still out: not finished")

	# Stood on its handle, the wedge driven home.
	await _look_at(head_sdf.to_global(Vector3(10, 35, 27.5)))
	_key(KEY_F)
	await _frames(1)
	_key(KEY_T)
	_key(KEY_T)
	await _seconds(1.5)
	await _look_at(Vector3(0.0, 0.0, 0.05))
	_key(KEY_E)
	await _frames(2)
	var wedge: RigidBody3D = _carried("wedge")
	await _look_at(handle_sdf.to_global(Vector3(0, 17.5, 14)))
	_key(KEY_E)
	var blows := 0
	while assembling.offering() and assembling.offer.t < assembling.offer.joint.travel - 0.01 and blows < 80:
		_click()
		blows += 1
		await get_tree().create_timer(assembling.BLOW_INTERVAL + 0.02).timeout
	_key(KEY_E)
	await _frames(1)
	var made: Dictionary = assembling.finished(head)
	print("your mallet: wedged in %d blows; finished: %s" % [blows, made])
	_check(made.get("plan", "") == "mallet" and made.get("name", "") == "Mallet", "glued and wedged: the mallet, finished")

	# Out of the vise, let go on the bench, and taken up.
	await _look_at(head_sdf.to_global(Vector3(10, 35, 27.5)))
	_key(KEY_F)
	await _frames(1)
	_check(workshop.held == head, "out of the vise, in the hands")
	await _look_at(Vector3(0.25, 0.0, 0.1))
	_key(KEY_E)
	await _seconds(1.5)
	_check(workshop.held == null and workshop.clamped == null, "let go on the bench")
	await _look_at(head.global_position)
	print("your mallet: looked at: %s" % workshop.prompt())
	_check(workshop.prompt() == "E: take up the mallet as yours", "the line offers it: " + workshop.prompt())
	_key(KEY_E)
	await _frames(1)
	var mallet: Dictionary = workshop.mallet
	print("your mallet: taken up: %s, blows %.2f times the workshop mallet's" % [mallet.get("name", "?"),
			workshop.blow_weight()])
	_check(mallet.get("piece") == head and not head.visible and not workshop.pieces.has(head) and
			mallet.get("mass", 0.0) > 0.3 and mallet.get("mass", 0.0) < 0.7, "yours: off the bench, %.0f g" % (
			mallet.get("mass", 0.0) * 1000.0))
	_check(String(mallet.get("name", "")).begins_with("your mallet (oak and ash"), "named for its woods")
	var weight: float = clampf(mallet.get("mass", 0.0) / workshop.WORKSHOP_MALLET, 0.6, 1.6)
	_check(absf(workshop.blow_weight() - weight) < 1e-6, "its blows as hard as its weight says")

	# A new board in the vise: still yours. A firm chop with it.
	workshop.set_wood("board")
	await _frames(2)
	_check(not workshop.mallet.is_empty() and is_instance_valid(workshop.mallet.piece), "a new board leaves it yours")
	var after: float = await _chop_blow()
	print("your mallet: a firm chop: %.2f mm with the workshop's mallet, %.2f with yours (%.2f times)" % [
			before, after, after / before])
	_check(absf(after / before - weight) < 0.02, "a chop goes as deep as its weight says")
	_check(workshop._ui._mallet_labels[0].text.contains("your mallet"), "the panel names it: " +
			workshop._ui._mallet_labels[0].text)

	print("your mallet: %s" % ("the mallet made is yours" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## At the bench, the chisel held up to chop, a firm blow planned (Space) on the middle of the
## board's top: how deep it goes (mm).
func _chop_blow() -> float:
	workshop.enter_work(false)
	await _frames(2)
	workshop.select_tool("chisel")
	workshop.set_setting("chisel", "angle", 90.0)
	workshop.set_setting("chisel", "blow", 1.0)
	var screen: Vector2 = workshop.camera.unproject_position(workshop.board.to_global(Vector3(0, 0, 12.5)))
	workshop.lock(screen)
	await _frames(1)
	var blow: float = workshop.get_plan().get("blow", 0.0)
	workshop.unlock()
	workshop.leave_work()
	await _frames(2)
	return blow


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


## A part made as drawn, in the hands.
func _carried(part: String) -> RigidBody3D:
	var piece: RigidBody3D = workshop.part_as_drawn("mallet", part)
	piece.global_position = Vector3(0.3, 0.1, 0.25)
	workshop.pick_up(piece)
	return piece


func _look_at(point: Vector3) -> void:
	workshop.player.face(point)
	await _frames(2)


func _key(code: Key) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	workshop._unhandled_input(key)


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
		push_error("your mallet: " + what)
