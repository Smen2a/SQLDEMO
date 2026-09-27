extends "res://tests/harness.gd"

## What the tools take off and what comes away land and lie still: nothing sinks into what it
## rests on (the board in the vise, the bench, the floor) or keeps shaking there. Driven
## through the workshop's own methods, in game time:
## - a chisel's shaving, pared mid-board, and a spokeshave's come to rest on the board;
## - the chisel's shaving and a chip, off the bench's end, fall to the floor and lie on it;
## - a strip sawn off rests on the bench;
## - a loose block the chisel and the sanding block push along and off the board's end;
## - the strip let go from carry height over the floor, and over the bench;
## - the strip carried with the eyes on the bench top nearer than it is held: it rests on the
##   bench, not driven into it.
## For each, one line: how deep it went into anything at worst and at the end (the deepest
## contact its shapes make, from the physics space), and when it came to lie still.
## --report-only just reports; --trace prints each watched body every tenth of a second (where it is,
## how fast it goes, how deep it is in, what it overlaps).

const Workshop := preload("res://workshop/workshop.tscn")
const MM := 0.001
const RESTING := 0.0006   # m: as deep as a body resting on something may be in it (the solver's slop is 0.2 mm)
const LANDING := 0.003    # m: as deep as anything may go, landing
const STILL_SPEED := 0.005 # m/s
const STILL_TURN := 0.5    # rad/s
const STILL_FOR := 0.5     # s

var workshop
var _failed := false
var _report_only := false
var _trace := false


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "600"))).timeout.connect(func():
		push_error("physics calm: timed out")
		get_tree().quit(1))
	_report_only = user_arg("--report-only", "") != ""
	_trace = user_arg("--trace", "") != ""
	workshop = Workshop.instantiate()
	add_child(workshop)
	workshop.enter_work(false) # (at the bench, over the board in the vise)
	await _frames(3)
	var debris = workshop.debris

	# A chisel pared along the grain mid-board: its shaving rests on the board.
	workshop.set_setting("chisel", "depth", 0.5)
	workshop.set_setting("chisel", "angle", 30.0)
	var shaving := await _stroke_for_debris("chisel", Vector3(-0.0785, 0.025, -0.02), Vector3(-0.03, 0.025, -0.02))
	if not shaving.is_empty():
		var seen := await _watch(shaving.body, 2.5, shaving)
		_verdict("the chisel's shaving, on the board", seen, 1.5)
		var top := _top_of_board()
		var low := _lowest(shaving.body)
		print("physics calm:   its underside %.2f mm from the board's top (%.1f mm up)" % [(low - top) * 1000.0, low * 1000.0])
		_check(low > top - RESTING, "the chisel's shaving rests on the board, not in it")
		# Off the bench's end: it falls to the floor and lies there.
		_drop(shaving.body, Vector3(0.6, 0.06, -0.1))
		_verdict("the chisel's shaving, off the bench onto the floor", await _watch(shaving.body, 2.5, shaving), 1.5)
	# A chip off the bench's end.
	var before: int = debris.pieces.size()
	debris._add_chip({"size": Vector3(4.0, 2.0, 1.5) * MM, "transform": Transform3D(Basis(), Vector3(0.6, 0.05, 0.1)),
			"colour": Color(0.8, 0.7, 0.5), "volume": 12.0}, 999, 0.67)
	if debris.pieces.size() > before:
		var chip: Dictionary = debris.pieces.back()
		_verdict("a chip, off the bench onto the floor", await _watch(chip.body, 2.5, chip), 1.5)

	# A spokeshave's shaving, on the board.
	var shaved := await _stroke_for_debris("spokeshave", Vector3(-0.06, 0.025, -0.035), Vector3(-0.01, 0.025, -0.035))
	if not shaved.is_empty():
		_verdict("the spokeshave's shaving, on the board", await _watch(shaved.body, 2.5, shaved), 1.5)
		var low := _lowest(shaved.body)
		_check(low > _top_of_board() - RESTING, "the spokeshave's shaving rests on the board, not in it (%.1f mm up)"
				% (low * 1000.0))

	# A strip sawn off (as offcut_physics saws it): it rests on the bench.
	var strip := await _saw_off()
	if strip == null:
		_check(false, "the board did not come apart")
		_finish()
		return
	_verdict("the sawn strip, on the bench", await _watch(strip, 2.0), 1.5)

	# A loose block on the board, pushed along and off its end: by the chisel, and by the
	# sanding block.
	var block := _loose_block(Vector3(0.03, 0.025 + 0.004 + 0.0002, -0.02))
	await _physics(10)
	_stroke("chisel", Vector3(0.0, 0.025, -0.02), Vector3(0.078, 0.025, -0.02)) # (watched as it goes)
	_verdict("a loose block the chisel pushed off the end", await _watch(block, 6.0), 6.0)
	await _idle()
	block.global_position = Vector3(-0.03, 0.025 + 0.004 + 0.0002, -0.025)
	block.global_basis = Basis()
	block.linear_velocity = Vector3.ZERO
	block.angular_velocity = Vector3.ZERO
	await _physics(10)
	_stroke("sanding_block", Vector3(-0.065, 0.025, -0.025), Vector3(0.075, 0.025, -0.025))
	_verdict("a loose block the sanding block pushed off the end", await _watch(block, 6.0), 6.0)
	await _idle()
	block.queue_free()

	# Carried: the strip let go from carry height over the floor, and over the bench.
	workshop.leave_work()
	var player = workshop.player
	workshop.pick_up(strip)
	player.face(player.eye() + Vector3(0.0, 0.0, 1.0))
	workshop.reach_held(100)
	await _seconds(1.2)
	workshop.let_go()
	_verdict("the strip, let go over the floor from 1.2 m out", await _watch(strip, 3.0), 2.5)
	workshop.pick_up(strip)
	var over_bench := Vector3(0.25, 0.5, 0.0)
	player.face(over_bench)
	workshop._hold = player.eye().distance_to(over_bench)
	await _seconds(1.5)
	workshop.let_go()
	_verdict("the strip, let go 0.5 m over the bench", await _watch(strip, 3.0), 2.5)
	# Held out farther than the bench top the eyes are on: it rests on the bench, not in it.
	workshop.pick_up(strip)
	player.face(Vector3(0.25, 0.0, 0.1))
	workshop.reach_held(100)
	await _seconds(1.5)
	var held := await _watch(strip, 1.5)
	var mean_speed: float = held.speed
	print("physics calm: the strip held into the bench: %.1f mm in at worst, %.1f at the end, moving %.3f m/s on average" % [
			held.worst * 1000.0, held.end * 1000.0, mean_speed])
	_check(held.end <= RESTING and held.worst <= LANDING and mean_speed < 0.05,
			"held into the bench, it rests on it: not driven into it, not shaking")
	workshop.let_go()
	_finish()


