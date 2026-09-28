extends Control

## The plan book (P, or E on it at the side table): the plans (plans.gd), a plan's parts and
## the joints between them, and a part's sheet, drawn as a woodworker's drawing: its face
## side, face edge and end, each with the lines that lay it out (the far faces' dashed), its
## sizes, and what each feature comes to. "Take this sheet" puts it in hand: the Layout
## slot's plan sheet, laid on the piece in the vise to draw the part on.

const INK := Color(0.16, 0.14, 0.13)
const FAINT := Color(0.16, 0.14, 0.13, 0.5)
const PAPER := Color(0.95, 0.92, 0.84)
const SIZES := Color(0.6, 0.22, 0.14) # dimensions
const MARGIN := 44.0 # px round the views (room for their sizes)
const GAP := 36.0    # px between them

var workshop
var plan_id := ""
var part_id := ""
var _plans: OptionButton
var _about: Label
var _parts: ItemList
var _joints: Label
var _features: Label
var _sheet: Control
var _take: Button
var _lines := {} # part id -> its lines (SdfBody.part_lines), for the drawing


func _ready() -> void:
	set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	mouse_filter = MOUSE_FILTER_STOP
	var shade := ColorRect.new()
	shade.color = Color(0, 0, 0, 0.45)
	shade.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	shade.mouse_filter = MOUSE_FILTER_IGNORE
	add_child(shade)
	var paper := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = PAPER
	style.set_corner_radius_all(6)
	style.set_content_margin_all(18)
	paper.add_theme_stylebox_override("panel", style)
	paper.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	paper.offset_left = 40
	paper.offset_top = 30
	paper.offset_right = -40
	paper.offset_bottom = -30
	add_child(paper)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 18)
	paper.add_child(row)

	var left := VBoxContainer.new()
	left.custom_minimum_size.x = 320
	row.add_child(left)
	_label(left, "The plan book", 22)
	_plans = OptionButton.new()
	_plans.focus_mode = FOCUS_NONE
	left.add_child(_plans)
	_about = _label(left, "", 14)
	_about.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_label(left, "Parts", 18)
	_parts = ItemList.new()
	_parts.custom_minimum_size.y = 120
	_parts.focus_mode = FOCUS_NONE
	_parts.add_theme_color_override("font_color", INK)
	_parts.add_theme_color_override("font_selected_color", INK)
	var list_style := StyleBoxFlat.new()
	list_style.bg_color = PAPER.darkened(0.06)
	list_style.set_content_margin_all(6)
	_parts.add_theme_stylebox_override("panel", list_style)
	left.add_child(_parts)
	_label(left, "Joints", 18)
	_joints = _label(left, "", 14)
	_joints.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	var spacer := Control.new()
	spacer.size_flags_vertical = SIZE_EXPAND_FILL
	left.add_child(spacer)
	var buttons := HBoxContainer.new()
	left.add_child(buttons)
	_take = Button.new()
	_take.text = "Take this sheet"
	_take.focus_mode = FOCUS_NONE
	_take.pressed.connect(take_sheet)
	buttons.add_child(_take)
	var close := Button.new()
	close.text = "Close (P)"
	close.focus_mode = FOCUS_NONE
	close.pressed.connect(func(): workshop._ui.close_plans())
	buttons.add_child(close)

	var right := VBoxContainer.new()
	right.size_flags_horizontal = SIZE_EXPAND_FILL
	row.add_child(right)
	_sheet = Control.new()
	_sheet.size_flags_horizontal = SIZE_EXPAND_FILL
	_sheet.size_flags_vertical = SIZE_EXPAND_FILL
	_sheet.draw.connect(_draw_sheet)
	right.add_child(_sheet)
	_features = _label(right, "", 15)
	_features.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

	var ids: Array = workshop.plans.plans.keys()
	ids.sort()
	for id in ids:
		_plans.add_item(workshop.plans.plans[id].get("name", id))
		_plans.set_item_metadata(_plans.item_count - 1, id)
	_plans.item_selected.connect(func(i): show_plan(_plans.get_item_metadata(i)))
	_parts.item_selected.connect(func(i): show_part(_parts.get_item_metadata(i)))
	# The plan (and part) of the sheet in hand, else the first.
	var sheet: Dictionary = workshop.plans.sheet
	if not sheet.is_empty():
		show_plan(sheet.plan, sheet.part)
	elif not ids.is_empty():
		show_plan(ids[0])


