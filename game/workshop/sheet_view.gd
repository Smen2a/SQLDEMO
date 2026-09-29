extends Control

## A part's sheet, drawn as a woodworker's drawing (the plan book, plan_viewer.gd, and the
## pad, plan_editor.gd): the part's face side (length across, width down), its face edge below
## it (length, thickness) and its end beside it (thickness, width), each seen from outside the
## part with the reference corner at its top left; each face's lines (SdfBody.part_lines), the
## far face's dashed; what the features take out, hidden, dashed; the sizes.
##
## A point on a view is in that view's own millimetres: the face side's (x, y), the face
## edge's (x, z), the end's (z, y). to_screen() and view_at() map them to the control and
## back (snapped to half millimetres, and onto the blank's outline near it), for drawing a
## part with the mouse.

const INK := Color(0.16, 0.14, 0.13)
const FAINT := Color(0.16, 0.14, 0.13, 0.5)
const SIZES := Color(0.6, 0.22, 0.14) # dimensions
const PICKED := Color(0.15, 0.4, 0.95) # the feature chosen, and one being drawn
const MARGIN := 44.0 # px round the views (room for their sizes)
const GAP := 36.0    # px between them
const TOP := 30.0    # px above them for the title
const SNAP := 8.0    # px: a point this near the blank's outline goes onto it

enum { FACE, EDGE, END }
const VIEW_NAMES := ["face side", "face edge", "end"]
## Each face: the axis square to it, and whether it lies at the far side of the blank.
const FACES := {"side": [2, false], "back": [2, true], "edge": [1, false], "other_edge": [1, true],
		"end": [0, false], "far_end": [0, true]}

var title := ""
## The chosen feature (its index in the part's features), drawn in PICKED; -1 none.
var picked := -1
## A drag being drawn on a view: {"view", "a", "b"} (view mm), shown as a dashed box.
var rubber := {}
var _part := {}
var _lines := []


## Shows a part (a plan's part dictionary, as game/plans/*.json has them). Call again (or
## refresh()) after changing it.
func set_part(p: Dictionary) -> void:
	_part = p
	refresh()


func get_part() -> Dictionary:
	return _part


## Lays the part's lines out again (after its size or features changed) and redraws.
func refresh() -> void:
	var s := size_of(_part)
	_lines = SdfBody.part_lines(_part, s).lines if s.x > 0.4 and s.y > 0.4 and s.z > 0.4 else []
	queue_redraw()


static func size_of(p: Dictionary) -> Vector3:
	var s: Array = p.get("size", [0, 0, 0])
	return Vector3(s[0], s[1], s[2]) if s.size() == 3 else Vector3.ZERO


## The scale (px a mm) and where each view lies: {"k", "corners", "extents" (view mm)}; {}
## where nothing fits.
func frame() -> Dictionary:
	var s := size_of(_part)
	var area := size - Vector2(2.0 * MARGIN + GAP, 2.0 * MARGIN + GAP + TOP)
	if area.x <= 0.0 or area.y <= 0.0 or s.x <= 0.0 or s.y <= 0.0 or s.z <= 0.0:
		return {}
	var k := minf(area.x / (s.x + s.z), area.y / (s.y + s.z))
	var face := Vector2(MARGIN, MARGIN + TOP)
	return {"k": k, "corners": [face, face + Vector2(0.0, s.y * k + GAP), face + Vector2(s.x * k + GAP, 0.0)],
			"extents": [Vector2(s.x, s.y), Vector2(s.x, s.z), Vector2(s.z, s.y)]}


## A point on a view (view mm) in the control.
func to_screen(view: int, at: Vector2) -> Vector2:
	var f := frame()
	return Vector2.ZERO if f.is_empty() else f.corners[view] + at * f.k


## The same on the screen (for pushing mouse events at it).
func to_global(view: int, at: Vector2) -> Vector2:
	return get_global_transform() * to_screen(view, at)


