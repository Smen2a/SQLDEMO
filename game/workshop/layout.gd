extends Node3D

## Laying out (the Layout slot on the hotbar): lines marked on the work before it is cut, for
## the tools to go to.
##   marking gauge   its fence rides one of the piece's faces as sawn (its bounds: the
##                   reference faces a fence rides); the pin scribes a line on the face under
##                   the pointer, parallel to the edge between them, `distance` mm in (the
##                   wheel sets it, Ctrl finely).
##   marking knife   drawn along a square whose stock rides the nearest edge: a line across
##                   the face, square to that edge, through the pointer.
## Hovered, the line shows where it would go. A click scribes it the whole way across the face,
## a drag as far as the drag goes. Either way it is a real cut (a knife's V, 0.3 mm deep: core
## tools/layout.h), one undo step, and a mark on the piece: body space (millimetres), so it
## moves with the piece. Marks are drawn while a tool is in hand at the bench.

const MM := 0.001
const LINE_COLOUR := Color(1.0, 0.35, 0.15)
const PREVIEW_COLOUR := Color(1.0, 0.85, 0.3)
const LIFT := 0.05 # mm the lines are drawn off the face (the scribe is below it)
const DRAG := 2.0 # mm a drag goes before it scribes only as far as it goes
const SCRIBE_DEPTH := 0.3 # mm (core tools/layout.h kScribeDepth)
const SCRIBE_STEP := 5.0 # mm the scribe is drawn along at a time

var workshop
## The line under the pointer (body space, see line_at()), or {}.
var preview := {}
var _press := {}  # while the button is down: {"line", "from", "to" (mm along it), "dragged"}
var _mesh: MeshInstance3D


func _ready() -> void:
	_mesh = MeshInstance3D.new()
	_mesh.mesh = ImmediateMesh.new()
	_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var lines := StandardMaterial3D.new()
	lines.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	lines.vertex_color_use_as_albedo = true
	_mesh.material_override = lines
	add_child(_mesh)


func kind() -> String:
	return workshop.settings.layout.variant


func distance() -> float:
	return workshop.settings.layout.distance


## The line the tool in hand would scribe at a point on the piece in the vise (world, with the
## surface's normal there), in its body space:
##   "kind"    "gauge" or "knife";
##   "face"    the face's outward normal (a bounds axis);
##   "origin", "dir", "length"   the line: from origin along dir, length mm;
##   "toward"  (in the face, square to the line) towards the edge the gauge was set from:
##             the waste side, for a gauge line;
##   "distance", "edge"          the gauge's setting, and where that edge is along `toward`.
## Or {} (off the piece, or the gauge set wider than the face).
func line_at(point: Vector3, normal: Vector3) -> Dictionary:
	var sdf = workshop.board
	if sdf == null:
		return {}
	var bounds: AABB = sdf.get_body_bounds()
	var lo := bounds.position
	var hi := bounds.end
	var p := to_body(point)
	var n := dir_to_body(normal)
	var i := 0
	for axis in 3:
		if absf(n[axis]) > absf(n[i]):
			i = axis
	var face := Vector3.ZERO
	face[i] = signf(n[i])
	var face_at: float = hi[i] if n[i] > 0.0 else lo[i]
	# The nearest of the face's four edges: across (a) from the point, running along (e).
	var j := (i + 1) % 3
	var k := (i + 2) % 3
	var best := INF
	var a := j
	var side := -1.0
	for c in [[p[j] - lo[j], j, -1.0], [hi[j] - p[j], j, 1.0], [p[k] - lo[k], k, -1.0], [hi[k] - p[k], k, 1.0]]:
		if c[0] < best:
			best = c[0]
			a = c[1]
			side = c[2]
	var e := k if a == j else j
	var toward := Vector3.ZERO
	toward[a] = side
	var edge: float = hi[a] if side > 0.0 else lo[a]
	var origin := Vector3.ZERO
	origin[i] = face_at
	var dir := Vector3.ZERO
	if kind() == "gauge":
		var at: float = edge - side * distance()
		if at <= lo[a] or at >= hi[a]:
			return {}
		origin[a] = at
		origin[e] = lo[e]
		dir[e] = 1.0
		return {"kind": "gauge", "face": face, "origin": origin, "dir": dir, "length": hi[e] - lo[e], "toward": toward,
				"distance": distance(), "edge": edge}
	# The knife, along the square: across the face, square to that edge, through the point.
	origin[e] = clampf(p[e], lo[e], hi[e])
	origin[a] = lo[a]
	dir[a] = 1.0
	var across := Vector3.ZERO
	across[e] = 1.0
	return {"kind": "knife", "face": face, "origin": origin, "dir": dir, "length": hi[a] - lo[a], "toward": across}


## The pointer over the work: where the line would go.
func hover(position: Vector2) -> void:
	if not _press.is_empty():
		return
	var hit: Dictionary = workshop._surface_at(position)
	preview = {} if hit.is_empty() or hit.get("stale", false) else line_at(hit.position, hit.get("own_normal", hit.normal))


## Button down: the scribe starts from the pointer's place along the line.
func press(position: Vector2) -> void:
	hover(position)
	if preview.is_empty():
		return
	var s := _along(preview, position)
	_press = {"line": preview, "from": s, "to": s, "dragged": false}


## A drag: the scribe goes as far as the pointer, along the line.
func drag(position: Vector2) -> void:
	if _press.is_empty():
		hover(position)
		return
	var s := _along(_press.line, position)
	_press.to = s
	if absf(s - _press.from) >= DRAG:
		_press.dragged = true


