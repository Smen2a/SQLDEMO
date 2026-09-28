extends Control

## The plan book (P, or E on it at the side table): the plans (plans.gd), a plan's parts and
## the joints between them, and a part's sheet, drawn as a woodworker's drawing: its face
## side, face edge and end, each with the lines that lay it out (the far faces' dashed), its
## sizes, and what each feature comes to. "Take this sheet" puts it in hand: the Layout
## slot's plan sheet, laid on the piece in the vise to draw the part on. "New plan" and "Edit"
## (the player's own plans) or "Edit a copy" (the presets) open the pad (plan_editor.gd).

const SheetView := preload("res://workshop/sheet_view.gd")

const INK := Color(0.16, 0.14, 0.13)
const FAINT := Color(0.16, 0.14, 0.13, 0.5)
const PAPER := Color(0.95, 0.92, 0.84)

var workshop
var plan_id := ""
var part_id := ""
var _plans: OptionButton
var _about: Label
var _parts: ItemList
var _joints: Label
var _features: Label
var _sheet: SheetView
var _take: Button
var _edit: Button


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
	var drawing := HBoxContainer.new()
	left.add_child(drawing)
	var new := Button.new()
	new.text = "New plan"
	new.focus_mode = FOCUS_NONE
	new.pressed.connect(func(): workshop._ui.open_editor("", false, false))
	drawing.add_child(new)
	_edit = Button.new()
	_edit.focus_mode = FOCUS_NONE
	_edit.pressed.connect(edit)
	drawing.add_child(_edit)
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
	_sheet = SheetView.new()
	_sheet.size_flags_horizontal = SIZE_EXPAND_FILL
	_sheet.size_flags_vertical = SIZE_EXPAND_FILL
	_sheet.mouse_filter = MOUSE_FILTER_IGNORE
	right.add_child(_sheet)
	_features = _label(right, "", 15)
	_features.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART

	var ids: Array = workshop.plans.plans.keys()
	ids.sort()
	for id in ids:
		var plan: Dictionary = workshop.plans.plans[id]
		_plans.add_item(plan.get("name", id) + ("  (yours)" if plan.get("mine", false) else ""))
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
	_edit.text = "Edit" if plan.get("mine", false) else "Edit a copy"
	_edit.disabled = plan.is_empty()
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
	var plan: Dictionary = workshop.plans.plans.get(plan_id, {})
	var s: Vector3 = workshop.plans.size_of(p) if not p.is_empty() else Vector3.ZERO
	_sheet.title = "" if p.is_empty() else "%s: %s, %s, %s x %s x %s mm" % [plan.get("name", plan_id), p.get("name", p.id),
			p.get("wood", "wood"), _mm(s.x), _mm(s.y), _mm(s.z)]
	_sheet.set_part(p)


## The plan shown, on the pad to change: the player's own as it is, a preset as a copy.
func edit() -> void:
	var plan: Dictionary = workshop.plans.plans.get(plan_id, {})
	if not plan.is_empty():
		workshop._ui.open_editor(plan_id, not plan.get("mine", false), false)


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
