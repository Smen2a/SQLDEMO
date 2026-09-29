extends "res://tests/harness.gd"

## The planes (the Planes slot, workshop.gd's "spokeshave", by kind), driven through the
## workshop's own pointer methods:
## - the block plane with its chamfer fence, pushed along the back edge between gauge lines
##   3 mm in on the top and the back: edge lock holds it across the arris, the lines on the
##   plane between them. Each pass widens the chamfer evenly along the board, and once it
##   meets the lines a pass takes nothing;
## - the shoulder plane along a rebate gauged 8 mm wide and 1.5 mm deep on the front: its
##   side set on the width line, it takes the rebate down to the depth line right into the
##   inside corner, and leaves the shoulder square.
## Each pass is pressed a few millimetres in from the board's end; a plane starts at the end.

const Workshop := preload("res://workshop/workshop.tscn")

var workshop
var _failed := false


func _ready() -> void:
	get_tree().create_timer(float(user_arg("--timeout", "600"))).timeout.connect(func():
		push_error("planes: timed out")
		get_tree().quit(1))
	workshop = Workshop.instantiate()
	add_child(workshop)
	workshop.enter_work(false) # (at the bench, over the board in the vise)
	await _frames(3)
	var layout = workshop.layout
	var top := _depth(Vector3(0.0, 0.03, 0.0))

	# The chamfer's lines: 3 mm in on the top from the back edge, and on the back from the top.
	workshop.select_tool("layout")
	workshop.set_setting("layout", "variant", "gauge")
	workshop.set_setting("layout", "distance", 3.0)
	_click(Vector3(0.0, 0.025, -0.046))
	await _settle()
	var on_back: Dictionary = layout.line_at(Vector3(0.0, 0.022, -0.05), Vector3.FORWARD)
	layout.scribe(on_back, 0.0, on_back.length)
	await _settle()
	# The block plane with its fence, from behind (seen from the front the chamfer faces away).
	workshop.select_tool("spokeshave")
	workshop.set_setting("spokeshave", "variant", "block_plane_fence")
	workshop.set_setting("spokeshave", "depth", 0.5)
	var orbit = workshop.camera
	orbit.yaw = PI
	orbit._apply()
	var taken := []
	for i in 10:
		var before := _section(0.0)
		await _push(Vector3(-0.074, 0.025, -0.048), Vector3(0.09, 0.025, -0.048))
		taken.append(_section(0.0) - before)
		if taken.back() < 0.01:
			break
	var sections := [-0.05, 0.0, 0.05].map(func(x): return _section(x))
	var mid := _depth(Vector3(0.0, 0.03, -0.0485)) - top
	var beyond := _depth(Vector3(0.0, 0.03, -0.0465)) - top
	print("planes: a 3 mm chamfer with the fenced block plane in %d passes (%s mm^2 of its section off each); along the board %s mm^2; 1.5 mm in %.2f mm down, past the line %.3f" % [
			taken.size(), ", ".join(taken.map(func(t): return "%.2f" % t)),
			", ".join(sections.map(func(t): return "%.2f" % t)), mid, beyond])
	_check(taken.back() < 0.01 and taken.size() <= 8, "the chamfer comes to its lines, then a pass takes nothing")
	_check(sections.all(func(a): return absf(a - 4.5) < 0.25), "even along the board (3 x 3 mm: 4.5 mm^2)")
	_check(absf(mid - 1.5) < 0.15 and beyond < 0.01, "on the plane between the lines, and not past them")
	orbit.yaw = 0.0
	orbit._apply()

	# The rebate's lines: 8 mm in on the top from the front edge, 1.5 mm down the front.
	workshop.select_tool("layout")
	workshop.set_setting("layout", "distance", 8.0)
	_click(Vector3(0.0, 0.025, 0.046))
	await _settle()
	workshop.set_setting("layout", "distance", 1.5)
	var on_front: Dictionary = layout.line_at(Vector3(0.0, 0.022, 0.05), Vector3.BACK)
	layout.scribe(on_front, 0.0, on_front.length)
	await _settle()
	workshop.select_tool("spokeshave")
	workshop.set_setting("spokeshave", "variant", "shoulder_plane")
	workshop.set_setting("spokeshave", "depth", 0.5)
	var floor_at := INF
	var passes := 0
	for i in 8:
		await _push(Vector3(-0.074, 0.025, 0.046), Vector3(0.09, 0.025, 0.046))
		passes += 1
		var now := _depth(Vector3(0.0, 0.03, 0.046)) - top
		if absf(now - floor_at) < 0.005:
			break
		floor_at = now
	var floor := _depth(Vector3(0.0, 0.03, 0.046)) - top
	var corner := _depth(Vector3(0.0, 0.03, 0.0423)) - top   # 0.3 mm from the width line, in the rebate
	var shoulder := _depth(Vector3(0.0, 0.03, 0.0415)) - top # 0.5 mm past it
	var near_end := _depth(Vector3(-0.078, 0.03, 0.046)) - top
	print("planes: a rebate 8 x 1.5 mm with the shoulder plane in %d passes: its floor %.3f mm down (%.3f by the end), %.3f mm 0.3 mm from the shoulder, %.3f past it" % [
			passes, floor, near_end, corner, shoulder])
	_check(absf(floor - 1.5) < 0.06 and absf(near_end - 1.5) < 0.06, "the rebate goes down to its depth line, end to end")
	_check(absf(corner - 1.5) < 0.1, "right into the inside corner")
	_check(shoulder < 0.01, "and the shoulder is left square at its line")

	print("planes: %s" % ("the planes take whole sections to the lines" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## The area (mm^2) taken off the board's back top corner at `x`: rays down every 0.25 mm over
## the 4 mm next to the back edge (0: square).
func _section(x: float) -> float:
	var area := 0.0
	for i in 16:
		area += (_depth(Vector3(x, 0.03, -0.05 + 0.00025 * (i + 0.5))) - 5.0) * 0.25
	return area


## A plane pushed from `from` to `to` (on the board's top), direct, at its working speed.
func _push(from: Vector3, to: Vector3) -> void:
	var cam: Camera3D = workshop.camera
	workshop.hover_screen(cam.unproject_position(from))
	await _frames(2)
	workshop.press(cam.unproject_position(from))
	for k in 16:
		workshop.drag_screen(cam.unproject_position(from.lerp(to, float(k + 1) / 16)))
		await _frames(1)
	await catch_up(workshop)
	workshop.release()
	await _settle()


## A click (no drag) at a point on the board: the whole line.
func _click(at: Vector3) -> void:
	var screen: Vector2 = workshop.camera.unproject_position(at)
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()


## Until the edit has landed and the board is idle: rays locking the next stroke read it as
## it is.
func _settle() -> void:
	workshop.board.flush()
	for i in 1200:
		if not workshop.board.is_busy() and not workshop.board.get_stats().get("refine_pending", false):
			break
		await _frames(1)
	await _frames(2)


## How far down (mm) a ray from `from` goes to the board.
func _depth(from: Vector3) -> float:
	var hit: Dictionary = workshop.board.raycast(from, Vector3.DOWN, 0.02)
	return hit.get("distance", INF) * 1000.0


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("planes: " + what)