## Button up: the line is scribed (as far as it was dragged, or the whole way).
func release() -> void:
	if _press.is_empty():
		return
	var line: Dictionary = _press.line
	var from: float = _press.from if _press.dragged else 0.0
	var to: float = _press.to if _press.dragged else line.length
	_press = {}
	scribe(line, from, to)


## Scribes `line` from `from` to `to` mm along it: a knife's V drawn along it (one undo
## step), and a mark on the piece.
func scribe(line: Dictionary, from: float, to: float) -> void:
	var sdf = workshop.board
	var piece: RigidBody3D = workshop.clamped
	if sdf == null or piece == null or line.is_empty() or absf(to - from) < DRAG:
		return
	var a: Vector3 = line.origin + line.dir * from
	var b: Vector3 = line.origin + line.dir * to
	var step: int = sdf.get_stats().get("steps", 0) + 1
	if not sdf.begin_stroke("scribe", to_world(a), dir_to_world(line.face), dir_to_world(b - a),
			{"depth": SCRIBE_DEPTH}):
		return
	var n := maxi(2, ceili(absf(to - from) / SCRIBE_STEP))
	for s in n:
		sdf.move_stroke(to_world(a.lerp(b, float(s + 1) / n)))
	sdf.end_stroke()
	var mark := line.duplicate()
	mark.from = minf(from, to)
	mark.to = maxf(from, to)
	mark.step = step
	marks_of(piece).append(mark)
	piece.set_meta("marks_undone", [])
	workshop._ui.refresh()


## The wheel: the gauge's distance, half a millimetre a notch (`fine`: a tenth).
func adjust(steps: int, fine: bool) -> void:
	var spec: Array = workshop.INTENSITY.layout
	var step: float = spec[1] / 5.0 if fine else spec[1]
	workshop.set_setting("layout", "distance", clampf(snappedf(distance() + steps * step, step), spec[2], spec[3]))


## The marks on a piece (its meta "marks": an array of lines, as line_at(), with "from",
## "to" and "step": the undo step that scribed it).
func marks_of(piece: Object) -> Array:
	if not piece.has_meta("marks"):
		piece.set_meta("marks", [])
	return piece.get_meta("marks")


## Undo of `piece`'s step `step`: its marks from then on go (to come back on redo).
func undo_step(piece: Object, step: int) -> void:
	if piece == null:
		return
	var kept := []
	var undone: Array = piece.get_meta("marks_undone", [])
	for mark in marks_of(piece):
		if mark.step >= step:
			undone.append(mark)
		else:
			kept.append(mark)
	piece.set_meta("marks", kept)
	piece.set_meta("marks_undone", undone)


## Redo of `piece`'s step `step`: its marks come back.
func redo_step(piece: Object, step: int) -> void:
	if piece == null:
		return
	var undone: Array = piece.get_meta("marks_undone", [])
	var still := []
	for mark in undone:
		if mark.step <= step:
			marks_of(piece).append(mark)
		else:
			still.append(mark)
	piece.set_meta("marks_undone", still)


## Another stroke made on `piece`: what was undone cannot come back.
func forget_redo(piece: Object) -> void:
	if piece != null:
		piece.set_meta("marks_undone", [])


## For the line by the pointer.
func describe() -> String:
	if kind() == "gauge":
		var said := "Marking gauge %.1f mm in" % distance()
		if preview.is_empty():
			return said + "   (hover over a face, near the edge its fence rides)"
		return said + "   click: along the whole face, drag: as far as you go"
	if preview.is_empty():
		return "Marking knife and square   (hover over a face)"
	return "Marking knife and square   click: across the whole face, drag: as far as you go"


func _process(_delta: float) -> void:
	var mesh: ImmediateMesh = _mesh.mesh
	mesh.clear_surfaces()
	var sdf = workshop.board
	var piece: RigidBody3D = workshop.clamped
	if sdf == null or piece == null or workshop.mode != workshop.Mode.WORK or workshop.current == "":
		return
	var lines := []
	for mark in marks_of(piece):
		lines.append([mark, mark.from, mark.to, LINE_COLOUR])
	if workshop.current == "layout":
		if not _press.is_empty():
			var line: Dictionary = _press.line
			var from: float = _press.from if _press.dragged else 0.0
			var to: float = _press.to if _press.dragged else line.length
			lines.append([line, from, to, PREVIEW_COLOUR])
		elif not preview.is_empty():
			lines.append([preview, 0.0, preview.length, PREVIEW_COLOUR])
	if lines.is_empty():
		return
	mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	for l in lines:
		var line: Dictionary = l[0]
		var lift: Vector3 = line.face * LIFT
		mesh.surface_set_color(l[3])
		mesh.surface_add_vertex(to_world(line.origin + line.dir * l[1] + lift))
		mesh.surface_add_vertex(to_world(line.origin + line.dir * l[2] + lift))
	mesh.surface_end()


## How far along `line` (mm) the pointer is, on its face's plane.
func _along(line: Dictionary, position: Vector2) -> float:
	var cam: Camera3D = workshop.camera
	var plane := Plane(dir_to_world(line.face), to_world(line.origin))
	var at = plane.intersects_ray(cam.project_ray_origin(position), cam.project_ray_normal(position))
	if at == null:
		return 0.0
	return clampf((to_body(at) - line.origin).dot(line.dir), 0.0, line.length)


func to_body(point: Vector3) -> Vector3:
	return workshop.board.global_transform.affine_inverse() * point


func to_world(point: Vector3) -> Vector3:
	return workshop.board.global_transform * point


func dir_to_body(direction: Vector3) -> Vector3:
	return (workshop.board.global_transform.basis.inverse() * direction).normalized()


func dir_to_world(direction: Vector3) -> Vector3:
	return (workshop.board.global_transform.basis * direction).normalized()
