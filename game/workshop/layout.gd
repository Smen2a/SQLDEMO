extends Node3D

## Laying out (the Layout slot on the hotbar): lines marked on the work before it is cut, for
## the tools to go to.
##   marking gauge   its fence rides one of the piece's faces as sawn (its bounds: the
##                   reference faces a fence rides); the pin scribes a line on the face under
##                   the pointer, parallel to the edge between them, `distance` mm in (the
##                   wheel sets it, Ctrl finely).
##   marking knife   drawn along a square whose stock rides the nearest edge: a line across
##                   the face, square to that edge, through the pointer.
##   pencil          a line in pencil: a click draws one across the face, square to the
##                   nearest edge; a drag, along the edge or across it as the drag goes.
##   plan sheet      a part of a plan (plans.gd), laid on the piece: its face side on the
##                   face pointed at, from the nearer end and edge; a click draws it on in
##                   pencil, and the piece becomes that part.
## Hovered, the line shows where it would go. A click scribes it the whole way across the face,
## a drag as far as the drag goes. Either way it is a real cut (a knife's V, 0.3 mm deep: core
## tools/layout.h), one undo step, and a mark on the piece: body space (millimetres), so it
## moves with the piece. Marks are drawn while a tool is in hand at the bench.
## Pencil only guides: nothing holds to it. The knife or gauge by a pencil line (within SNAP)
## takes that line, knifed or gauged in as the plan (or the pencil) meant it ("as").

const MM := 0.001
const LINE_COLOUR := Color(1.0, 0.35, 0.15)
const PREVIEW_COLOUR := Color(1.0, 0.85, 0.3)
const HELD_COLOUR := Color(0.35, 0.9, 1.0) # the lines the stroke in hand is held to
const LIFT := 0.05 # mm the lines are drawn off the face (the scribe is below it)
const DRAG := 2.0 # mm a drag goes before it scribes only as far as it goes
const SCRIBE_DEPTH := 0.3 # mm (core tools/layout.h kScribeDepth)
const SCRIBE_STEP := 5.0 # mm the scribe is drawn along at a time
const PENCIL_COLOUR := Color(0.3, 0.3, 0.34)
const SHEET_COLOUR := Color(0.2, 0.45, 1.0) # a plan's sheet laid on the piece, before a click draws it on
const UNFIT_COLOUR := Color(1.0, 0.25, 0.2) # a sheet laid on a piece too small for it
const SNAP := 1.5 # mm: the knife or gauge this near a pencil line takes it

var workshop
## The line under the pointer (body space, see line_at()), or {}.
var preview := {}
## The plan sheet laid on the piece under the pointer (plans.gd placement_at()), or {}.
var placed := {}
var _press := {}  # while the button is down: {"line", "from", "to" (mm along it), "dragged"}; the sheet's: {"plan"}
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
	var at := _face_at(point, normal)
	if at.is_empty():
		return {}
	var lo: Vector3 = at.lo
	var hi: Vector3 = at.hi
	var p: Vector3 = at.p
	var a: int = at.a
	var e: int = at.e
	var origin := Vector3.ZERO
	origin[at.i] = at.face_at
	var dir := Vector3.ZERO
	if kind() == "gauge":
		var at_line: float = at.edge - at.side * distance()
		if at_line <= lo[a] or at_line >= hi[a]:
			return {}
		origin[a] = at_line
		origin[e] = lo[e]
		dir[e] = 1.0
		return {"kind": "gauge", "face": at.face, "origin": origin, "dir": dir, "length": hi[e] - lo[e],
				"toward": at.toward, "distance": distance(), "edge": at.edge}
	# The knife, along the square: across the face, square to that edge, through the point.
	origin[e] = clampf(p[e], lo[e], hi[e])
	origin[a] = lo[a]
	dir[a] = 1.0
	var across := Vector3.ZERO
	across[e] = 1.0
	return {"kind": "knife", "face": at.face, "origin": origin, "dir": dir, "length": hi[a] - lo[a], "toward": across}


