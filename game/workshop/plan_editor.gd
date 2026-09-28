extends Control

## The pad: a plan drawn on paper (E on the pad on the side table; "New plan", "Edit" or "Edit
## a copy" in the plan book). A plan is its parts, each a rectangular blank (a wood; its
## length along the grain, its width and thickness) with features drawn on its sheet, and the
## joints between them. Saved, it is one of the player's plans (plans.gd: user://plans), in the
## plan book with the presets and laid out on the wood as they are.
##
## Features are drawn with the mouse on the sheet's views (sheet_view.gd), a tool at a time:
##   hole     a box on the face side or the face edge (through; or given a depth)
##   tenon    its section as a box on the end; or a box from an end on the face side (its
##            length and width) or the face edge (its length and thickness)
##   kerf     a line across the end; or from an end along the face side or edge (its depth)
##   rebate   a box in a corner of the end; or along an edge of the face side or edge
##   chamfer  a drag from a corner of the end, as wide as it goes; or along an edge of the
##            face side or edge
##   taper    a line along the face edge, from end to end: the thickness it leaves at each
## What it comes to shows as the drag goes. "Pick" chooses a feature (a click on it) to set
## its sizes exactly, rename it or take it off (Delete). What is on the pad stays there when it
## is put down (P, Esc), until it is saved or thrown away.

const SheetView := preload("res://workshop/sheet_view.gd")
const PlanViewer := preload("res://workshop/plan_viewer.gd")
const Room := preload("res://workshop/room.gd")

const INK := Color(0.16, 0.14, 0.13)
const PAPER := Color(0.95, 0.92, 0.84)
const WARN := Color(0.62, 0.16, 0.1)
const TOOLS := ["pick", "hole", "tenon", "kerf", "rebate", "chamfer", "taper"]
const HINTS := {
	"pick": "Click a feature to set its sizes.",
	"hole": "Drag a box on the face side or the face edge: a hole (a mortise), through.",
	"tenon": "Drag its section on the end, or a box from an end on the face side or edge.",
	"kerf": "Drag across the end, or from an end along the face side or edge: a saw kerf.",
	"rebate": "Drag a box in a corner of the end, or along an edge of the face side or edge.",
	"chamfer": "Drag from a corner of the end as wide as it is, or along an edge of a face.",
	"taper": "Drag a line along the face edge, end to end: the thickness left at each end.",
}
## Each kind's sizes, as set in the feature's panel: [key, index in its pair (-1: a number),
## label, the choices (for a choice)].
const FIELDS := {
	"hole": [["face", -1, "in the", ["side", "back", "edge", "other_edge"]], ["along", 0, "from the end"], ["along", 1, "to"],
			["across", 0, "across, from"], ["across", 1, "to"], ["through", -1, "through"], ["depth", -1, "deep"]],
	"tenon": [["end", -1, "at the", ["end", "far_end"]], ["length", -1, "long"], ["y", 0, "across the width, from"],
			["y", 1, "to"], ["z", 0, "the thickness, from"], ["z", 1, "to"]],
	"kerf": [["end", -1, "from the", ["end", "far_end"]], ["depth", -1, "deep"], ["axis", -1, "square to the", ["y", "z"]],
			["at", -1, "at"], ["width", -1, "wide"]],
	"rebate": [["face", -1, "in the", ["side", "back", "edge", "other_edge"]],
			["other", -1, "along the", ["side", "back", "edge", "other_edge"]], ["width", -1, "wide"], ["depth", -1, "deep"],
			["along", 0, "from the end"], ["along", 1, "to"]],
	"chamfer": [["face", -1, "on the", ["side", "back", "edge", "other_edge"]],
			["other", -1, "and the", ["side", "back", "edge", "other_edge"]], ["width", -1, "wide"],
			["along", 0, "from the end"], ["along", 1, "to"]],
	"taper": [["face", -1, "off the", ["side", "back"]], ["from", -1, "leaving at the end"], ["to", -1, "at the far end"]],
}
const WORDS := {"side": "face side", "back": "back", "edge": "face edge", "other_edge": "other edge", "end": "end",
		"far_end": "far end", "y": "width", "z": "thickness"}
const JOINT_KINDS := ["mortise and tenon", "through mortise and tenon", "wedge", "rebate", "housing", "glued"]
const ON := 0.25 # mm: a drag this near the blank's outline starts or ends on it

var workshop
## What to open (set before it is added): a plan to change (`open_id`), as a copy
## (`open_copy`); else what was left on the pad (`open_resume`); else a new plan.
var open_id := ""
var open_copy := false
var open_resume := false
## The plan on the pad, the player's plan it is saved as ("" not yet), and whether it is as
## saved.
var plan := {}
var plan_id := ""
var saved := false
var part_index := 0
var picked := -1 # the chosen feature of the part shown
var tool := "hole"

