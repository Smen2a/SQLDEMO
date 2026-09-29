extends "res://tests/harness.gd"

## Cuts a crumb off the workshop board: three scribed V grooves, 2.7 mm deep, meet round
## the front right top arris, leaving a sliver of it about 5 mm long apart from the board
## (along the top 1.8 mm from the front face, along the front face 1.8 mm below the top, and
## across the top 6 mm from the end). Once the board is idle it takes the sliver out as an
## undo step of its own (no offcut: it is under 30 mm^3), and it comes away as a debris
## chunk, a hull coloured like the wood. A ray down through where it was reaches the board
## below it. Undo puts it back and takes the chunk away. Prints the look's times. Rendered,
## with --out=<dir>, saves a close view of the chunk once it has landed (crumbs_chunk.png).

const Workshop := preload("res://workshop/workshop.tscn")
const DEPTH := 2.7
const IN := 1.8 # mm the grooves along the arris are in from its faces

var workshop
var _failed := false
var _out := ""


func _ready() -> void:
	get_tree().create_timer(600.0).timeout.connect(func():
		push_error("crumbs: timed out")
		get_tree().quit(1))
	_out = user_arg("--out", "")
	workshop = Workshop.instantiate()
	add_child(workshop)
	workshop.enter_work(false) # (at the bench, over the board in the vise)
	await _frames(3)
	var board = workshop.board
	var crumbled := []
	board.crumbled.connect(func(chunks: Array, step: int): crumbled.append({"chunks": chunks, "step": step}))
	var steps: int = board.get_stats().get("steps", 0)
	var offcuts: int = workshop.offcuts.size()
	var made: int = workshop.debris.made.chunk
	_check(_top_below(board) > 12.0, "the board is whole to begin with")
	_scribe(board, Vector3(71, -50 + IN, 12.5), Vector3(0, 0, 1), Vector3(1, 0, 0), 12.0)
	_scribe(board, Vector3(71, -50, 12.5 - IN), Vector3(0, -1, 0), Vector3(1, 0, 0), 12.0)
	_scribe(board, Vector3(74, -53, 12.5), Vector3(0, 0, 1), Vector3(0, 1, 0), 9.0)
	for k in 1200:
		if not crumbled.is_empty():
			break
		await _frames(1)
	if crumbled.is_empty():
		push_error("crumbs: nothing crumbled (%s)" % board.get_stats())
		get_tree().quit(1)
		return
	await _frames(1) # (the workshop takes them in deferred)
	var stats: Dictionary = board.get_stats()
	var chunks: Array = crumbled[0].chunks
	_check(chunks.size() == 1, "one chunk came away (%d)" % chunks.size())
	if chunks.is_empty():
		get_tree().quit(1)
		return
	var chunk: Dictionary = chunks[0]
	var centre: Vector3 = board.to_local(chunk.centre)
	print("crumbs: a crumb of %.1f mm^3 at (%.1f, %.1f, %.1f), %.1f mg, a hull of %d corners and %d faces; looked for in %.0f ms, cut out and measured in %.0f ms (worker)" % [
			chunk.volume, centre.x, centre.y, centre.z, chunk.mass * 1e6, chunk.points.size(),
			chunk.triangles.size() / 3, stats.get("island_ms", 0.0), stats.get("crumb_ms", 0.0)])
	_check(chunk.volume > 1.0 and chunk.volume < 30.0, "a crumb's volume")
	_check(centre.x > 74.0 and centre.x < 80.0 and centre.y < -50 + IN and centre.z > 12.5 - IN, "where the sliver was")
	_check(chunk.points.size() >= 4 and chunk.triangles.size() >= 12 and chunk.triangles.size() % 3 == 0 and \
			chunk.colours.size() == chunk.points.size(), "a hull, coloured per corner")
	_check(crumbled[0].step == steps + 4 and stats.get("steps", 0) == steps + 4, "taken out as a step of its own")
	_check(workshop.offcuts.size() == offcuts, "not a piece of work")
	_check(workshop.debris.made.chunk == made + 1 and
			workshop.debris.pieces.any(func(p): return p.kind == "chunk"), "a chunk of debris")
	var gone := _top_below(board)
	_check(gone < 11.0, "a ray down through the sliver meets the board below it (%.1f mm)" % gone)
	await _shot("crumbs_chunk")
	workshop.undo()
	board.flush()
	_check(board.get_stats().get("steps", 0) == steps + 3, "undo takes the step back")
	_check(not workshop.debris.pieces.any(func(p): return p.kind == "chunk"), "and takes the chunk away")
	_check(_top_below(board) > 12.0, "and puts the sliver back: the ray meets it again")
	for k in 60:
		await _frames(1)
	_check(crumbled.size() == 1, "and it stays (%d)" % crumbled.size())
	print("crumbs: %s" % ("the crumb came away" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A scribed line from `from` (board mm, on a face whose normal is `normal`), `length` mm
## along `along`, DEPTH deep.
func _scribe(board, from: Vector3, normal: Vector3, along: Vector3, length: float) -> void:
	var basis: Basis = board.global_basis
	board.begin_stroke("scribe", board.to_global(from), (basis * normal).normalized(), (basis * along).normalized(),
			{"depth": DEPTH})
	for k in range(1, int(length) + 1):
		board.move_stroke(board.to_global(from + along * float(k)))
	board.end_stroke()


## Where a ray down through the middle of the sliver first meets the board (board mm, z).
func _top_below(board) -> float:
	var hit: Dictionary = board.raycast(board.to_global(Vector3(77.5, -49.4, 30.0)),
			(board.global_basis * Vector3(0, 0, -1)).normalized(), 1.0)
	return board.to_local(hit.position).z if not hit.is_empty() and not hit.get("stale", false) else -100.0


## Rendered, with --out: the chunk, once it has landed, close up, saved as <out>/<name>.png.
func _shot(name: String) -> void:
	if _out == "" or DisplayServer.get_name() == "headless":
		return
	var chunk: Array = workshop.debris.pieces.filter(func(p): return p.kind == "chunk")
	if chunk.is_empty():
		return
	for k in 90:
		await get_tree().physics_frame
	var cam = workshop.camera
	cam.target = chunk[0].body.global_position
	cam.distance = 0.05
	cam.yaw = PI # (from behind the board: the vise's jaw stands in front of the arris)
	cam.pitch = -0.6
	cam._apply()
	for i in 3:
		await RenderingServer.frame_post_draw
	DirAccess.make_dir_recursive_absolute(_out)
	get_viewport().get_texture().get_image().save_png("%s/%s.png" % [_out, name])


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("crumbs: " + what)


func _frames(n: int) -> void:
	for k in n:
		await get_tree().process_frame