## Where a point (world, with the surface's normal) lies on the piece in the vise's bounds
## faces: {"lo", "hi" (the bounds, body space), "p" (the point), "i" (the face's axis), "face"
## (its outward normal), "face_at", "a" (the axis across to the nearest of its four edges),
## "side" (-1 or 1, which way), "edge" (where that edge is along a), "toward" (towards it),
## "e" (the axis along it)}; {} with no piece.
func _face_at(point: Vector3, normal: Vector3) -> Dictionary:
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
	var toward := Vector3.ZERO
	toward[a] = side
	return {"lo": lo, "hi": hi, "p": p, "i": i, "face": face, "face_at": hi[i] if n[i] > 0.0 else lo[i], "a": a,
			"side": side, "edge": hi[a] if side > 0.0 else lo[a], "toward": toward, "e": k if a == j else j}


## The pencil at a point: a line across the face, square to the nearest edge, through the
## point (a click draws it the whole way across; a drag, see _drawn()).
func _pencil_at(point: Vector3, normal: Vector3) -> Dictionary:
	var at := _face_at(point, normal)
	if at.is_empty():
		return {}
	var lo: Vector3 = at.lo
	var hi: Vector3 = at.hi
	var a: int = at.a
	var e: int = at.e
	var origin := Vector3.ZERO
	origin[at.i] = at.face_at
	origin[e] = clampf(at.p[e], lo[e], hi[e])
	origin[a] = lo[a]
	var dir := Vector3.ZERO
	dir[a] = 1.0
	var across := Vector3.ZERO
	across[e] = 1.0
	return {"kind": "pencil", "as": "knife", "face": at.face, "origin": origin, "dir": dir, "length": hi[a] - lo[a],
			"toward": across, "from": 0.0, "to": hi[a] - lo[a], "at": at}


## The pencil drawn from `start` to `now` (body space, on the face): along the nearest edge
## (parallel to it: a line gauged from it) or across it, whichever way the drag went further.
func _drawn(line: Dictionary, start: Vector3, now: Vector3) -> Dictionary:
	var at: Dictionary = line.at
	var lo: Vector3 = at.lo
	var hi: Vector3 = at.hi
	var a: int = at.a
	var e: int = at.e
	var origin := Vector3.ZERO
	origin[at.i] = at.face_at
	var dir := Vector3.ZERO
	var drawn := {"kind": "pencil", "face": at.face, "at": at}
	if absf(now[e] - start[e]) >= absf(now[a] - start[a]):
		origin[a] = clampf(start[a], lo[a], hi[a])
		origin[e] = lo[e]
		dir[e] = 1.0
		drawn.merge({"as": "gauge", "toward": at.toward, "distance": absf(origin[a] - at.edge), "edge": at.edge,
				"length": hi[e] - lo[e], "from": clampf(minf(start[e], now[e]), lo[e], hi[e]) - lo[e],
				"to": clampf(maxf(start[e], now[e]), lo[e], hi[e]) - lo[e]})
	else:
		origin[e] = clampf(start[e], lo[e], hi[e])
		origin[a] = lo[a]
		dir[a] = 1.0
		var across := Vector3.ZERO
		across[e] = 1.0
		drawn.merge({"as": "knife", "toward": across, "length": hi[a] - lo[a],
				"from": clampf(minf(start[a], now[a]), lo[a], hi[a]) - lo[a],
				"to": clampf(maxf(start[a], now[a]), lo[a], hi[a]) - lo[a]})
	drawn.origin = origin
	drawn.dir = dir
	return drawn