var _press := {} # where a drag on the sheet began: {"view", "at"}
var _filling := false
var _thrown := false
var _deleting := false
var _name: LineEdit
var _about: TextEdit
var _title: Label
var _parts: ItemList
var _part_name: LineEdit
var _wood: OptionButton
var _count: SpinBox
var _size: Array[SpinBox] = []
var _stock: Label
var _joints: ItemList
var _pick_a: OptionButton
var _pick_a_feature: OptionButton
var _pick_b: OptionButton
var _pick_b_feature: OptionButton
var _pick_kind: OptionButton
var _tools := {} # tool -> its button
var _hint: Label
var _sheet: SheetView
var _props: HFlowContainer
var _status: Label
var _save: Button
var _take: Button
var _delete: Button


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
	style.set_content_margin_all(14)
	paper.add_theme_stylebox_override("panel", style)
	paper.set_anchors_and_offsets_preset(PRESET_FULL_RECT)
	paper.offset_left = 24
	paper.offset_top = 18
	paper.offset_right = -24
	paper.offset_bottom = -18
	add_child(paper)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 16)
	paper.add_child(row)

	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size.x = 350
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	row.add_child(scroll)
	var left := VBoxContainer.new()
	left.size_flags_horizontal = SIZE_EXPAND_FILL
	left.size_flags_vertical = SIZE_EXPAND_FILL
	scroll.add_child(left)
	_title = _label(left, "The pad", 20)
	_name = _line(left, "the plan's name", func(t): _set_plan("name", t))
	_about = TextEdit.new()
	_about.placeholder_text = "what it is"
	_about.custom_minimum_size.y = 46
	_about.wrap_mode = TextEdit.LINE_WRAPPING_BOUNDARY
	_about.text_changed.connect(func(): _set_plan("about", _about.text))
	left.add_child(_about)

	_label(left, "Parts", 16)
	_parts = ItemList.new()
	_parts.custom_minimum_size.y = 74
	_parts.focus_mode = FOCUS_NONE
	_parts.add_theme_color_override("font_color", INK)
	_parts.add_theme_color_override("font_selected_color", INK)
	var list_style := StyleBoxFlat.new()
	list_style.bg_color = PAPER.darkened(0.06)
	list_style.set_content_margin_all(4)
	_parts.add_theme_stylebox_override("panel", list_style)
	_parts.item_selected.connect(show_part)
	left.add_child(_parts)
	var part_buttons := HBoxContainer.new()
	left.add_child(part_buttons)
	_button(part_buttons, "Add a part", add_part)
	_button(part_buttons, "Take the part off", remove_part)
	var grid := GridContainer.new()
	grid.columns = 2
	left.add_child(grid)
	_label(grid, "Name", 14)
	_part_name = _line(grid, "the part's name", rename_part)
	_part_name.size_flags_horizontal = SIZE_EXPAND_FILL
	_label(grid, "Wood", 14)
	_wood = OptionButton.new()
	_wood.focus_mode = FOCUS_NONE
	for wood in Room.WOOD_COLOUR:
		_wood.add_item(wood)
	_wood.item_selected.connect(func(i): _set_part("wood", _wood.get_item_text(i)))
	grid.add_child(_wood)
	_label(grid, "How many", 14)
	_count = _spin(grid, func(v): _set_part("count", int(v)), 1.0, 99.0, "")
	for i in 3:
		_label(grid, ["Length (along the grain)", "Width", "Thickness"][i], 14)
		_size.append(_spin(grid, _set_blank.bind(i), 1.0, 1000.0))
	_stock = _label(left, "", 13)
	_stock.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

	_label(left, "Joints", 16)
	_joints = ItemList.new()
	_joints.custom_minimum_size.y = 48
	_joints.focus_mode = FOCUS_NONE
	_joints.add_theme_color_override("font_color", INK)
	_joints.add_theme_color_override("font_selected_color", INK)
	_joints.add_theme_stylebox_override("panel", list_style)
	left.add_child(_joints)
	var joint_a := HBoxContainer.new()
	left.add_child(joint_a)
	_pick_a = _option(joint_a, func(_i): _fill_joint_features())
	_pick_a_feature = _option(joint_a, Callable())
	var joint_b := HBoxContainer.new()
	left.add_child(joint_b)
	_pick_b = _option(joint_b, func(_i): _fill_joint_features())
	_pick_b_feature = _option(joint_b, Callable())
	var joint_c := HBoxContainer.new()
	left.add_child(joint_c)
	_pick_kind = _option(joint_c, Callable())
	for kind in JOINT_KINDS:
		_pick_kind.add_item(kind)
	_button(joint_c, "Join them", add_joint)
	_button(joint_c, "Undo a joint", remove_joint)

	_status = _label(left, "", 13)
	_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	var spacer := Control.new()
	spacer.size_flags_vertical = SIZE_EXPAND_FILL
	left.add_child(spacer)
	var buttons := HBoxContainer.new()
	left.add_child(buttons)
	_save = _button(buttons, "Save", save)
	_take = _button(buttons, "Take this sheet", take_sheet)
	var more := HBoxContainer.new()
	left.add_child(more)
	_button(more, "Put down (P)", func(): workshop._ui.close_plans())
	_button(more, "Throw away", throw_away)
	_delete = _button(more, "Delete", delete)

	var right := VBoxContainer.new()
	right.size_flags_horizontal = SIZE_EXPAND_FILL
	row.add_child(right)
	var tools := HBoxContainer.new()
	right.add_child(tools)
	var group := ButtonGroup.new()
	for t in TOOLS:
		var b := Button.new()
		b.text = t.capitalize()
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = FOCUS_NONE
		b.pressed.connect(set_tool.bind(t))
		tools.add_child(b)
		_tools[t] = b
	_hint = _label(right, "", 14)
	_sheet = SheetView.new()
	_sheet.size_flags_horizontal = SIZE_EXPAND_FILL
	_sheet.size_flags_vertical = SIZE_EXPAND_FILL
	_sheet.gui_input.connect(_on_sheet_input)
	_sheet.mouse_exited.connect(func(): _hint.text = HINTS[tool])
	right.add_child(_sheet)
	_props = HFlowContainer.new()
	_props.custom_minimum_size.y = 64
	right.add_child(_props)

	_start()
	set_tool(tool)