## The view under a point of the control and the point on it (view mm, snapped): {"view",
## "at"}, or {} off the views.
func view_at(pos: Vector2) -> Dictionary:
	var f := frame()
	if f.is_empty():
		return {}
	for view in 3:
		var rect := Rect2(f.corners[view], f.extents[view] * f.k).grow(SNAP)
		if rect.has_point(pos):
			return {"view": view, "at": point_on(view, pos)}
	return {}


## A point of the control on a view (view mm): snapped to half millimetres, onto the blank's
## outline within SNAP px of it, and kept on the view.
func point_on(view: int, pos: Vector2) -> Vector2:
	var f := frame()
	if f.is_empty():
		return Vector2.ZERO
	var k: float = f.k
	var extent: Vector2 = f.extents[view]
	var at: Vector2 = (pos - f.corners[view]) / k
	for i in 2:
		if absf(at[i]) * k < SNAP:
			at[i] = 0.0
		elif absf(at[i] - extent[i]) * k < SNAP:
			at[i] = extent[i]
		else:
			at[i] = clampf(snappedf(at[i], 0.5), 0.0, extent[i])
	return at


## A part-space point on a view (view mm), and back (the axis the view looks along: 0).
static func on_view(view: int, q: Vector3) -> Vector2:
	match view:
		FACE:
			return Vector2(q.x, q.y)
		EDGE:
			return Vector2(q.x, q.z)
	return Vector2(q.z, q.y)


static func in_part(view: int, at: Vector2) -> Vector3:
	match view:
		FACE:
			return Vector3(at.x, at.y, 0.0)
		EDGE:
			return Vector3(at.x, 0.0, at.y)
	return Vector3(0.0, at.y, at.x)


## The feature whose footprint on a view holds a point (view mm): the smallest; -1 none.
func feature_at(view: int, at: Vector2) -> int:
	var f := frame()
	var slack: float = 4.0 / f.k if not f.is_empty() else 0.5
	var best := -1
	var area := INF
	for foot in footprints(_part):
		var r := _rect_on(view, foot.box).grow(slack)
		if r.has_point(at) and r.get_area() < area:
			area = r.get_area()
			best = foot.feature
	return best


static func _rect_on(view: int, box: AABB) -> Rect2:
	var a := on_view(view, box.position)
	var b := on_view(view, box.end)
	return Rect2(a, b - a).abs()