## The knife or gauge by a pencil line on the face it is on (within SNAP mm of the point,
## body space): that line, to be knifed or gauged in as it was meant to be; else `line`.
func _snap(line: Dictionary, p: Vector3, face: Vector3) -> Dictionary:
	var piece = workshop.holder()
	if piece == null:
		return line
	var best := SNAP
	var found := {}
	for mark in marks_of(piece):
		if mark.kind != "pencil" or mark.as == "guide" or mark.face.dot(face) < 0.9:
			continue
		var s := clampf((p - mark.origin).dot(mark.dir), mark.from, mark.to)
		var d := p.distance_to(mark.origin + mark.dir * s)
		if d < best:
			best = d
			found = mark
	if found.is_empty():
		return line
	var snapped := found.duplicate()
	snapped.kind = found.as
	snapped.snapped = true
	snapped.erase("serial")
	snapped.erase("step")
	return snapped


## The pointer over the work: where the line would go (the sheet: where the part would).
func hover(position: Vector2) -> void:
	if not _press.is_empty():
		return
	var hit: Dictionary = workshop._surface_at(position)
	preview = {}
	placed = {}
	if hit.is_empty() or hit.get("stale", false):
		return
	var normal: Vector3 = hit.get("own_normal", hit.normal)
	match kind():
		"plan":
			placed = workshop.plans.placement_at(hit.position, normal)
		"pencil":
			preview = _pencil_at(hit.position, normal)
		_:
			var at := _face_at(hit.position, normal)
			if not at.is_empty():
				preview = _snap(line_at(hit.position, normal), at.p, at.face)


## Button down: the scribe starts from the pointer's place along the line (the sheet is laid
## on when it comes up).
func press(position: Vector2) -> void:
	hover(position)
	if kind() == "plan":
		if not placed.is_empty():
			_press = {"plan": placed}
		return
	if preview.is_empty():
		return
	var s := _along(preview, position)
	_press = {"line": preview, "from": s, "to": s, "dragged": false, "start": _on_face(preview, position)}


## A drag: the scribe goes as far as the pointer, along the line (the pencil: along the edge
## or across it, as the drag goes).
func drag(position: Vector2) -> void:
	if _press.is_empty():
		hover(position)
		return
	if _press.has("plan"):
		return
	if _press.line.kind == "pencil":
		var now := _on_face(_press.line, position)
		if now.distance_to(_press.start) >= DRAG:
			_press.dragged = true
		if _press.dragged:
			_press.line = _drawn(_press.line, _press.start, now)
			_press.from = _press.line.from
			_press.to = _press.line.to
		return
	var s := _along(_press.line, position)
	_press.to = s
	if absf(s - _press.from) >= DRAG:
		_press.dragged = true


## Button up: the line is scribed (as far as it was dragged, or the whole way: a pencil line
## taken, its own length), or drawn in pencil; the sheet is laid on.
func release() -> void:
	if _press.is_empty():
		return
	if _press.has("plan"):
		var sheet: Dictionary = _press.plan
		_press = {}
		if workshop.plans.transfer(sheet):
			placed = {} # (drawn on: seen in pencil now, until the pointer moves)
		return
	var line: Dictionary = _press.line
	var from: float = _press.from if _press.dragged else line.get("from", 0.0)
	var to: float = _press.to if _press.dragged else line.get("to", line.length)
	_press = {}
	if line.kind == "pencil":
		var mark := line.duplicate()
		mark.erase("at")
		mark.from = minf(from, to)
		mark.to = maxf(from, to)
		if mark.to - mark.from >= DRAG:
			workshop.plans.pencil(mark)
		return
	scribe(line, from, to)


## Scribes `line` from `from` to `to` mm along it: a knife's V drawn along it (one undo
## step), and a mark on the piece.
func scribe(line: Dictionary, from: float, to: float) -> void:
	var sdf = workshop.board
	var piece = workshop.holder()
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
	mark.erase("snapped")
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