func _start() -> void:
	var plans = workshop.plans
	if open_id != "" and plans.plans.has(open_id):
		plan = plans.plans[open_id].duplicate(true)
		plan.erase("mine")
		if open_copy or not plans.plans[open_id].get("mine", false):
			plan.name = "%s (copy)" % plan.get("name", open_id)
			plan_id = ""
			saved = false
		else:
			plan_id = open_id
			saved = true
	elif open_resume and not plans.draft.is_empty():
		plan = plans.draft.plan
		plan_id = plans.draft.id
		saved = plans.draft.saved
	else:
		plan = {"name": "New plan", "about": "", "parts": [], "joints": []}
		plan.parts.append(_new_part())
		plan_id = ""
		saved = false
	if not plan.get("joints") is Array:
		plan.joints = []
	part_index = 0
	picked = -1
	_fill()


## Put down (the book or the pad closed): what is on it stays there, unless thrown away.
func put_down() -> void:
	workshop.plans.draft = {} if _thrown else {"plan": plan, "id": plan_id, "saved": saved}


# --- the plan and its parts ----------------------------------------------------------------

func part() -> Dictionary:
	var parts: Array = plan.get("parts", [])
	return parts[part_index] if part_index >= 0 and part_index < parts.size() else {}


func feature() -> Dictionary:
	var features: Array = part().get("features", [])
	return features[picked] if picked >= 0 and picked < features.size() else {}


func _new_part() -> Dictionary:
	var n: int = plan.get("parts", []).size() + 1
	return {"id": _part_id("part %d" % n, -1), "name": "Part %d" % n, "count": 1, "wood": "oak", "size": [120.0, 40.0, 20.0],
			"features": []}


## A part's id from its name, as no other part of the plan has it.
func _part_id(name: String, index: int) -> String:
	var base := ""
	for c in name.to_lower():
		base += c if (c >= "a" and c <= "z") or (c >= "0" and c <= "9") else "_"
	base = base.strip_edges().trim_prefix("_").trim_suffix("_")
	if base == "":
		base = "part"
	var id := base
	var n := 2
	var parts: Array = plan.get("parts", [])
	while range(parts.size()).any(func(i): return i != index and parts[i].id == id):
		id = "%s_%d" % [base, n]
		n += 1
	return id


func add_part() -> void:
	plan.parts.append(_new_part())
	_changed()
	_fill_parts()
	show_part(plan.parts.size() - 1)


func remove_part() -> void:
	var p := part()
	if p.is_empty() or plan.parts.size() <= 1:
		_say("A plan needs a part.")
		return
	plan.joints = plan.joints.filter(func(j): return j.get("a", "") != p.id and j.get("b", "") != p.id)
	plan.parts.remove_at(part_index)
	_changed()
	_fill_parts()
	_fill_joints()
	show_part(mini(part_index, plan.parts.size() - 1))