## Opens a plan: its parts listed (each with its wood and size, and how many pieces are it),
## its joints, and a part's sheet (`part`, else the first).
func show_plan(id: String, part := "") -> void:
	var plan: Dictionary = workshop.plans.plans.get(id, {})
	plan_id = id
	for i in _plans.item_count:
		if _plans.get_item_metadata(i) == id:
			_plans.select(i)
	_about.text = plan.get("about", "")
	var laid: Dictionary = workshop.plans.status(id)
	_parts.clear()
	for p in plan.get("parts", []):
		var s: Vector3 = workshop.plans.size_of(p)
		_parts.add_item("%s: %s, %s x %s x %s mm   laid out %d / %d" % [p.get("name", p.id), p.get("wood", "wood"),
				_mm(s.x), _mm(s.y), _mm(s.z), laid.get(p.id, 0), p.get("count", 1)])
		_parts.set_item_metadata(_parts.item_count - 1, p.id)
	var joints: Array = []
	for j in plan.get("joints", []):
		joints.append("%s: %s (%s's %s, %s's %s)" % [j.get("name", ""), j.get("kind", ""), j.get("a", ""),
				j.get("a_feature", ""), j.get("b", ""), j.get("b_feature", "")])
	_joints.text = "\n".join(joints)
	var parts: Array = plan.get("parts", [])
	show_part(part if part != "" else (parts[0].id if not parts.is_empty() else ""))


## Shows a part's sheet.
func show_part(id: String) -> void:
	part_id = id
	for i in _parts.item_count:
		if _parts.get_item_metadata(i) == id:
			_parts.select(i)
	var p: Dictionary = workshop.plans.part(plan_id, id)
	_take.disabled = p.is_empty()
	var said: Array = []
	for f in p.get("features", []):
		said.append(_feature_text(f, workshop.plans.size_of(p)))
	_features.text = "\n".join(said)
	_sheet.queue_redraw()


## Puts the part's sheet in hand (the Layout slot's plan sheet) and closes the book.
func take_sheet() -> void:
	if workshop.plans.part(plan_id, part_id).is_empty():
		return
	workshop.plans.take_sheet(plan_id, part_id)
	if workshop.current != "layout":
		workshop.select_tool("layout")
	workshop._ui.close_plans()


## What a feature comes to, in words and millimetres.
static func _feature_text(f: Dictionary, size: Vector3) -> String:
	var name: String = f.get("name", f.get("kind", ""))
	match f.get("kind", ""):
		"hole":
			var along: Array = f.get("along", [0, size.x])
			var across: Array = f.get("across", [0, 0])
			var edge := "the face edge" if f.get("face", "side") in ["side", "back"] else "the face side"
			return "%s: %s x %s mm, %s, %s mm from the end, %s mm from %s" % [name, _mm(along[1] - along[0]),
					_mm(across[1] - across[0]), "through" if f.get("through", false) else "%s mm deep" % _mm(f.get("depth", 0)),
					_mm(along[0]), _mm(across[0]), edge]
		"tenon":
			var y: Array = f.get("y", [0, size.y])
			var z: Array = f.get("z", [0, size.z])
			return "%s: %s mm long, %s x %s mm; shoulders %s and %s mm in from the edges, %s and %s mm from the faces" % [
					name, _mm(f.get("length", 0)), _mm(y[1] - y[0]), _mm(z[1] - z[0]), _mm(y[0]), _mm(size.y - y[1]),
					_mm(z[0]), _mm(size.z - z[1])]
		"kerf":
			return "%s: sawn %s mm deep from the end, %s mm from the %s" % [name, _mm(f.get("depth", 0)), _mm(f.get("at", 0)),
					"face edge" if f.get("axis", "y") == "y" else "face side"]
		"rebate":
			return "%s: %s mm wide, %s mm deep" % [name, _mm(f.get("width", 0)), _mm(f.get("depth", 0))]
		"chamfer":
			return "%s: %s mm" % [name, _mm(f.get("width", 0))]
		"taper":
			return "%s: from %s mm thick at the end to %s mm at the other" % [name, _mm(f.get("from", 0)), _mm(f.get("to", 0))]
	return name