## Holds a stroke to the lines marked on the piece in the vise (unless it was made freely,
## Alt): what they mean depends on how the tool is held (`lock`: the workshop's, world space;
## its point, normal and path may be set here). Returns the limits for SdfBody (body space:
## {"floors", "sides", "ends"}, each an Array of Vector4, the plane's normal towards the
## waste and its offset), and notes the marks it held to (drawn blue); {} with none near.
##   Flat on a face:
##     - a gauge line on the face beside, gauged from this face's edge: the floor, this face
##       cut down to it (a rebate's depth), the tool squared to the face;
##     - a gauge line on this face: a wall, the waste between it and its edge (a shoulder);
##     - a knife line on this face: a wall, the waste on the side the stroke is on.
##     Walls along the way the tool goes are its sides: a chisel's or spokeshave's side is set
##     flush on one; those across its way are its ends. The saw's line snaps onto a knife
##     line along it, the kerf on the waste side. A brace's bit is kept inside them all.
##   Across the corner (a chisel's or gouge's edge lock, or any tool but the saw laid on a
##   chamfer begun): a chamfer. Its plane runs through the gauge lines on the two faces (one
##   line: at 45 degrees, as far in on the other face); the tool is laid on it, and cuts down
##   to it and no further.
func hold(lock: Dictionary, tool: String) -> Dictionary:
	held = []
	var piece = workshop.holder()
	if piece == null or workshop.board == null or lock.get("free", false) or tool == "sanding_sponge":
		return {}
	var marks := marks_of(piece).filter(func(m): return m.kind != "pencil") # (pencil only guides)
	if marks.is_empty():
		return {}
	var floors := []
	var walls := []
	var across := tool != "saw" and tool != "brace" and _chamfer(lock, marks, floors, walls)
	var p := to_body(lock.point)
	var f := _axis(dir_to_body(lock.normal))
	if not across:
		for mark in marks:
			var face: Vector3 = mark.face
			if mark.kind == "gauge" and absf(face.dot(f)) < 0.1 and mark.toward.dot(f) > 0.9:
				if _floor_here(mark, marks, f, p):
					floors.append(Stop.new(mark.origin, f))
					held.append(mark)
			elif face.dot(f) > 0.9:
				var d: Vector3 = p - mark.origin
				var waste: Vector3 = mark.toward
				if mark.kind == "knife" and waste.dot(d) < 0.0:
					waste = -waste
				if waste.dot(d) < 0.0:
					continue # (this stroke is on the side it keeps: not its line)
				walls.append([Stop.new(mark.origin, waste), mark])
	if not floors.is_empty() and not across:
		_square(lock, f)
	var path := dir_to_body(lock.path)
	var sides := []
	var ends := []
	for w in walls:
		var stop: Stop = w[0]
		if absf(stop.normal.dot(path)) < 0.5:
			if tool == "saw":
				if w[1].kind == "knife" and stop.at(p) < 4.0:
					_on_the_line(lock, stop, w[1])
					held.append(w[1])
				continue
			sides.append(stop)
			if tool in ["chisel", "gouge", "spokeshave"]:
				_flush(lock, stop, tool)
		else:
			ends.append(stop)
		held.append(w[1])
	if floors.is_empty() and sides.is_empty() and ends.is_empty():
		return {}
	return {"floors": floors.map(func(s): return s.vector()), "sides": sides.map(func(s): return s.vector()),
			"ends": ends.map(func(s): return s.vector())}


## The marks the stroke in hand is held to (drawn blue).
var held := []


## A plane a tool stops at (body space): at(q) > 0 on the waste side.
class Stop:
	var normal: Vector3
	var offset: float

	func _init(point: Vector3, waste: Vector3) -> void:
		normal = waste.normalized()
		offset = -normal.dot(point)

	func at(q: Vector3) -> float:
		return normal.dot(q) + offset

	func vector() -> Vector4:
		return Vector4(normal.x, normal.y, normal.z, offset)


## Whether a depth line (a gauge line on the face beside, from this face's edge) holds a
## stroke at `p` on this face (its normal `f`): where the face is gauged from the same edge
## too (a rebate's width line), only within that line's strip; otherwise all over the face.
func _floor_here(line: Dictionary, marks: Array, f: Vector3, p: Vector3) -> bool:
	var widths := 0
	for mark in marks:
		if mark.kind == "gauge" and mark.face.dot(f) > 0.9 and mark.toward.dot(line.face) > 0.9:
			widths += 1
			if mark.toward.dot(p - mark.origin) > -0.05:
				return true
	return widths == 0