## Shows a part's sheet, its fields filled in.
func show_part(index: int) -> void:
	part_index = index
	picked = -1
	if index < _parts.item_count:
		_parts.select(index)
	var p := part()
	_filling = true
	_part_name.text = p.get("name", "")
	for i in _wood.item_count:
		if _wood.get_item_text(i) == p.get("wood", ""):
			_wood.select(i)
	_count.value = p.get("count", 1)
	var s := SheetView.size_of(p)
	for i in 3:
		_size[i].value = s[i]
	_filling = false
	_show()
	_fill_feature()
	_say_stock()


## A part renamed: its id follows its name (the joints with it).
func rename_part(text: String) -> void:
	var p := part()
	if _filling or p.is_empty():
		return
	var old: String = p.id
	p.name = text
	p.id = _part_id(text, part_index)
	for j in plan.joints:
		for side in ["a", "b"]:
			if j.get(side, "") == old:
				j[side] = p.id
	_changed()
	_parts.set_item_text(part_index, _part_text(p))
	_fill_joints()


func _set_plan(key: String, value) -> void:
	if _filling:
		return
	plan[key] = value
	_changed()


func _set_part(key: String, value) -> void:
	var p := part()
	if _filling or p.is_empty():
		return
	p[key] = value
	_changed()
	_parts.set_item_text(part_index, _part_text(p))
	_say_stock()


func _set_blank(value: float, axis: int) -> void:
	var p := part()
	if _filling or p.is_empty():
		return
	var s: Array = p.get("size", [0.0, 0.0, 0.0]).duplicate()
	s[axis] = value
	p.size = s
	_changed()
	_parts.set_item_text(part_index, _part_text(p))
	_say_stock()


func _say_stock() -> void:
	var p := part()
	var kind: String = workshop.plans.stock_for(p)
	if kind == "":
		_stock.text = "No %s on the rack is big enough for it." % p.get("wood", "wood")
		_stock.add_theme_color_override("font_color", WARN)
	else:
		var spec: Dictionary = Room.STOCK[kind]
		_stock.text = "Cut from %s %s (%s x %s x %s mm)." % [spec.wood, spec.kind, _mm(spec.size.x), _mm(spec.size.y),
				_mm(spec.size.z)]
		_stock.add_theme_color_override("font_color", INK)


# --- drawing features on the sheet -----------------------------------------------------------

func set_tool(t: String) -> void:
	tool = t
	_tools[t].button_pressed = true
	_hint.text = HINTS[t]
	_press = {}
	_sheet.rubber = {}
	_show()


func _on_sheet_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and (event as InputEventMouseButton).button_index == MOUSE_BUTTON_LEFT:
		var button := event as InputEventMouseButton
		if button.pressed:
			get_viewport().gui_release_focus()
			var at := _sheet.view_at(button.position)
			if tool == "pick" or at.is_empty():
				pick(at)
				return
			_press = at
		elif not _press.is_empty():
			var b := _sheet.point_on(_press.view, button.position)
			var f := feature_from(tool, _press.view, _press.at, b)
			_press = {}
			_sheet.rubber = {}
			if f.is_empty():
				_say(HINTS[tool])
				_show()
			else:
				add_feature(f)
		_sheet.accept_event()
	elif event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if not _press.is_empty():
			var b := _sheet.point_on(_press.view, motion.position)
			_sheet.rubber = {"view": _press.view, "a": _press.at, "b": b}
			_show(feature_from(tool, _press.view, _press.at, b))
			_hint.text = _where(_press.view, b, _press.at)
		else:
			var at := _sheet.view_at(motion.position)
			_hint.text = HINTS[tool] if at.is_empty() else _where(at.view, at.at, null)


## Where the pointer is on a view, in words (and how far from where the drag began).
func _where(view: int, at: Vector2, from) -> String:
	var names := [["from the end", "from the face edge"], ["from the end", "from the face side"],
			["from the face side", "from the face edge"]]
	var said := "%s mm %s, %s mm %s, on the %s" % [_mm(at.x), names[view][0], _mm(at.y), names[view][1],
			SheetView.VIEW_NAMES[view]]
	if from != null:
		var d: Vector2 = (at - from).abs()
		said += "   (%s x %s mm)" % [_mm(d.x), _mm(d.y)]
	return said