func _finish() -> void:
	print("physics calm: %s" % ("pieces and debris land and lie still" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed and not _report_only else 0)


## A stroke of `tool` from `from` to `to` (world, on the board's top), direct, followed at the
## tool's working speed; then what came away (the newest piece of debris), or {}.
func _stroke_for_debris(tool: String, from: Vector3, to: Vector3) -> Dictionary:
	var before: int = workshop.debris.pieces.size()
	await _stroke(tool, from, to)
	var waited := 0.0
	while workshop.debris.pieces.size() <= before and waited < 2.0:
		await get_tree().physics_frame
		waited += get_physics_process_delta_time()
	if workshop.debris.pieces.size() <= before:
		_check(false, "the %s's shaving did not come away" % tool)
		return {}
	return workshop.debris.pieces.back()


func _stroke(tool: String, from: Vector3, to: Vector3) -> void:
	workshop.select_tool(tool)
	await _frames(8)
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(from))
	await _frames(2)
	workshop.press(cam.unproject_position(from))
	for k in 16:
		workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 16)))
		await _frames(1)
	await catch_up(workshop)
	workshop.release()
	workshop.board.flush()
	workshop.select_tool("")


## Saws a strip off the board's front (offcut_physics' cut): the offcut's body, or null.
func _saw_off() -> RigidBody3D:
	workshop.select_tool("saw")
	workshop.set_setting("saw", "feed", 0.1)
	await _frames(8)
	var cam: Camera3D = workshop.camera
	var start := Vector3(0.0, 0.025, 0.03)
	workshop.hover_screen(cam.unproject_position(start))
	await _frames(2)
	workshop.lock(cam.unproject_position(start))
	workshop.aim(cam.unproject_position(Vector3(-0.03, 0.025, 0.03)))
	workshop.press(cam.unproject_position(start))
	var from := start
	for i in 12:
		var to := Vector3(-0.03 if i % 2 == 0 else 0.03, 0.025, 0.03)
		for k in 2:
			workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 2)))
			await _frames(1)
		await catch_up(workshop)
		from = to
	workshop.release()
	workshop.unlock()
	workshop.board.flush()
	workshop.select_tool("")
	var waited := 0.0
	while workshop.offcuts.is_empty() and waited < 5.0:
		await _frames(1)
		waited += get_process_delta_time()
	return null if workshop.offcuts.is_empty() else workshop.offcuts.back().body


## A loose block on the board, as tool_planning's: 16 x 8 x 16 mm, 5 g to the solver.
func _loose_block(at: Vector3) -> RigidBody3D:
	var block := RigidBody3D.new()
	block.mass = 0.005
	var surface := PhysicsMaterial.new()
	surface.friction = 0.5
	block.physics_material_override = surface
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	shape.shape.size = Vector3(0.016, 0.008, 0.016)
	shape.shape.margin = workshop.SHAPE_MARGIN
	block.add_child(shape)
	add_child(block)
	block.global_position = at
	return block


## A body set down (still) at `at`, to fall from there.
func _drop(body: RigidBody3D, at: Vector3) -> void:
	body.global_position = at
	body.linear_velocity = Vector3.ZERO
	body.angular_velocity = Vector3.ZERO
	body.sleeping = false