## Across the corner: the chamfer between the gauge lines on the two faces (a pair gauged from
## the edge between them; or one, at 45 degrees), its plane the tool's; knife lines across the
## edge its ends. Only where the stroke was locked within the corner those lines cut off, and
## with the tool across it: the edge lock's corner hold, or laid on a slope between the two
## faces (a chamfer begun: its own folds are then the edges found, and the lock snaps flush
## off them, so where it was locked is what counts). Says whether it held.
func _chamfer(lock: Dictionary, marks: Array, floors: Array, walls: Array) -> bool:
	var p0 := to_body(lock.get("free_point", lock.point))
	var surface := dir_to_body(lock.get("surface_normal", lock.get("free_normal", lock.normal)))
	var on1 = null
	var on2 = null
	for a in marks:
		if a.kind != "gauge":
			continue
		var depth_a: float = a.face.dot(a.origin - p0) # how far below a's face the point is
		var in_a: float = a.toward.dot(p0 - a.origin) # how far past a's line, towards its edge
		if depth_a < -0.5 or in_a < -0.5:
			continue
		var pair = null
		for b in marks:
			if b.kind == "gauge" and b.face.dot(a.toward) > 0.9 and b.toward.dot(a.face) > 0.9:
				pair = b
		var reach: float = pair.distance if pair != null else a.distance
		if depth_a > reach + 0.5:
			continue
		on1 = a
		on2 = pair
		break
	if on1 == null:
		return false
	var f1: Vector3 = on1.face
	var f2: Vector3 = on1.toward
	if not lock.get("corner", false) and (surface.dot(f1) > 0.95 or surface.dot(f2) > 0.95 or surface.dot(f1 + f2) < 0.5):
		return false # flat on one of the faces: a rebate's lines, not a chamfer's
	# A point of each line (one line: the other as far in on the other face, 45 degrees).
	var p1: Vector3 = on1.origin
	var p2: Vector3 = on2.origin if on2 != null else on1.origin + f2 * on1.distance - f1 * on1.distance
	var along := f1.cross(f2).normalized()
	var n := (p2 - p1).cross(along).normalized()
	if n.dot(f1 + f2) < 0.0:
		n = -n
	floors.append(Stop.new(p1, n))
	held.append(on1)
	if on2 != null:
		held.append(on2)
	# The tool laid on that plane, on the corner as it is now, where it was locked: the corner
	# the faces meet at (the edge the line was gauged from), abreast of that point.
	var corner: Vector3 = p0 + f1 * f1.dot(on1.origin - p0) + f2 * (f2.dot(on1.origin - p0) + on1.distance)
	var normal := dir_to_world(n)
	var on: Dictionary = workshop.board.raycast(to_world(corner) + normal * 0.005, -normal, 0.01)
	if not on.is_empty() and not on.get("stale", false):
		# Along the corner, the way the stroke goes (not along a fold of the chamfer begun, which
		# an unfinished pass leaves running off it); a tool turned on its way (Q / E) as turned.
		var way := dir_to_world(along)
		if way.dot(lock.path) < 0.0:
			way = -way
		var turned: float = lock.path.signed_angle_to(lock.along, lock.normal)
		lock.point = on.position
		lock.normal = normal
		lock.plane = Plane(normal, on.position)
		lock.path = way
		lock.along = way.rotated(normal, turned)
	# A knife line across the edge on either face stops it (a stopped chamfer).
	var p := to_body(lock.point)
	for mark in marks:
		if mark.kind == "knife" and (mark.face.dot(f1) > 0.9 or mark.face.dot(f2) > 0.9) and absf(mark.dir.dot(along)) < 0.5:
			var waste: Vector3 = mark.toward if mark.toward.dot(p - mark.origin) >= 0.0 else -mark.toward
			walls.append([Stop.new(mark.origin, waste), mark])
	return true