## The feature a drag draws with a tool, from `a` to `b` on a view (view mm): {} for none.
func feature_from(t: String, view: int, a: Vector2, b: Vector2) -> Dictionary:
	var s := SheetView.size_of(part())
	var lo := a.min(b)
	var hi := a.max(b)
	var d := hi - lo
	var across_size: float = s.y if view == SheetView.FACE else s.z # the face's other size (face side, face edge)
	var faces: Array = ["edge", "other_edge"] if view == SheetView.FACE else ["side", "back"]
	match t:
		"hole":
			if view == SheetView.END or d.x < 1.0 or d.y < 1.0:
				return {}
			return {"kind": "hole", "face": "side" if view == SheetView.FACE else "edge", "along": [lo.x, hi.x],
					"across": [lo.y, hi.y], "through": true}
		"tenon":
			if d.x < 1.0 or d.y < 1.0:
				return {}
			if view == SheetView.END:
				return {"kind": "tenon", "end": "end", "length": _default_depth(s), "y": [lo.y, hi.y], "z": [lo.x, hi.x]}
			var end := _end_of(lo.x, hi.x, s.x)
			if end == "":
				return {}
			var length := hi.x if end == "end" else s.x - lo.x
			var other_size: float = s.z if view == SheetView.FACE else s.y
			var middle := [snappedf(other_size / 3.0, 0.5), snappedf(2.0 * other_size / 3.0, 0.5)]
			if view == SheetView.FACE:
				return {"kind": "tenon", "end": end, "length": length, "y": [lo.y, hi.y], "z": middle}
			return {"kind": "tenon", "end": end, "length": length, "y": middle, "z": [lo.y, hi.y]}
		"kerf":
			if view == SheetView.END:
				if (b - a).length() < 1.0:
					return {}
				var along_z := absf(b.x - a.x) >= absf(b.y - a.y) # across the width: square to y
				var at := snappedf(0.5 * (a.y + b.y) if along_z else 0.5 * (a.x + b.x), 0.5)
				return {"kind": "kerf", "end": "end", "depth": _default_depth(s), "axis": "y" if along_z else "z", "at": at,
						"width": 0.8}
			var end := _end_of(lo.x, hi.x, s.x)
			if end == "" or d.x < 1.0:
				return {}
			return {"kind": "kerf", "end": end, "depth": hi.x if end == "end" else s.x - lo.x,
					"axis": "y" if view == SheetView.FACE else "z", "at": snappedf(0.5 * (a.y + b.y), 0.5), "width": 0.8}
		"rebate":
			if d.x < 0.5 or d.y < 0.5:
				return {}
			if view == SheetView.END:
				var face := _side_of(lo.x, hi.x, s.z, "side", "back")
				var other := _side_of(lo.y, hi.y, s.y, "edge", "other_edge")
				if face == "" or other == "":
					return {}
				return {"kind": "rebate", "face": face, "other": other, "width": d.y, "depth": d.x}
			var other := _side_of(lo.y, hi.y, across_size, faces[0], faces[1])
			if other == "":
				return {}
			var deep: float = s.z if view == SheetView.FACE else s.y
			var f := {"kind": "rebate", "face": "side" if view == SheetView.FACE else "edge", "other": other, "width": d.y,
					"depth": snappedf(deep / 3.0, 0.5)}
			if lo.x > ON or hi.x < s.x - ON:
				f.along = [lo.x, hi.x]
			return f
		"chamfer":
			if view == SheetView.END:
				var face := "side" if a.x <= 0.5 * s.z else "back"
				var other := "edge" if a.y <= 0.5 * s.y else "other_edge"
				var corner := Vector2(0.0 if face == "side" else s.z, 0.0 if other == "edge" else s.y)
				var w := snappedf(maxf(absf(b.x - corner.x), absf(b.y - corner.y)), 0.5)
				if w < 0.5:
					return {}
				return {"kind": "chamfer", "face": face, "other": other, "width": minf(w, minf(s.y, s.z))}
			if d.x < 1.0:
				return {}
			var f := {"kind": "chamfer", "face": "side" if view == SheetView.FACE else "edge",
					"other": faces[0] if 0.5 * (a.y + b.y) <= 0.5 * across_size else faces[1], "width": 2.0}
			if lo.x > ON or hi.x < s.x - ON:
				f.along = [lo.x, hi.x]
			return f
		"taper":
			if view != SheetView.EDGE or absf(b.x - a.x) < 0.25 * s.x:
				return {}
			var z0 := a.y + (b.y - a.y) * (0.0 - a.x) / (b.x - a.x)
			var z1 := a.y + (b.y - a.y) * (s.x - a.x) / (b.x - a.x)
			if 0.5 * (z0 + z1) >= 0.5 * s.z: # nearer the back: off the back, leaving z
				return {"kind": "taper", "face": "back", "from": _thickness(z0, s.z), "to": _thickness(z1, s.z)}
			return {"kind": "taper", "face": "side", "from": _thickness(s.z - z0, s.z), "to": _thickness(s.z - z1, s.z)}
	return {}


