extends "res://tests/harness.gd"

## Glue, the wedge and sawing flush (workshop/assembling.gd), the mallet's head, handle and
## wedge made as drawn, driven through the workshop's keys, wheel and clicks:
## - the handle offered up to the head in the vise, glued (G), pushed home, let go: joined,
##   the glue wet; undo takes it out; again, glued; the glue set (GLUE_SET on): F no longer
##   draws it out, nor undo;
## - taken out of the vise (F on the head), tipped handle down (T twice), put in the vise:
##   the handle standing on the bench, the head up, the tenon's end 3 mm proud above it;
## - the wedge offered to the tenon's end: it binds at the mouth; blows drive it, less far
##   as it tightens, home (38 mm); let go: joined to the handle, wedged, and F leaves it;
## - sawn flush with the head (the saw straight on each part's SdfBody): the wedge's and the
##   tenon's proud ends come away as offcuts, the parts still joined.
## Rendered, with --out=<dir>: the wedge being driven (wedge_driven.png), the mallet wedged
## and sawn flush (mallet_wedged.png).

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var assembling
var _failed := false
var _out := ""


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("wedge glue: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	assembling = workshop.assembling

	# The handle glued into the head.
	var head: RigidBody3D = await _head_in_vise()
	var head_sdf = head.get_meta("sdf")
	var handle: RigidBody3D = _carried("handle")
	var handle_sdf = handle.get_meta("sdf")
	await _look_at(head_sdf.to_global(Vector3(55, 35, 0)))
	_key(KEY_E)
	_key(KEY_G)
	_check(assembling.offering() and assembling.offer.glued and workshop.prompt().contains("glued"), "offered up, glued")
	_home()
	_key(KEY_E)
	await _frames(1)
	var member: Dictionary = _member(head, handle_sdf)
	_check(member.get("glued_at", -1.0) >= 0.0 and assembling.why_locked(member) == "", "joined, the glue wet")
	workshop.undo()
	await _frames(1)
	_check(workshop.held != null and workshop.held.get_meta("sdf") == handle_sdf, "undo while it is wet: out into the hands")
	await _look_at(head_sdf.to_global(Vector3(55, 35, 0)))
	_key(KEY_E)
	_key(KEY_G)
	_home()
	_key(KEY_E)
	await _frames(1)
	assembling.clock += assembling.GLUE_SET + 1.0
	member = _member(head, handle_sdf)
	_check(assembling.why_locked(member) == "glued", "a minute on, the glue has set")
	await _look_at(handle_sdf.to_global(Vector3(150, 17.5, 14)))
	_check(workshop.prompt().contains("glued in"), "the handle looked at: " + workshop.prompt())
	_key(KEY_F)
	workshop.undo()
	await _frames(1)
	_check(not assembling.offering() and head.get_meta("members", []).size() == 1 and workshop.clamped == head and
			workshop.held == null, "F and undo leave it: glued for good, the head still in the vise")
	print("wedge glue: %s" % "; ".join(assembling.joined_lines(head)))

	# Out of the vise, tipped handle down, back in: the handle on the bench, the head up.
	await _look_at(head_sdf.to_global(Vector3(10, 35, 27.5)))
	_key(KEY_F)
	await _frames(1)
	_check(workshop.held == head and workshop.clamped == null, "F on the head: out of the vise")
	_key(KEY_T)
	_key(KEY_T)
	await _seconds(1.5)
	await _look_at(Vector3(0.0, 0.0, 0.05))
	_check(workshop.prompt().contains("in the vise"), "over the vise: " + workshop.prompt())
	_key(KEY_E)
	await _frames(2)
	var foot: Vector3 = handle_sdf.to_global(Vector3(300, 17.5, 14))
	var tip: Vector3 = handle_sdf.to_global(Vector3(0, 17.5, 14))
	var head_top: float = head_sdf.to_global(Vector3(55, 10, 55)).y
	print("wedge glue: on end in the vise: the handle's foot at y %.1f mm, the tenon's end %.1f mm over the head" % [
			foot.y * 1000.0, (tip.y - head_top) * 1000.0])
	_check(workshop.clamped == head and absf(foot.y) < 0.003 and absf(tip.y - head_top - 0.003) < 0.0005,
			"the handle standing on the bench, its tenon 3 mm proud of the head")

	# The wedge, driven into the tenon's kerf.
	var wedge: RigidBody3D = _carried("wedge")
	var wedge_sdf = wedge.get_meta("sdf")
	await _look_at(tip)
	_check(workshop.prompt().begins_with("E: offer the wedge up to the handle"), "the wedge offered: " + workshop.prompt())
	_key(KEY_E)
	_check(assembling.offering() and assembling.offer.joint.kind == "wedge", "on the kerf")
	_wheel(true)
	print("wedge glue: the wedge pushed by hand: %s mm in; %s" % [assembling.offer.t, assembling.offer.said])
	_check(assembling.offer.said.begins_with("It binds"), "it binds at the mouth")
	var gains: Array[float] = []
	var blows := 0
	while assembling.offer.t < assembling.offer.joint.travel - 0.01 and blows < 80:
		var was: float = assembling.offer.t
		_click()
		gains.append(assembling.offer.t - was)
		blows += 1
		if blows == 6:
			await _shot("wedge_driven")
		await get_tree().create_timer(assembling.BLOW_INTERVAL + 0.02).timeout
	print("wedge glue: driven home in %d blows (%.1f mm the first, %.1f the last): %s" % [blows, gains[0], gains.back(),
			assembling.offer.said])
	_check(assembling.offer.t >= assembling.offer.joint.travel - 0.01 and gains[0] > 2.0 * gains.back(),
			"home, each blow driving it less far")
	_key(KEY_E)
	await _frames(1)
	var wedged: Dictionary = _member(head, wedge_sdf)
	_check(not wedged.is_empty() and wedged.to == handle_sdf and assembling.why_locked(wedged) == "wedged",
			"joined to the handle, wedged")
	await _look_at(wedge_sdf.to_global(Vector3(3, 6, 2.5)))
	_key(KEY_F)
	_check(not assembling.offering() and head.get_meta("members", []).size() == 2 and workshop.clamped == head,
			"F leaves a wedge, the mallet still in the vise")

	# Sawn flush with the head's top (the kerf on the proud side of it).
	var level: Vector3 = handle_sdf.global_transform.basis * Vector3(1, 0, 0) # the handle's way, into the head
	var cut_at: Vector3 = handle_sdf.to_global(Vector3(2.6, 17.5, 8))
	var offcuts: int = workshop.offcuts.size()
	_saw(wedge_sdf, cut_at, handle_sdf)
	await _separated(offcuts + 1)
	_saw(handle_sdf, handle_sdf.to_global(Vector3(2.6, 5.0, 8)), handle_sdf)
	await _separated(offcuts + 2)
	print("wedge glue: sawn flush: %d offcuts" % (workshop.offcuts.size() - offcuts))
	_check(workshop.offcuts.size() == offcuts + 2 and head.get_meta("members", []).size() == 2 and
			workshop.offcuts[-1].sdf == handle_sdf and workshop.offcuts[-2].sdf == wedge_sdf,
			"the wedge's and the tenon's proud ends come off, the parts still joined")
	# Straight down onto the wedge (the tenon's middle, where the kerf is) and the tenon beside
	# it: each part's top, level with the head's.
	var tops := []
	for probe in [[wedge_sdf, Vector3(55, 35, 80)], [handle_sdf, Vector3(48, 35, 80)]]:
		probe[0].flush() # (a body answers no ray while its split is being applied)
		var hit: Dictionary = probe[0].raycast(head_sdf.to_global(probe[1]), level.normalized(), 1.0)
		tops.append((hit.position.y - head_top) * 1000.0 if not hit.is_empty() else INF)
	print("wedge glue: the wedge's top %.2f mm over the head's, the tenon's %.2f" % [tops[0], tops[1]])
	_check(absf(tops[0]) < 0.2 and absf(tops[1]) < 0.2, "flush with the head")
	workshop.debris.clear()
	await _look_at(head_sdf.to_global(Vector3(55, 35, 55)))
	await _shot("mallet_wedged")

	print("wedge glue: %s" % ("the mallet glued, wedged and sawn flush" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A saw stroke straight on a part's body across the handle's end, level at `at` (world):
## from the tenon's cheek (the handle's -z face), along its width, through.
func _saw(body, at: Vector3, handle_sdf) -> void:
	var basis: Basis = handle_sdf.global_basis
	var normal: Vector3 = (basis * Vector3(0, 0, -1)).normalized()
	var along: Vector3 = (basis * Vector3(0, 1, 0)).normalized()
	body.begin_stroke("saw", at, normal, along, {"feed": 0.5})
	for k in 8:
		body.move_stroke(at + along * (0.045 if k % 2 == 0 else -0.045))
	body.end_stroke()


func _separated(count: int) -> void:
	for i in 600:
		if workshop.offcuts.size() >= count:
			break
		await _frames(1)
	await _frames(2)


func _member(piece: RigidBody3D, sdf) -> Dictionary:
	for m in piece.get_meta("members", []):
		if m.sdf == sdf:
			return m
	return {}


## A part made as drawn, in the hands.
func _carried(part: String) -> RigidBody3D:
	var piece: RigidBody3D = workshop.part_as_drawn("mallet", part)
	piece.global_position = Vector3(0.3, 0.1, 0.25)
	workshop.pick_up(piece)
	return piece


## Pushed in with the wheel as far as it goes.
func _home() -> void:
	for i in 62:
		_wheel(true)


func _look_at(point: Vector3) -> void:
	workshop.player.face(point)
	await _frames(2)


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
		push_error("wedge glue: " + what)