## What each of a part's features takes out of its blank, as boxes (part space): {"box",
## "feature" (its index), "hidden" (drawn dashed, seen through the part)}. A hole's, a
## tenon's four cheeks', a kerf's and a rebate's are drawn; a tenon's length, a chamfer's
## corner and a taper's wedge are there to pick them by.
static func footprints(p: Dictionary) -> Array:
	var s := size_of(p)
	var out := []
	var features: Array = p.get("features", [])
	for index in features.size():
		var f: Dictionary = features[index]
		match f.get("kind", ""):
			"hole":
				var along: Array = f.get("along", [0, s.x])
				var across: Array = f.get("across", [0, 0])
				var face: String = f.get("face", "side")
				var through: bool = f.get("through", false)
				var depth: float = f.get("depth", 0.0)
				var axis: int = FACES.get(face, [2, false])[0]
				if axis == 0:
					continue
				var box := AABB(Vector3(along[0], 0, 0), Vector3(along[1] - along[0], s.y, s.z))
				var other := 3 - axis # the axis across the face
				box.position[other] = across[0]
				box.size[other] = across[1] - across[0]
				if not through:
					box = _within(box, face, depth, s)
				out.append({"box": box, "feature": index, "hidden": true})
			"tenon":
				var l: float = f.get("length", 0.0)
				var x0 := s.x - l if f.get("end", "end") == "far_end" else 0.0
				var y: Array = f.get("y", [0, s.y])
				var z: Array = f.get("z", [0, s.z])
				for b in [AABB(Vector3(x0, 0, 0), Vector3(l, y[0], s.z)), AABB(Vector3(x0, y[1], 0), Vector3(l, s.y - y[1], s.z)),
						AABB(Vector3(x0, 0, 0), Vector3(l, s.y, z[0])), AABB(Vector3(x0, 0, z[1]), Vector3(l, s.y, s.z - z[1]))]:
					if b.size.x > 0.0 and b.size.y > 0.0 and b.size.z > 0.0:
						out.append({"box": b, "feature": index, "hidden": true})
				out.append({"box": AABB(Vector3(x0, 0, 0), Vector3(l, s.y, s.z)), "feature": index, "hidden": false})
			"kerf":
				var d: float = f.get("depth", 0.0)
				var x0 := s.x - d if f.get("end", "end") == "far_end" else 0.0
				var half: float = 0.5 * f.get("width", 0.8)
				var at: float = f.get("at", 0.0)
				if f.get("axis", "y") == "z":
					out.append({"box": AABB(Vector3(x0, 0, at - half), Vector3(d, s.y, 2.0 * half)), "feature": index, "hidden": true})
				else:
					out.append({"box": AABB(Vector3(x0, at - half, 0), Vector3(d, 2.0 * half, s.z)), "feature": index, "hidden": true})
			"rebate", "chamfer":
				var x := span(f, s.x)
				var box := AABB(Vector3(x.x, 0, 0), Vector3(x.y - x.x, s.y, s.z))
				var wide: float = f.get("width", 0.0)
				var deep: float = f.get("depth", 0.0) if f.kind == "rebate" else wide
				box = _within(_within(box, f.get("other", "edge"), wide, s), f.get("face", "side"), deep, s)
				out.append({"box": box, "feature": index, "hidden": f.kind == "rebate"})
			"taper":
				var thin := minf(f.get("from", s.z), f.get("to", s.z))
				var back: bool = f.get("face", "back") == "back"
				var z0 := thin if back else 0.0
				out.append({"box": AABB(Vector3(0, 0, z0), Vector3(s.x, s.y, s.z - thin)), "feature": index, "hidden": false})
	return out


## A rebate's or chamfer's x range: its `along`, or the whole length.
static func span(f: Dictionary, length: float) -> Vector2:
	var along: Array = f.get("along", [0, 0])
	return Vector2(along[0], along[1]) if along[1] > along[0] else Vector2(0.0, length)


## A box cut down to within `d` of a face (on that face's axis).
static func _within(box: AABB, face: String, d: float, s: Vector3) -> AABB:
	var spec: Array = FACES.get(face, [2, false])
	var axis: int = spec[0]
	box.position[axis] = s[axis] - d if spec[1] else 0.0
	box.size[axis] = d
	return box


