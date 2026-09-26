extends "res://tests/harness.gd"

## Saws a rebate off the end of the workshop board, two cuts meeting that no plane
## separates: one 15 mm down from the top face 10 mm from the end, one 15 mm in from the end
## face 8 mm below the top. The strip between them comes away as a piece of its own, hidden
## until it has taken in the region that cuts it out, then resting in the rebate on the
## board's own surface; undo puts it back. Prints the check's and the split's times.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false


func _ready() -> void:
	get_tree().create_timer(600.0).timeout.connect(func():
		push_error("island split: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	var board = workshop.board
	var steps: int = board.get_stats().get("steps", 0)
	# Down from the top at x = 70 mm, then in from the end face (x = 80) at z = 4.
	_saw(board, Vector3(70, 0, 12.5), Vector3(0, 0, 1))
	_saw(board, Vector3(80, 0, 4), Vector3(1, 0, 0))
	for k in 600:
		if not workshop.offcuts.is_empty():
			break
		await _frames(1)
	if workshop.offcuts.is_empty():
		push_error("island split: the rebate did not come away (%s)" % board.get_stats().get("island_failed", ""))
		get_tree().quit(1)
		return
	var offcut: Dictionary = workshop.offcuts.back()
	var piece = offcut.piece
	var body: RigidBody3D = offcut.body
	var stats: Dictionary = board.get_stats()
	_check(offcut.island, "it came away as an island (no plane)")
	_check(not piece.visible and body.freeze, "hidden and held until it is cut out")
	var strip := 9.6 * 100.0 * 8.1
	print("island split: found and cut out in %.0f ms (worker), split in %.1f ms (main thread); the strip %.0f mm^3 (%.0f), %.0f g" % [
			stats.get("island_ms", 0.0), stats.get("split_ms", 0.0), piece.get_volume(), strip, body.mass * 1000.0])
	_check(absf(piece.get_volume() - strip) < 0.1 * strip, "the strip's volume")
	for k in 3000:
		if piece.visible and not body.freeze:
			break
		await get_tree().physics_frame
	_check(piece.visible and not body.freeze, "shown, and let go, once cut out")
	var spawn: Transform3D = offcut.spawn
	for k in 60:
		await get_tree().physics_frame
	var moved := (body.global_position - spawn.origin) * 1000.0
	print("island split: after a second the strip moved %.1f mm down, %.1f mm across" % [-moved.y,
			Vector2(moved.x, moved.z).length()])
	_check(moved.y > -1.2 and moved.y < 0.2, "it rests in the rebate (dropped at most the kerf)")
	_check(Vector2(moved.x, moved.z).length() < 1.0, "and stays there")
	workshop.undo()
	board.flush()
	_check(workshop.offcuts.is_empty() and board.get_stats().get("steps", 0) == steps + 2, "undo puts it back")
	print("island split: %s" % ("the rebate came away" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A saw stroke from `at` (board mm, on a face whose normal is `normal`), across the board's
## width, worked back and forth until 15 mm deep (feed 0.1 mm a mm).
func _saw(board, at: Vector3, normal: Vector3) -> void:
	var along := Vector3(0, 1, 0)
	var basis: Basis = board.global_basis
	board.begin_stroke("saw", board.to_global(at), (basis * normal).normalized(), (basis * along).normalized(),
			{"feed": 0.1})
	for y in [30.0, -30.0, 30.0]:
		board.move_stroke(board.to_global(at + along * y))
	board.end_stroke()


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("island split: " + what)


func _frames(n: int) -> void:
	for k in n:
		await get_tree().process_frame