static func _thickness(t: float, most: float) -> float:
	return clampf(snappedf(t, 0.5), 0.5, most)


## A tenon's length or a kerf's depth when drawn on the end: a quarter of the part, at most 40.
static func _default_depth(s: Vector3) -> float:
	return minf(snappedf(0.25 * s.x, 0.5), 40.0)


## Which end a range along the length starts from: "end" (0), "far_end" (the length), "".
static func _end_of(lo: float, hi: float, length: float) -> String:
	if lo <= ON:
		return "end"
	if hi >= length - ON:
		return "far_end"
	return ""


## Which side a range across starts from: `near` (0), `far` (the size), "".
static func _side_of(lo: float, hi: float, extent: float, near: String, far: String) -> String:
	if lo <= ON:
		return near
	if hi >= extent - ON:
		return far
	return ""


## Adds a feature to the part shown, named after its kind, and picks it.
func add_feature(f: Dictionary) -> void:
	var p := part()
	var named := {"kind": f.kind, "name": _feature_name(f)}
	named.merge(f)
	p.features.append(named)
	picked = p.features.size() - 1
	_changed()
	_show()
	_fill_feature()
	_say("Drawn: " + PlanViewer._feature_text(named, SheetView.size_of(p)))


func _feature_name(f: Dictionary) -> String:
	var base: String = "mortise" if f.kind == "hole" else f.kind
	var names: Array = part().get("features", []).map(func(g): return g.get("name", ""))
	var name := base
	var n := 2
	while name in names:
		name = "%s %d" % [base, n]
		n += 1
	return name


## Picks the feature under a point of the sheet ({"view", "at"}); nothing off it.
func pick(at: Dictionary) -> void:
	picked = -1 if at.is_empty() else _sheet.feature_at(at.view, at.at)
	_show()
	_fill_feature()


func remove_feature() -> void:
	var p := part()
	var f := feature()
	if f.is_empty():
		return
	plan.joints = plan.joints.filter(func(j): return not ((j.get("a", "") == p.id and j.get("a_feature", "") == f.name) or \
			(j.get("b", "") == p.id and j.get("b_feature", "") == f.name)))
	p.features.remove_at(picked)
	picked = -1
	_changed()
	_show()
	_fill_feature()
	_fill_joints()


## The sheet: the part as drawn, and a feature being drawn (`ghost`) as it would come out.
func _show(ghost := {}) -> void:
	var p := part()
	if ghost.is_empty():
		_sheet.picked = picked
		_sheet.set_part(p)
		return
	var q: Dictionary = p.duplicate(true)
	q.features.append(ghost)
	_sheet.picked = q.features.size() - 1
	_sheet.set_part(q)


## The picked feature's panel: its name and sizes, to set exactly; "Take it off".
func _fill_feature() -> void:
	for c in _props.get_children():
		c.hide()
		c.queue_free()
	var f := feature()
	if f.is_empty():
		var l := _label(_props, "Pick a feature (Pick, then click it) to set its sizes." if tool == "pick" else
				"Pick a feature to set its sizes.", 13)
		l.modulate.a = 0.7
		return
	_filling = true
	var name := _line(_props, "its name", _rename_feature)
	name.text = f.get("name", "")
	name.custom_minimum_size.x = 120
	var s := SheetView.size_of(part())
	for spec in FIELDS.get(f.kind, []):
		var key: String = spec[0]
		var index: int = spec[1]
		var box := HBoxContainer.new()
		_props.add_child(box)
		_label(box, spec[2], 13)
		if spec.size() > 3:
			var choice := _option(box, Callable())
			for option in spec[3]:
				choice.add_item(WORDS.get(option, option))
				if f.get(key, spec[3][0]) == option:
					choice.select(choice.item_count - 1)
			choice.item_selected.connect(func(i): _set_field(spec[3][i], key, -1))
		elif key == "through":
			var check := CheckBox.new()
			check.focus_mode = FOCUS_NONE
			check.button_pressed = f.get("through", false)
			check.toggled.connect(func(on): _set_field(on, key, -1))
			box.add_child(check)
		else:
			var value: float = f.get(key, 0.0) if index < 0 else _pair(f, key, s)[index]
			var spin := _spin(box, _set_field.bind(key, index), 0.0, 1000.0)
			spin.value = value
	var off := Button.new()
	off.text = "Take it off"
	off.focus_mode = FOCUS_NONE
	off.pressed.connect(remove_feature)
	_props.add_child(off)
	_filling = false


