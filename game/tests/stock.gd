extends "res://tests/harness.gd"

## The rack's stock: each kind (Room.STOCK) taken off its stack is a piece of its wood and
## size (bounds, volume and mass), named as the stack is. Each is put in the vise flat, on
## edge and on end: squared, standing on the bench top between the jaws, the jaws closed
## on it. Stepping up to the bench with the ash handle blank on end, the view takes it all in.
## Rendered, with --out=<dir>: the rack from the room (stock_rack.png) and the handle blank on
## end at the bench (stock_handle.png).

const Workshop := preload("res://workshop/workshop.tscn")
const Room := preload("res://workshop/room.gd")
const MM := 0.001
const DENSITY := {"ash": 0.67, "oak": 0.75, "walnut": 0.64} # g/cm^3 (core materials)

var workshop
var _failed := false
var _out := ""


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("stock: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	for kind in Room.STOCK:
		var spec: Dictionary = Room.STOCK[kind]
		workshop.set_wood(kind) # (a fresh piece of it in the vise, all else cleared)
		var piece: RigidBody3D = workshop.clamped
		var sdf = piece.get_meta("sdf")
		var size: Vector3 = sdf.get_body_bounds().size
		var volume: float = sdf.get_volume()
		var box: float = spec.size.x * spec.size.y * spec.size.z
		print("stock: %s, %s %s, %.0f x %.0f x %.0f mm, %.0f mm^3 (%.3f of its box), %.0f g" % [kind, spec.wood,
				spec.kind, size.x, size.y, size.z, volume, volume / box, piece.mass * 1000.0])
		_check(piece.get_meta("wood_name") == spec.wood and piece.get_meta("kind") == spec.kind,
				"%s: named as its stack (%s %s)" % [kind, piece.get_meta("wood_name"), piece.get_meta("kind")])
		_check((size - spec.size).abs().length() < 0.5, "%s: its size" % kind)
		# Short of the box by its eased arrises (and what the ADF's voxels miss round the surface:
		# under 5% on the thinnest).
		_check(volume < box and volume > 0.95 * box, "%s: its volume" % kind)
		_check(absf(piece.mass - volume * DENSITY[spec.wood] * 1e-6) < 0.02 * piece.mass, "%s: its mass" % kind)
		# Into the vise flat, on edge and on end (as it was turned when let go).
		for up in 3:
			_in_vise(piece, up, spec.size)
		# Another off its stack, into the hands.
		var taken: RigidBody3D = workshop.take_stock(workshop.room.stacks[kind])
		_check(taken != null and workshop.held == taken and taken.get_meta("kind") == spec.kind and
				taken.get_meta("wood_name") == spec.wood, "%s: one taken off its stack" % kind)
		if taken != null:
			var top: Vector3 = workshop.room.stacks[kind].get_meta("top")
			var lies: float = workshop._extent(taken.get_meta("sdf"), taken.get_meta("sdf").global_transform).size.y
			_check(absf(lies / MM - spec.size.z) < 0.5, "%s: lying flat as the stack's do" % kind)
			_check(taken.global_position.distance_to(top) < 0.1, "%s: from the top of its stack" % kind)
		await _frames(1)
	# The rack, seen from the room.
	var player = workshop.player
	player.global_position = Vector3(-1.9, player.global_position.y, 1.0)
	player.face(Room.RACK_AT + Vector3(0.0, 0.65, 0.0))
	await _shot("stock_rack")
	# The ash handle blank on end, stepping up to the bench: all of it in view.
	workshop.set_wood("handle_ash")
	var handle: RigidBody3D = workshop.clamped
	workshop._unclamp()
	_in_vise(handle, 0, Room.STOCK.handle_ash.size)
	_check(workshop.enter_work(false), "at the bench with the handle blank on end")
	var cam: Camera3D = workshop.camera
	var extent: AABB = workshop._extent(workshop.board, workshop.board.global_transform)
	var seen := true
	for i in 8:
		seen = seen and cam.is_position_in_frustum(extent.get_endpoint(i))
	print("stock: the handle on end, %.0f mm tall, seen from %.2f m" % [extent.size.y / MM, cam.distance])
	_check(seen, "all of it in view")
	await _shot("stock_handle")
	workshop.leave_work()
	print("stock: %s" % ("the stock is as the rack says" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The piece let go into the vise with its body axis `up` (0: x, the length; 1: y; 2: z)
## vertical: squared, standing on the bench top at the vise's middle, the jaws on it.
func _in_vise(piece: RigidBody3D, up: int, size: Vector3) -> void:
	var sdf = piece.get_meta("sdf")
	if workshop.clamped != null:
		workshop._unclamp()
	var axes := [Vector3.ZERO, Vector3.ZERO, Vector3.ZERO]
	axes[up] = Vector3.UP
	axes[(up + 1) % 3] = Vector3.RIGHT
	axes[(up + 2) % 3] = Vector3.UP.cross(Vector3.RIGHT) # (right-handed: a turn of up, right, their cross)
	var turn := Basis(axes[0], axes[1], axes[2])
	piece.global_basis = turn * sdf.transform.basis.orthonormalized().inverse()
	workshop._put_in_vise(piece)
	var extent: AABB = workshop._extent(sdf, sdf.global_transform)
	var names := ["on end", "on edge", "flat"]
	var tall: float = extent.size.y / MM
	var jaws: Array = workshop.room.jaws
	var gap: float = absf(jaws[1].position.z - jaws[0].position.z) - workshop.room.JAW.z
	_check(workshop.clamped == piece, "%s: clamped %s" % [piece.get_meta("kind"), names[up]])
	_check(absf(tall - size[up]) < 0.5, "%s %s: %.1f mm tall (%.1f)" % [piece.get_meta("kind"), names[up], tall, size[up]])
	_check(absf(extent.position.y) < 0.0005 and absf(extent.get_center().x) < 0.001 and
			absf(extent.get_center().z) < 0.001, "%s %s: on the bench top at the vise's middle" % [
			piece.get_meta("kind"), names[up]])
	_check(extent.size.x >= extent.size.z - 0.0005, "%s %s: its longer side along the bench" % [
			piece.get_meta("kind"), names[up]])
	_check(absf(gap - extent.size.z) < 0.001, "%s %s: the jaws closed on it" % [piece.get_meta("kind"), names[up]])


## Rendered, with --out: the view as it is, saved as <out>/<name>.png.
func _shot(name: String) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	for i in 3:
		await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_out)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_out, name])


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("stock: " + what)


func _frames(n: int) -> void:
	for k in n:
		await get_tree().process_frame