static func _mm(v: float) -> String:
	return ("%.0f" % v) if absf(v - roundf(v)) < 0.01 else ("%.1f" % v)


func _label(parent: Control, text: String, size: int) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", INK)
	l.add_theme_font_size_override("font_size", size)
	parent.add_child(l)
	return l


## The sheet: the part's face side (length across, width down), its face edge below it
## (length, thickness) and its end beside it (thickness, width), in third-angle projection;
## each face's lines, the far face's dashed; the sizes.
func _draw_sheet() -> void:
	var p: Dictionary = workshop.plans.part(plan_id, part_id)
	if p.is_empty():
		return
	var font := get_theme_default_font()
	var plan: Dictionary = workshop.plans.plans.get(plan_id, {})
	var s: Vector3 = workshop.plans.size_of(p)
	_sheet.draw_string(font, Vector2(0, 20), "%s: %s, %s, %s x %s x %s mm" % [plan.get("name", plan_id), p.get("name", p.id),
			p.get("wood", "wood"), _mm(s.x), _mm(s.y), _mm(s.z)], HORIZONTAL_ALIGNMENT_LEFT, -1, 20, INK)
	var area := _sheet.size - Vector2(2.0 * MARGIN + GAP, 2.0 * MARGIN + GAP + 30.0)
	if area.x <= 0.0 or area.y <= 0.0:
		return
	var k := minf(area.x / (s.x + s.z), area.y / (s.y + s.z)) # px a mm
	var face := Vector2(MARGIN, MARGIN + 30.0) # the face side view's corner
	var edge := face + Vector2(0.0, s.y * k + GAP)
	var end := face + Vector2(s.x * k + GAP, 0.0)
	# Each view: where a part-space point lands, and which faces it shows (near, far).
	var views := [
		[face, func(q: Vector3): return face + Vector2(q.x, q.y) * k, Vector3(0, 0, -1), Vector2(s.x, s.y), "face side"],
		[edge, func(q: Vector3): return edge + Vector2(q.x, q.z) * k, Vector3(0, -1, 0), Vector2(s.x, s.z), "face edge"],
		[end, func(q: Vector3): return end + Vector2(q.z, q.y) * k, Vector3(-1, 0, 0), Vector2(s.z, s.y), "end"],
	]
	if not _lines.has(part_id):
		_lines[part_id] = workshop.tools["layout"].part_lines(p, s).lines
	var hidden := _removed(p)
	for view in views:
		var corner: Vector2 = view[0]
		var at: Callable = view[1]
		var near: Vector3 = view[2]
		var extent: Vector2 = view[3] * k
		_sheet.draw_rect(Rect2(corner, extent), INK, false, 1.5)
		_sheet.draw_string(font, corner + Vector2(0, extent.y + 16), view[4], HORIZONTAL_ALIGNMENT_LEFT, -1, 13, FAINT)
		# What the features take out, seen through the part (hidden: dashed), then the lines
		# on this face (the far face's dashed).
		for box in hidden:
			var lo: Vector2 = at.call(box.position.clamp(Vector3.ZERO, s))
			var hi: Vector2 = at.call(box.end.clamp(Vector3.ZERO, s))
			var r := Rect2(lo, hi - lo).abs()
			for c in 4:
				var a := r.position + Vector2(r.size.x if c == 1 or c == 2 else 0.0, r.size.y if c >= 2 else 0.0)
				var b := r.position + Vector2(r.size.x if c == 0 or c == 1 else 0.0, r.size.y if c == 1 or c == 2 else 0.0)
				_sheet.draw_dashed_line(a, b, FAINT, 1.0, 4.0)
		for line in _lines[part_id]:
			var a: Vector2 = at.call(line.origin)
			var b: Vector2 = at.call(line.origin + line.dir * line.length)
			if line.face.dot(near) > 0.9:
				_sheet.draw_line(a, b, INK, 1.2)
			elif line.face.dot(near) < -0.9:
				_sheet.draw_dashed_line(a, b, FAINT, 1.0, 5.0)
	# The sizes: its length under the edge view, its width left of the face side, its
	# thickness left of the edge view.
	_size_line(font, edge + Vector2(0, s.z * k + 26), edge + Vector2(s.x * k, s.z * k + 26), _mm(s.x))
	_size_line(font, face - Vector2(22, 0), face + Vector2(-22, s.y * k), _mm(s.y))
	_size_line(font, edge - Vector2(22, 0), edge + Vector2(-22, s.z * k), _mm(s.z))