## Held to a floor gauged from the face it is on: the tool square to that face (the normal
## fitted round the pointer tips several degrees within a few millimetres of an edge, and
## would tilt the floor it leaves). Within 15 degrees of it; further off, the face under it
## is sloped, and it is left as it is.
func _square(lock: Dictionary, face: Vector3) -> void:
	var normal := dir_to_world(face)
	if normal.dot(lock.normal) < cos(deg_to_rad(15.0)):
		return
	lock.normal = normal
	lock.plane = Plane(normal, lock.point)
	for key in ["path", "along"]:
		var way: Vector3 = lock[key] - normal * normal.dot(lock[key])
		if way.length() > 1e-3:
			lock[key] = way.normalized()


## A chisel's, spokeshave's or plane's side set flush on a wall along its way: moved (in the
## face) just far enough that its edge (a plane's sole) keeps to the waste side.
func _flush(lock: Dictionary, stop: Stop, tool: String) -> void:
	# (A plane by its sole, which its iron may not reach the side of: a block plane's leaves a
	# strip by the wall, a shoulder plane's none.)
	var width: float = workshop.variant().get("sole_width" if tool == "spokeshave" else "width", 12.0)
	var p := to_body(lock.point)
	var n := dir_to_body(lock.normal)
	var across := n.cross(dir_to_body(lock.path)).normalized()
	var worst := stop.at(p) - absf(stop.normal.dot(across)) * 0.5 * width
	var inward := stop.normal - n * n.dot(stop.normal)
	if worst >= 0.0 or inward.length() < 1e-3:
		return
	var shift := inward.normalized() * ((-worst + 0.02) / inward.length())
	lock.point = to_world(p + shift)
	lock.plane = Plane(lock.normal, lock.point)


## The saw's line onto a knife line along its way: its kerf on the waste side.
func _on_the_line(lock: Dictionary, stop: Stop, mark: Dictionary) -> void:
	var p := to_body(lock.point)
	var half_kerf := 0.4 # mm (core Saw::kerf 0.8)
	lock.point = to_world(p - stop.normal * (stop.at(p) - half_kerf))
	var dir := dir_to_world(mark.dir)
	lock.path = dir if dir.dot(lock.path) >= 0.0 else -dir
	lock.along = lock.path
	lock.plane = Plane(lock.normal, lock.point)


## The bounds axis nearest a direction (body space), as a unit vector.
func _axis(v: Vector3) -> Vector3:
	var i := 0
	for axis in 3:
		if absf(v[axis]) > absf(v[i]):
			i = axis
	var out := Vector3.ZERO
	out[i] = signf(v[i])
	return out


## For the line by the pointer.
func describe() -> String:
	match kind():
		"plan":
			return _describe_sheet()
		"pencil":
			if preview.is_empty():
				return "Pencil   (hover over a face)"
			var at: Dictionary = preview.at
			var p: Vector3 = at.p
			var e: int = at.e
			return "Pencil   %.1f mm from the edge, %.1f mm from the end of it   click: a line across, drag: along the edge or across it" % [
					absf(p[at.a] - at.edge), minf(p[e] - at.lo[e], at.hi[e] - p[e])]
	var tool := "Marking gauge %.1f mm in" % distance() if kind() == "gauge" else "Marking knife and square"
	if preview.get("snapped", false):
		var what := "gauge" if preview.kind == "gauge" else "knife"
		var of: String = (" (%s)" % preview.feature) if preview.get("feature", "") != "" else ""
		return "%s   on the pencil line%s: click: %s it in, drag: as far as you go" % [tool, of, what]
	if kind() == "gauge":
		if preview.is_empty():
			return tool + "   (hover over a face, near the edge its fence rides)"
		return tool + "   click: along the whole face, drag: as far as you go"
	if preview.is_empty():
		return tool + "   (hover over a face)"
	return tool + "   click: across the whole face, drag: as far as you go"