## Watches a body for `seconds` of game time (a piece of debris's `entry` kept from fading):
## {"worst": the deepest it went into anything (m), "end": at the end, "still": when it came
## to lie still for good (s, or -1), "speed": its mean speed (m/s)}.
func _watch(body: RigidBody3D, seconds: float, entry := {}) -> Dictionary:
	var t := 0.0
	var worst := 0.0
	var depth := 0.0
	var calm_since := -1.0
	var speed := 0.0
	var steps := 0
	while t < seconds and is_instance_valid(body):
		await get_tree().physics_frame
		t += get_physics_process_delta_time()
		if not entry.is_empty():
			entry.rest = -1.0
			entry.moving = workshop.debris._clock
		depth = _depth(body)
		worst = maxf(worst, depth)
		if _trace and steps % 6 == 0:
			print("    t %.2f at %s v %.3f w %.2f depth %.2f mm engaged %s tool %s touching %s" % [t, body.global_position,
					body.linear_velocity.length(), body.angular_velocity.length(), depth * 1000.0, workshop.is_engaged(),
					workshop.current, _touching(body)])
		speed += body.linear_velocity.length()
		steps += 1
		var calm: bool = body.sleeping or body.freeze or (body.linear_velocity.length() < STILL_SPEED and
				body.angular_velocity.length() < STILL_TURN)
		if not calm:
			calm_since = -1.0
		elif calm_since < 0.0:
			calm_since = t
	var still := calm_since if calm_since >= 0.0 and t - calm_since >= STILL_FOR else -1.0
	return {"worst": worst, "end": depth, "still": still, "speed": speed / maxf(steps, 1)}


## One line for what was watched, and whether it landed and lay still within `within` s.
func _verdict(what: String, seen: Dictionary, within: float) -> void:
	print("physics calm: %s: %.2f mm in at worst, %.2f at the end; %s" % [what, seen.worst * 1000.0, seen.end * 1000.0,
			("still after %.2f s" % seen.still) if seen.still >= 0.0 else "never still"])
	_check(seen.worst <= LANDING and seen.end <= RESTING, "%s: not sunk into anything" % what)
	_check(seen.still >= 0.0 and seen.still <= within, "%s: lies still" % what)


## The deepest contact (m) a body's shapes make with anything in the physics space it is not
## meant to pass through (its collision exceptions, other than a frozen body's, are left out).
func _depth(body: RigidBody3D) -> float:
	var exclude: Array[RID] = [body.get_rid()]
	for other in body.get_collision_exceptions():
		if not (other is RigidBody3D and other.freeze):
			exclude.append(other.get_rid())
	var deepest := 0.0
	for child in body.get_children():
		if not (child is CollisionShape3D) or child.disabled:
			continue
		var query := PhysicsShapeQueryParameters3D.new()
		query.shape = child.shape
		query.transform = child.global_transform
		query.exclude = exclude
		query.collision_mask = body.collision_mask | body.collision_layer
		var points: Array = get_world_3d().direct_space_state.collide_shape(query, 16)
		for i in range(0, points.size() - 1, 2):
			deepest = maxf(deepest, (points[i] as Vector3).distance_to(points[i + 1]))
	return deepest


## What a body's shapes overlap (for the trace).
func _touching(body: RigidBody3D) -> String:
	var names := []
	for child in body.get_children():
		if child is CollisionShape3D and not child.disabled:
			var query := PhysicsShapeQueryParameters3D.new()
			query.shape = child.shape
			query.transform = child.global_transform
			query.exclude = [body.get_rid()]
			query.collision_mask = 0xFFFF
			for hit in get_world_3d().direct_space_state.intersect_shape(query, 8):
				var c: Object = hit.collider
				var what: String = c.get_class()
				if c.has_meta("kind"):
					what = c.get_meta("kind")
				elif c.has_meta("bench"):
					what = "bench"
				elif c.has_meta("ground"):
					what = "ground"
				elif c.get_parent() == workshop.debris:
					what = "debris"
				names.append("%s(%s)" % [what, "frozen" if c is RigidBody3D and c.freeze else "l%d" % c.collision_layer])
	return ",".join(names)


## The lowest point of a body's shapes (world y).
func _lowest(body: RigidBody3D) -> float:
	var lowest := INF
	for child in body.get_children():
		if child is CollisionShape3D and child.shape is BoxShape3D:
			var h: Vector3 = child.shape.size * 0.5
			for c in 8:
				var corner := Vector3(h.x if c & 1 else -h.x, h.y if c & 2 else -h.y, h.z if c & 4 else -h.z)
				lowest = minf(lowest, (child.global_transform * corner).y)
	return lowest


## The top of the board in the vise (world y).
func _top_of_board() -> float:
	var board = workshop.board
	return workshop._extent(board, board.global_transform).end.y


## Until no stroke is on the board (one started alongside a watch has ended).
func _idle() -> void:
	var waited := 0.0
	while (workshop.is_engaged() or workshop.current != "") and waited < 30.0:
		await get_tree().physics_frame
		waited += get_physics_process_delta_time()


func _physics(n: int) -> void:
	for i in n:
		await get_tree().physics_frame


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
		if _report_only:
			print("physics calm: (would fail) " + what)
		else:
			push_error("physics calm: " + what)