## A feature's range (along, across, y, z), or what it stands for when it has none.
static func _pair(f: Dictionary, key: String, s: Vector3) -> Array:
	if f.has(key) and f[key] is Array and f[key].size() == 2 and (key != "along" or f[key][1] > f[key][0]):
		return f[key]
	match key:
		"y":
			return [0.0, s.y]
		"z":
			return [0.0, s.z]
		"along":
			return [0.0, s.x]
	return [0.0, 0.0]


func _set_field(value, key: String, index: int) -> void:
	var f := feature()
	if _filling or f.is_empty():
		return
	if index < 0:
		f[key] = value
	else:
		var pair: Array = _pair(f, key, SheetView.size_of(part())).duplicate()
		pair[index] = value
		f[key] = pair
	if key == "through" and not value and f.get("depth", 0.0) <= 0.0:
		var s := SheetView.size_of(part())
		f.depth = snappedf(0.5 * (s.z if f.get("face", "side") in ["side", "back"] else s.y), 0.5)
		_fill_feature.call_deferred()
	_changed()
	_show()


func _rename_feature(text: String) -> void:
	var p := part()
	var f := feature()
	if _filling or f.is_empty():
		return
	for j in plan.joints:
		for side in ["a", "b"]:
			if j.get(side, "") == p.id and j.get(side + "_feature", "") == f.name:
				j[side + "_feature"] = text
	f.name = text
	_changed()
	_show()
	_fill_joints()


# --- joints ------------------------------------------------------------------------------------

func add_joint() -> void:
	var parts: Array = plan.parts
	var a: int = _pick_a.selected
	var b: int = _pick_b.selected
	if a < 0 or b < 0 or a == b or _pick_a_feature.selected < 0 or _pick_b_feature.selected < 0:
		_say("A joint is between two parts, a feature of each.")
		return
	var pa: Dictionary = parts[a]
	var pb: Dictionary = parts[b]
	plan.joints.append({"name": "the %s in the %s" % [pb.name.to_lower(), pa.name.to_lower()],
			"kind": _pick_kind.get_item_text(maxi(_pick_kind.selected, 0)), "a": pa.id,
			"a_feature": _pick_a_feature.get_item_text(_pick_a_feature.selected), "b": pb.id,
			"b_feature": _pick_b_feature.get_item_text(_pick_b_feature.selected)})
	_changed()
	_fill_joints()


func remove_joint() -> void:
	var i := _joints.get_selected_items()
	var at: int = i[0] if not i.is_empty() else plan.joints.size() - 1
	if at < 0:
		return
	plan.joints.remove_at(at)
	_changed()
	_fill_joints()


func _fill_joints() -> void:
	_joints.clear()
	for j in plan.joints:
		_joints.add_item("%s: %s (%s's %s, %s's %s)" % [j.get("name", ""), j.get("kind", ""), j.get("a", ""),
				j.get("a_feature", ""), j.get("b", ""), j.get("b_feature", "")])
	for pick in [_pick_a, _pick_b]:
		var was: int = pick.selected
		pick.clear()
		for p in plan.parts:
			pick.add_item(p.get("name", p.id))
		if pick.item_count > 0:
			pick.select(clampi(was, 0, pick.item_count - 1) if was >= 0 else (0 if pick == _pick_a else mini(1, pick.item_count - 1)))
	_fill_joint_features()


func _fill_joint_features() -> void:
	for pair in [[_pick_a, _pick_a_feature], [_pick_b, _pick_b_feature]]:
		var features: OptionButton = pair[1]
		features.clear()
		var i: int = pair[0].selected
		if i >= 0 and i < plan.parts.size():
			for f in plan.parts[i].get("features", []):
				features.add_item(f.get("name", f.kind))


# --- the whole plan ----------------------------------------------------------------------------

func _fill() -> void:
	_filling = true
	_name.text = plan.get("name", "")
	_about.text = plan.get("about", "")
	_filling = false
	_fill_parts()
	_fill_joints()
	show_part(0)
	_changed(false)


func _fill_parts() -> void:
	_parts.clear()
	for p in plan.parts:
		_parts.add_item(_part_text(p))
	if part_index < _parts.item_count:
		_parts.select(part_index)


func _part_text(p: Dictionary) -> String:
	var s := SheetView.size_of(p)
	return "%s: %s, %s x %s x %s mm%s" % [p.get("name", p.id), p.get("wood", "wood"), _mm(s.x), _mm(s.y), _mm(s.z),
			"" if p.get("count", 1) == 1 else ", %d of them" % p.count]


## After any change: not as saved (unless `unsaved` is false), the title and buttons.
func _changed(unsaved := true) -> void:
	if unsaved:
		saved = false
		_deleting = false
	_title.text = "The pad: %s%s" % [plan.get("name", ""), "" if saved else "  (not saved)"]
	var mine: bool = plan_id != "" and workshop.plans.plans.get(plan_id, {}).get("mine", false)
	_delete.disabled = not mine
	_delete.text = "Delete: sure?" if _deleting else "Delete"