func _describe_sheet() -> String:
	var plans = workshop.plans
	var p: Dictionary = plans.sheet_part()
	if p.is_empty():
		return "Plan sheet   (take one from the plan book: P)"
	var name: String = "%s: %s" % [plans.plans[plans.sheet.plan].get("name", plans.sheet.plan), p.get("name", p.id)]
	if placed.get("on_end", false):
		return name + "   (lay it on a face or an edge, not an end: it runs along the grain)"
	if placed.is_empty():
		return name + "   (hover over the piece: its face side on the face you point at, from the nearer end and edge)"
	if not placed.fits:
		var size: Vector3 = plans.size_of(p)
		return name + "   too small for it: it needs %.0f x %.0f x %.0f mm" % [size.x, size.y, size.z]
	var said := name + "   click: draw it on in pencil (%d lines" % placed.marks.size()
	if placed.later > 0:
		said += "; %d more once the piece is cut to size" % placed.later
	return said + ")"


func _process(_delta: float) -> void:
	var mesh: ImmediateMesh = _mesh.mesh
	mesh.clear_surfaces()
	var sdf = workshop.board
	var piece = workshop.holder()
	if sdf == null or piece == null or workshop.mode != workshop.Mode.WORK or workshop.current == "":
		return
	var lines := []
	# Pencil first: a line knifed or gauged in over it is drawn on top.
	for mark in marks_of(piece):
		if mark.kind == "pencil":
			lines.append([mark, mark.from, mark.to, PENCIL_COLOUR])
	for mark in marks_of(piece):
		if mark.kind != "pencil":
			lines.append([mark, mark.from, mark.to, HELD_COLOUR if held.has(mark) else LINE_COLOUR])
	if workshop.current == "layout":
		if _press.has("line"):
			var line: Dictionary = _press.line
			var from: float = _press.from if _press.dragged else line.get("from", 0.0)
			var to: float = _press.to if _press.dragged else line.get("to", line.length)
			lines.append([line, from, to, PREVIEW_COLOUR])
		elif not preview.is_empty():
			lines.append([preview, preview.get("from", 0.0), preview.get("to", preview.length), PREVIEW_COLOUR])
		var sheet: Dictionary = _press.get("plan", placed)
		# (Not over the part drawn on there already: its pencil shows.)
		var tag: Dictionary = piece.get_meta("part", {})
		var plans = workshop.plans
		if not sheet.is_empty() and not plans.sheet.is_empty() and tag.get("plan", "") == plans.sheet.plan and \
				tag.get("part", "") == plans.sheet.part and tag.placement.is_equal_approx(sheet.get("placement", Transform3D())):
			sheet = {}
		for mark in sheet.get("marks", []):
			lines.append([mark, mark.from, mark.to, SHEET_COLOUR if sheet.fits else UNFIT_COLOUR])
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
	return clampf((_on_face(line, position) - line.origin).dot(line.dir), 0.0, line.length)


## Where the pointer is on `line`'s face's plane (body space; the line's origin if the ray
## misses it).
func _on_face(line: Dictionary, position: Vector2) -> Vector3:
	var cam: Camera3D = workshop.camera
	var plane := Plane(dir_to_world(line.face), to_world(line.origin))
	var at = plane.intersects_ray(cam.project_ray_origin(position), cam.project_ray_normal(position))
	return line.origin if at == null else to_body(at)


func to_body(point: Vector3) -> Vector3:
	return workshop.board.global_transform.affine_inverse() * point


func to_world(point: Vector3) -> Vector3:
	return workshop.board.global_transform * point


func dir_to_body(direction: Vector3) -> Vector3:
	return (workshop.board.global_transform.basis.inverse() * direction).normalized()


func dir_to_world(direction: Vector3) -> Vector3:
	return (workshop.board.global_transform.basis * direction).normalized()