func _draw() -> void:
	var f := frame()
	if f.is_empty():
		return
	var font := get_theme_default_font()
	if title != "":
		draw_string(font, Vector2(0, 20), title, HORIZONTAL_ALIGNMENT_LEFT, -1, 20, INK)
	var k: float = f.k
	var s := size_of(_part)
	var features: Array = _part.get("features", [])
	var picked_name: String = features[picked].get("name", "") if picked >= 0 and picked < features.size() else ""
	var feet := footprints(_part)
	var nears := [Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(-1, 0, 0)]
	for view in 3:
		var corner: Vector2 = f.corners[view]
		var extent: Vector2 = f.extents[view] * k
		var at := func(q: Vector3) -> Vector2: return corner + on_view(view, q) * k
		# The chosen feature's footprint, shaded.
		for foot in feet:
			if foot.feature == picked and not foot.hidden:
				var r := _rect_on(view, foot.box)
				draw_rect(Rect2(corner + r.position * k, r.size * k), Color(PICKED, 0.12))
		draw_rect(Rect2(corner, extent), INK, false, 1.5)
		draw_string(font, corner + Vector2(0, extent.y + 16), VIEW_NAMES[view], HORIZONTAL_ALIGNMENT_LEFT, -1, 13, FAINT)
		# What the features take out, seen through the part (hidden: dashed), then the lines
		# on this face (the far face's dashed).
		for foot in feet:
			if not foot.hidden:
				continue
			var lo: Vector3 = foot.box.position.clamp(Vector3.ZERO, s)
			var r := _rect_on(view, AABB(lo, foot.box.end.clamp(Vector3.ZERO, s) - lo))
			_dashed_rect(Rect2(corner + r.position * k, r.size * k), PICKED if foot.feature == picked else FAINT)
		# A chamfer's corner, seen from the end.
		if view == END:
			for i in features.size():
				var c: Dictionary = features[i]
				if c.get("kind", "") != "chamfer":
					continue
				var w: float = c.get("width", 0.0)
				var faces := [c.get("face", "side"), c.get("other", "edge")]
				var z_face: String = faces[0] if FACES.get(faces[0], [0])[0] == 2 else faces[1]
				var y_face: String = faces[1] if z_face == faces[0] else faces[0]
				var zc := s.z if FACES.get(z_face, [2, false])[1] else 0.0
				var yc := s.y if FACES.get(y_face, [1, false])[1] else 0.0
				var zi := zc - w if zc > 0.0 else w
				var yi := yc - w if yc > 0.0 else w
				draw_line(at.call(Vector3(0, yc, zi)), at.call(Vector3(0, yi, zc)), PICKED if i == picked else INK, 1.2)
		for line in _lines:
			var a: Vector2 = at.call(line.origin)
			var b: Vector2 = at.call(line.origin + line.dir * line.length)
			var colour := PICKED if picked_name != "" and line.feature == picked_name else INK
			if line.face.dot(nears[view]) > 0.9:
				draw_line(a, b, colour, 1.2)
			elif line.face.dot(nears[view]) < -0.9:
				draw_dashed_line(a, b, Color(colour, 0.5), 1.0, 5.0)
	# The sizes: its length under the edge view, its width left of the face side, its
	# thickness left of the edge view.
	var face: Vector2 = f.corners[FACE]
	var edge: Vector2 = f.corners[EDGE]
	_size_line(font, edge + Vector2(0, s.z * k + 26), edge + Vector2(s.x * k, s.z * k + 26), _mm(s.x))
	_size_line(font, face - Vector2(22, 0), face + Vector2(-22, s.y * k), _mm(s.y))
	_size_line(font, edge - Vector2(22, 0), edge + Vector2(-22, s.z * k), _mm(s.z))
	# A drag being drawn.
	if not rubber.is_empty():
		var corner: Vector2 = f.corners[rubber.view]
		var a: Vector2 = corner + rubber.a * k
		var b: Vector2 = corner + rubber.b * k
		_dashed_rect(Rect2(a, b - a).abs(), PICKED)


func _dashed_rect(r: Rect2, colour: Color) -> void:
	var p := [r.position, r.position + Vector2(r.size.x, 0), r.end, r.position + Vector2(0, r.size.y)]
	for c in 4:
		draw_dashed_line(p[c], p[(c + 1) % 4], colour, 1.0, 4.0)


func _size_line(font: Font, a: Vector2, b: Vector2, text: String) -> void:
	draw_line(a, b, SIZES, 1.0)
	var across := (b - a).normalized().orthogonal() * 5.0
	draw_line(a - across, a + across, SIZES, 1.0)
	draw_line(b - across, b + across, SIZES, 1.0)
	var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x
	var mid := 0.5 * (a + b)
	var at := mid + Vector2(-0.5 * width, -4.0) if absf(b.y - a.y) < 1.0 else mid + Vector2(-width - 6.0, 4.0)
	draw_string(font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, SIZES)


static func _mm(v: float) -> String:
	return ("%.0f" % v) if absf(v - roundf(v)) < 0.01 else ("%.1f" % v)