## Saves the plan (one of the player's, user://plans): false, and why, where it can't be.
func save() -> bool:
	var name := String(plan.get("name", "")).strip_edges()
	if name == "":
		_say("Name the plan first.")
		return false
	for p in plan.parts:
		var s := SheetView.size_of(p)
		if s.x <= 0.0 or s.y <= 0.0 or s.z <= 0.0:
			_say("%s has no size." % p.get("name", "A part"))
			return false
		for f in p.get("features", []):
			_keep_in(f, s)
	if plan_id == "":
		plan_id = workshop.plans.new_id(name)
	var failed: String = workshop.plans.save_plan(plan_id, plan)
	if failed != "":
		_say("Not saved: " + failed)
		return false
	saved = true
	_changed(false)
	_show()
	_fill_feature()
	_say("Saved: \"%s\", in the plan book (%s/%s.json)." % [name, workshop.plans.USER_DIR, plan_id])
	return true


## The part shown's sheet in hand (saved first), the pad put down.
func take_sheet() -> void:
	if not saved and not save():
		return
	workshop.plans.take_sheet(plan_id, part().id)
	if workshop.current != "layout":
		workshop.select_tool("layout")
	workshop._ui.close_plans()


func throw_away() -> void:
	_thrown = true
	workshop._ui.close_plans()


## Deletes the saved plan (a second press: sure), and throws the pad's away.
func delete() -> void:
	if plan_id == "" or not workshop.plans.plans.get(plan_id, {}).get("mine", false):
		return
	if not _deleting:
		_deleting = true
		_changed(false)
		return
	workshop.plans.delete_plan(plan_id)
	throw_away()


## A feature kept inside its blank.
static func _keep_in(f: Dictionary, s: Vector3) -> void:
	var across: float = s.y if f.get("face", "side") in ["side", "back"] else s.z
	var sizes := {"along": s.x, "across": across, "y": s.y, "z": s.z}
	for key in sizes:
		if f.get(key) is Array and f[key].size() == 2:
			var lo := clampf(minf(f[key][0], f[key][1]), 0.0, sizes[key])
			var hi := clampf(maxf(f[key][0], f[key][1]), 0.0, sizes[key])
			f[key] = [lo, hi]
	for key in ["length", "from", "to", "width", "depth", "at"]:
		if f.has(key):
			f[key] = maxf(f[key], 0.0)
	if f.has("length"):
		f.length = minf(f.length, s.x)
	if f.get("kind", "") == "taper":
		f.from = minf(f.from, s.z)
		f.to = minf(f.to, s.z)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key != null and key.pressed and not key.echo and key.keycode == KEY_DELETE and picked >= 0:
		remove_feature()
		get_viewport().set_input_as_handled()


# --- widgets -------------------------------------------------------------------------------------

func _say(text: String) -> void:
	_status.text = text


static func _mm(v: float) -> String:
	return ("%.0f" % v) if absf(v - roundf(v)) < 0.01 else ("%.1f" % v)


func _label(parent: Control, text: String, size: int) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_color_override("font_color", INK)
	l.add_theme_font_size_override("font_size", size)
	parent.add_child(l)
	return l


func _line(parent: Control, placeholder: String, changed: Callable) -> LineEdit:
	var line := LineEdit.new()
	line.placeholder_text = placeholder
	line.select_all_on_focus = true
	line.text_changed.connect(changed)
	line.text_submitted.connect(func(_t): line.release_focus())
	parent.add_child(line)
	return line


func _spin(parent: Control, changed: Callable, least: float, most: float, suffix := "mm") -> SpinBox:
	var spin := SpinBox.new()
	spin.min_value = least
	spin.max_value = most
	spin.step = 0.5 if suffix == "mm" else 1.0
	spin.suffix = suffix
	spin.select_all_on_focus = true
	spin.custom_minimum_size.x = 92
	spin.value_changed.connect(changed)
	parent.add_child(spin)
	return spin


func _option(parent: Control, selected: Callable) -> OptionButton:
	var option := OptionButton.new()
	option.focus_mode = FOCUS_NONE
	option.fit_to_longest_item = false
	option.custom_minimum_size.x = 100
	option.size_flags_horizontal = SIZE_EXPAND_FILL
	if selected.is_valid():
		option.item_selected.connect(selected)
	parent.add_child(option)
	return option


func _button(parent: Control, text: String, pressed: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = FOCUS_NONE
	b.pressed.connect(pressed)
	parent.add_child(b)
	return b