## What a part's features take out of its blank, as boxes (part space): a hole's, a tenon's
## four cheeks', a kerf's, a rebate's. (A chamfer's and a taper's show in their lines.)
static func _removed(p: Dictionary) -> Array:
	var s: Vector3 = Vector3(p.size[0], p.size[1], p.size[2])
	var boxes := []
	for f in p.get("features", []):
		var along: Array = f.get("along", [0, s.x])
		match f.get("kind", ""):
			"hole":
				var across: Array = f.get("across", [0, 0])
				var face: String = f.get("face", "side")
				var through: bool = f.get("through", false)
				var depth: float = f.get("depth", 0.0)
				if face in ["side", "back"]:
					var z0 := 0.0 if face == "side" or through else s.z - depth
					var z1 := s.z if face == "back" or through else depth
					boxes.append(AABB(Vector3(along[0], across[0], z0), Vector3(along[1] - along[0], across[1] - across[0], z1 - z0)))
				elif face in ["edge", "other_edge"]:
					var y0 := 0.0 if face == "edge" or through else s.y - depth
					var y1 := s.y if face == "other_edge" or through else depth
					boxes.append(AABB(Vector3(along[0], y0, across[0]), Vector3(along[1] - along[0], y1 - y0, across[1] - across[0])))
			"tenon":
				var l: float = f.get("length", 0.0)
				var x0 := s.x - l if f.get("end", "end") == "far_end" else 0.0
				var y: Array = f.get("y", [0, s.y])
				var z: Array = f.get("z", [0, s.z])
				for b in [AABB(Vector3(x0, 0, 0), Vector3(l, y[0], s.z)), AABB(Vector3(x0, y[1], 0), Vector3(l, s.y - y[1], s.z)),
						AABB(Vector3(x0, 0, 0), Vector3(l, s.y, z[0])), AABB(Vector3(x0, 0, z[1]), Vector3(l, s.y, s.z - z[1]))]:
					if b.size.x > 0.0 and b.size.y > 0.0 and b.size.z > 0.0:
						boxes.append(b)
			"kerf":
				var d: float = f.get("depth", 0.0)
				var x0 := s.x - d if f.get("end", "end") == "far_end" else 0.0
				var half: float = 0.5 * f.get("width", 0.8)
				var at: float = f.get("at", 0.0)
				if f.get("axis", "y") == "z":
					boxes.append(AABB(Vector3(x0, 0, at - half), Vector3(d, s.y, 2.0 * half)))
				else:
					boxes.append(AABB(Vector3(x0, at - half, 0), Vector3(d, 2.0 * half, s.z)))
	return boxes


func _size_line(font: Font, a: Vector2, b: Vector2, text: String) -> void:
	_sheet.draw_line(a, b, SIZES, 1.0)
	var across := (b - a).normalized().orthogonal() * 5.0
	_sheet.draw_line(a - across, a + across, SIZES, 1.0)
	_sheet.draw_line(b - across, b + across, SIZES, 1.0)
	var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13).x
	var mid := 0.5 * (a + b)
	var at := mid + Vector2(-0.5 * width, -4.0) if absf(b.y - a.y) < 1.0 else mid + Vector2(-width - 6.0, 4.0)
	_sheet.draw_string(font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, SIZES)
