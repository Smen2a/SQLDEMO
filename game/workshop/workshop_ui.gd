extends CanvasLayer

## The workshop's controls: the hotbar (the tools, 1 to 9; 0 empty hands), a crosshair and
## what E would do while walking; at the bench, the tool in hand's settings, the wood, undo /
## redo / reset, and a status line (edits, how long the last edit took to apply and upload,
## GPU frame time).

const PlanViewer := preload("res://workshop/plan_viewer.gd")
const PlanEditor := preload("res://workshop/plan_editor.gd")
const TOOL_LABELS := {"chisel": "1 Chisel", "gouge": "2 Gouge", "saw": "3 Saw", "rasp": "4 Rasp",
		"spokeshave": "5 Planes", "scraper": "6 Scraper", "sanding_block": "7 Block", "sanding_sponge": "8 Sponge",
		"layout": "9 Layout"}
## What stands in a planned cut's way (SdfBody.plan_stroke's warnings), in words.
const WARNINGS := {
	"skates": "skates on its bevel: tip it past %d°",
	"shallow": "only as deep as a hand can push it",
	"tears out": "tears out: against the grain",
	"corners buried": "corners buried: its sides tear",
	"breaks out": "breaks out where it leaves the edge",
	"not struck": "never struck: pushed by hand, it barely goes in",
	"slit only": "only a slit: chop near an open face, or pare towards it",
	"pops off": "the chip pops off",
	"digs in": "dives steeply: digs in",
	"splits": "along the grain: the wedge splits it",
	"blocked": "blocked: a %.1f mm step ahead: chop it, or come from the other side",
	"blade meets the work": "its blade meets the work behind the edge: come at it another way",
	"too wide for the gap": "too wide for the gap: take a narrower one",
	"stalls": "stalls: the surface rises into a chip too thick to push: take it in lighter passes",
	"mouth": "its mouth passes no thicker a shaving",
	"at the line": "held to the marked lines: no deeper, no further",
	"lifts out": "lifts out: the handle lowered under its bevel",
}
## What stops a stroke going on (SdfBody.get_stroke_state's "limit"), in words.
const LIMITS := {
	"back": "its back meets the work: it goes no deeper",
	"through": "through",
	"at the line": "held to the marked lines",
}
## A workshop's pace against real life (workshop.pace).
const PACES := [0.25, 0.5, 1.0, 2.0, 4.0, 8.0]
## Sanding blocks' grits, coarse to fine.
const GRITS := [60, 80, 120, 180, 240, 320]
const SLOT := Vector2(92, 40) # a hotbar slot's size (pixels)
const GAUGE_SIZE := Vector2(240, 104) # the attitude gauge's (pixels)
const DIAL := 25.0 # its dials' radius
## How a chisel's or gouge's bevel meets the work (workshop.attitude()): words and colour.
const BITES := {"rides": ["rides its bevel", Color(0.55, 0.8, 1.0)], "bites": ["bites", Color(0.5, 1.0, 0.5)],
		"digs in": ["digs in", Color(1.0, 0.5, 0.3)], "chops": ["chops", Color(1.0, 0.85, 0.35)]}
const HINTS := "Left-drag on the board: use the tool (a chisel, gouge or plane follows a curving drag)   C: chop   Tab: variant   Q / E: skew or turn by 15°   P: plans   K: check a part\n" + \
		"Right-drag, the guiding hand (also mid-stroke): up / down the angle, left / right skew or turn, wheel the lean (Ctrl: finely)\n" + \
		"Hold Space first: plan it and see it (wheel: how hard, Ctrl+wheel: finely), then left-drag: make it   Alt: no edge lock, not held to the lines\n" + \
		"Esc: drop it, or step back from the bench   Ctrl+Z / Ctrl+Shift+Z: undo, redo   Middle-drag: orbit   Shift+middle-drag: pan   Wheel: zoom"
const WALK_HINTS := "WASD: walk   Shift: hurry   Mouse: look   1-9, wheel: tools   0: empty hands   Esc: free the mouse\n" + \
		"E: pick up, let go (over the vise: into it), take stock from the rack, open the plan book, work at the bench   F: out of the vise   R: turn what you carry   P: plans"

var workshop

var _slots := {}    # tool (or "" for empty hands) -> its hotbar slot (PanelContainer)
var _settings_boxes := {}
var _sliders := {}  # "tool/key" -> [slider, its value label, format]: what the wheel also sets
var _variants := {} # family -> its variant OptionButton
var _blows := {}    # family -> its blow OptionButton (a chop's strength)
var _undo: Button
var _redo: Button
var _status: Label
var _hints: Label
var _plan: Label  # beside the pointer: what the stroke being planned or made comes to
var _gauge: Control # by the pointer while the guiding hand has the tool: its attitude
var _left: Control   # the tool's settings (at the bench)
var _right: Control  # the wood, undo, pace... (at the bench)
var _crosshair: Label
var _prompt: Label   # under the crosshair: what E would do
var _viewer: Control # the plan book or the pad, while it is open (plan_viewer.gd, plan_editor.gd)
var _part_box: VBoxContainer # the part in the vise (if it is one): checking it against its drawing
var _part_title: Label
var _check_button: Button
var _check_text: Label


func _ready() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)

	# The tool in hand's settings, top left.
	var left := _panel(root, Vector2(12, 12))
	_left = left.get_parent()
	var edge_boxes := {}
	for family in ["chisel", "gouge"]:
		var box := VBoxContainer.new()
		left.add_child(box)
		var labels: Array = []
		for v in workshop.variants.get(family, []):
			labels.append(v.label)
		_variants[family] = _choice(box, "Kind", labels, 0, func(i):
			workshop.set_setting(family, "variant", workshop.variants[family][i].id))
		_slider(box, family, "depth", "Depth", "%.2f mm")
		_slider(box, family, "angle", "Angle", "%.0f°")
		_blows[family] = _choice(box, "Blow", ["Tap", "Firm", "Heavy"], 1, func(i):
			workshop.set_setting(family, "blow", workshop.BLOWS[i]))
		edge_boxes[family] = box
	var chisel: VBoxContainer = edge_boxes.chisel
	var rasp := VBoxContainer.new()
	left.add_child(rasp)
	var rasp_labels: Array = []
	for v in workshop.variants.get("rasp", []):
		rasp_labels.append(v.label)
	_variants["rasp"] = _choice(rasp, "Kind", rasp_labels, 1, func(i):
		workshop.set_setting("rasp", "variant", workshop.variants.rasp[i].id))
	_slider(rasp, "rasp", "pressure", "Pressure", "%.2f")
	_slider(rasp, "rasp", "tilt", "Tilt", "%.0f°")
	var shave := VBoxContainer.new() # the Planes slot: a spokeshave, block planes, a shoulder plane
	left.add_child(shave)
	var planes: Array = []
	for v in workshop.variants.get("spokeshave", []):
		planes.append(v.label)
	_variants["spokeshave"] = _choice(shave, "Kind", planes, 0, func(i):
		workshop.set_setting("spokeshave", "variant", workshop.variants.spokeshave[i].id))
	_slider(shave, "spokeshave", "depth", "Depth", "%.2f mm")
	var scraper := VBoxContainer.new()
	left.add_child(scraper)
	_slider(scraper, "scraper", "pressure", "Pressure", "%.2f")
	var saw := VBoxContainer.new()
	left.add_child(saw)
	_slider(saw, "saw", "pressure", "Pressure", "%.2f")
	var block := VBoxContainer.new()
	left.add_child(block)
	var pads: Array = []
	for v in workshop.variants.get("sanding_block", []):
		pads.append(v.label)
	_variants["sanding_block"] = _choice(block, "Kind", pads, 0, func(i):
		workshop.set_setting("sanding_block", "variant", workshop.variants.sanding_block[i].id))
	_choice(block, "Grit", GRITS.map(func(g): return str(g)), GRITS.find(workshop.settings.sanding_block.grit),
			func(i): workshop.set_setting("sanding_block", "grit", GRITS[i]))
	_slider(block, "sanding_block", "pressure", "Pressure", "%.2f")
	var sponge := VBoxContainer.new()
	left.add_child(sponge)
	_choice(sponge, "Grit", ["60", "120", "220"], 1,
			func(i): workshop.set_setting("sanding_sponge", "grit", [60, 120, 220][i]))
	_slider(sponge, "sanding_sponge", "pressure", "Pressure", "%.2f")
	# Layout: the marking gauge (how far in it is set) or the knife and square.
	var layout := VBoxContainer.new()
	left.add_child(layout)
	_variants["layout"] = _choice(layout, "Kind", workshop.variants.layout.map(func(v): return v.label), 0,
			func(i): workshop.set_setting("layout", "variant", workshop.variants.layout[i].id))
	_slider(layout, "layout", "distance", "Gauge", "%.1f mm")
	# A plan's sheet: drawn on in pencil, or scribed on at once.
	var scribe := CheckBox.new()
	scribe.text = "Scribe a sheet as you lay it on"
	scribe.focus_mode = Control.FOCUS_NONE
	scribe.button_pressed = workshop.settings.layout.scribe
	scribe.toggled.connect(func(on): workshop.set_setting("layout", "scribe", on))
	layout.add_child(scribe)
	_button(layout, "Plan book (P)", func(): open_plans())
	_settings_boxes = {"chisel": chisel, "gouge": edge_boxes.gouge, "saw": saw, "rasp": rasp, "spokeshave": shave,
			"scraper": scraper, "sanding_block": block, "sanding_sponge": sponge, "layout": layout}

	# The work in the vise, top right (new boards come from the rack).
	var right := _panel(root, Vector2.ZERO)
	_right = right.get_parent()
	var row := HBoxContainer.new()
	right.add_child(row)
	_undo = _button(row, "Undo", func(): workshop.undo())
	_redo = _button(row, "Redo", func(): workshop.redo())
	_button(row, "Sweep", func(): workshop.sweep())
	# How fast the work goes: 1x as a real hand would.
	_choice(right, "Pace", ["¼×", "½×", "1×", "2×", "4×", "8×"], PACES.find(workshop.pace),
			func(i): workshop.pace = PACES[i])
	# A chisel's or gouge's stroke locks onto an edge it is aimed along (Alt: freely).
	_value_slider(right, "Edge lock", 0.0, 45.0, 1.0, workshop.edge_aim, "aimed within %.0f°",
			func(v): workshop.edge_aim = v)
	_value_slider(right, "", 5.0, 90.0, 1.0, workshop.edge_fold, "edges over %.0f°", func(v): workshop.edge_fold = v)
	var shadows := CheckBox.new()
	shadows.text = "Board casts shadows"
	shadows.focus_mode = Control.FOCUS_NONE
	shadows.toggled.connect(func(on): workshop.board.live_shadows = on)
	right.add_child(shadows)
	# On: the shader draws a stroke while the tool moves, and it is applied once on release.
	# Off: every move is applied to the board as it happens (slower; for comparison).
	var preview := CheckBox.new()
	preview.text = "Preview strokes on the GPU"
	preview.button_pressed = workshop.board.stroke_preview
	preview.focus_mode = Control.FOCUS_NONE
	preview.toggled.connect(func(on): workshop.board.stroke_preview = on)
	right.add_child(preview)
	# The part in the vise, if it is one: checked against its drawing (K).
	_part_box = VBoxContainer.new()
	right.add_child(_part_box)
	_part_box.add_child(HSeparator.new())
	_part_title = Label.new()
	_part_box.add_child(_part_title)
	_check_button = _button(_part_box, "Check against the drawing (K)", func(): workshop.checking.toggle())
	_check_text = Label.new()
	_check_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_check_text.custom_minimum_size.x = 360
	_part_box.add_child(_check_text)
	_pin(right, Control.PRESET_TOP_RIGHT)

	# Status and hints, bottom left, over the hotbar.
	var bottom := _panel(root, Vector2.ZERO)
	_status = Label.new()
	bottom.add_child(_status)
	_hints = Label.new()
	_hints.text = HINTS
	_hints.modulate = Color(1, 1, 1, 0.7)
	bottom.add_child(_hints)
	_pin(bottom, Control.PRESET_BOTTOM_LEFT)
	(bottom.get_parent() as Control).offset_bottom = -(12 + SLOT.y + 8) # (over the hotbar)

	# The hotbar, bottom middle: 1 to 9 the tools, 0 empty hands.
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 4)
	root.add_child(bar)
	var order: Array = TOOL_LABELS.keys()
	order.append("")
	for tool in order:
		var slot := PanelContainer.new()
		slot.mouse_filter = Control.MOUSE_FILTER_STOP
		slot.custom_minimum_size = SLOT
		var label := Label.new()
		label.text = TOOL_LABELS[tool] if tool != "" else "0 Hands"
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		if tool != "":
			label.add_theme_color_override("font_color", workshop.TOOL_COLOURS[tool].lightened(0.3))
		slot.add_child(label)
		slot.gui_input.connect(func(e):
			if e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
				workshop.select_tool(tool))
		bar.add_child(slot)
		_slots[tool] = slot
	bar.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM, Control.PRESET_MODE_MINSIZE, 12)
	bar.grow_horizontal = Control.GROW_DIRECTION_BOTH
	bar.grow_vertical = Control.GROW_DIRECTION_BEGIN

	# Walking: a crosshair, and what E would do under it.
	_crosshair = Label.new()
	_crosshair.text = "+"
	_crosshair.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_crosshair.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
	root.add_child(_crosshair)
	_prompt = Label.new()
	_prompt.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_prompt.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
	_prompt.add_theme_constant_override("shadow_offset_x", 1)
	_prompt.add_theme_constant_override("shadow_offset_y", 1)
	root.add_child(_prompt)

	_plan = Label.new()
	_plan.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_plan.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
	_plan.add_theme_constant_override("shadow_offset_x", 1)
	_plan.add_theme_constant_override("shadow_offset_y", 1)
	root.add_child(_plan)
	_gauge = Control.new()
	_gauge.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_gauge.size = GAUGE_SIZE
	_gauge.draw.connect(_draw_gauge)
	root.add_child(_gauge)
	refresh()


## The plan book (plan_viewer.gd): opened over everything, the mouse freed while walking.
func open_plans() -> void:
	if plans_open():
		return
	_viewer = PlanViewer.new()
	_viewer.workshop = workshop
	add_child(_viewer)
	if workshop.mode == workshop.Mode.WALK:
		workshop.player.active = false
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


## The pad (plan_editor.gd), over everything as the book is: a plan of the player's to change
## (`id`), a copy of one (`copy`), what was left on the pad (`resume`, else a new plan).
func open_editor(id := "", copy := false, resume := false) -> void:
	if plans_open():
		_viewer.queue_free() # (from the book's own button: not freed under it)
	_viewer = PlanEditor.new()
	_viewer.workshop = workshop
	_viewer.open_id = id
	_viewer.open_copy = copy
	_viewer.open_resume = resume
	add_child(_viewer)
	if workshop.mode == workshop.Mode.WALK:
		workshop.player.active = false
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE


func editor_open() -> bool:
	return plans_open() and _viewer is PlanEditor


## Closes the book or the pad (what is drawn on it stays there).
func close_plans() -> void:
	if not plans_open():
		return
	if _viewer.has_method("put_down"):
		_viewer.put_down()
	_viewer.queue_free()
	_viewer = null
	if workshop.mode == workshop.Mode.WALK:
		workshop.player.active = true
		if DisplayServer.get_name() != "headless":
			Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	refresh()


func plans_open() -> bool:
	return _viewer != null and is_instance_valid(_viewer)


## One line on the stroke being planned or made: the tool, what the wheel has set, how far
## it goes and what it comes to.
func plan_text() -> String:
	if workshop.current == "layout":
		return workshop.layout.describe()
	var plan: Dictionary = workshop.get_plan()
	if plan.is_empty():
		return stroke_text()
	var tool: String = workshop.current
	var s: Dictionary = workshop.settings[tool]
	var named: Dictionary = workshop.variant()
	var parts: Array[String] = [named.label if not named.is_empty() else TOOL_LABELS[tool].substr(2)]
	if tool == "chisel" or tool == "gouge" or tool == "spokeshave":
		return _edge_text(plan, s, parts)
	var intensity = workshop.INTENSITY.get(tool)
	if intensity != null:
		parts.append(intensity[4] % s[intensity[0]])
	var tilt = workshop.TILT.get(tool)
	if tilt != null:
		parts.append(tilt[4] % s[tilt[0]])
	match tool:
		"saw":
			parts.append("%.0f mm strokes, %.2f mm deeper each" % [plan.length, plan.get("depth", 0.0)])
		"rasp", "scraper":
			parts.append("%.0f mm strokes, %.3f mm off each" % [plan.length, plan.get("depth", 0.0)])
		"sanding_block":
			parts.append("a %.0f mm rub takes %s, on %.0f%% of its face" % [plan.length, _thin(plan.get("depth", 0.0)),
					100.0 * plan.get("contact", 1.0)])
		"sanding_sponge":
			parts.append("rounds over what it rubs")
		_:
			parts.append("%.0f mm" % plan.length)
	return "   ".join(parts)


## While a direct stroke of a tool that is not planned is made: what it has come to.
func stroke_text() -> String:
	var state: Dictionary = workshop.stroke_state()
	var tool: String = workshop.current
	if state.is_empty() or tool in workshop.PUSHED:
		return ""
	var named: Dictionary = workshop.variant()
	var parts: Array[String] = [named.label if not named.is_empty() else TOOL_LABELS[tool].substr(2)]
	var lines: Array[String] = []
	match tool:
		"sanding_block", "rasp", "scraper":
			parts.append("%s off" % _thin(state.depth))
			var contact: float = state.get("contact", 1.0)
			parts.append("on %.0f%% of its face" % (100.0 * contact))
			if contact < 0.5:
				lines.append("resting on the high spots" if contact > 0.0 else "off the work")
			if tool != "sanding_block":
				lines.append("cuts on the push")
		"saw":
			parts.append("%.2f mm deep" % state.depth)
			lines.append("cuts on the push")
	lines.push_front("   ".join(parts))
	var limit: String = state.get("limit", "")
	if limit != "":
		lines.append("! " + LIMITS.get(limit, limit))
	return "\n".join(lines)


## A depth, in thousandths of a millimetre below a hundredth.
func _thin(depth: float) -> String:
	return "%.1f µm" % (depth * 1000.0) if depth < 0.01 else "%.3f mm" % depth


## The hotbar, which settings show (at the bench), whether undo and redo apply.
func refresh() -> void:
	if workshop == null or _undo == null:
		return
	var working: bool = workshop.mode == workshop.Mode.WORK
	for tool in _slots:
		_slots[tool].self_modulate = Color(1.6, 1.4, 0.8) if tool == workshop.current else Color(1, 1, 1, 0.75)
	for tool in _settings_boxes:
		_settings_boxes[tool].visible = tool == workshop.current
	_left.visible = working and workshop.current != ""
	_right.visible = working
	_hints.text = HINTS if working else WALK_HINTS
	_crosshair.visible = not working
	_prompt.visible = not working
	for family in _variants:
		var list: Array = workshop.variants.get(family, [])
		for i in list.size():
			if list[i].id == workshop.settings[family].variant:
				_variants[family].select(i)
	for family in _blows:
		_blows[family].select(maxi(workshop.BLOWS.find(workshop.settings[family].get("blow", 1.0)), 0))
	# The wheel sets these too, while a stroke is planned.
	for id in _sliders:
		var parts: PackedStringArray = id.split("/")
		var value: float = workshop.settings[parts[0]][parts[1]]
		_sliders[id][0].set_value_no_signal(value)
		_sliders[id][1].text = _sliders[id][2] % value
	var stats: Dictionary = workshop.board.get_stats() if workshop.board != null else {}
	_undo.disabled = not stats.get("can_undo", false)
	_redo.disabled = not stats.get("can_redo", false)
	_refresh_part()


## The part in the vise: its name, and its check (checking.gd) in words.
func _refresh_part() -> void:
	var piece: RigidBody3D = workshop.clamped
	var tag: Dictionary = piece.get_meta("part", {}) if piece != null else {}
	var checking = workshop.checking
	var said: Array[String] = checking.lines() if checking != null else []
	_part_box.visible = not tag.is_empty() or not said.is_empty()
	if not tag.is_empty():
		var plan: Dictionary = workshop.plans.plans.get(tag.plan, {})
		var part: Dictionary = workshop.plans.part(tag.plan, tag.part)
		var s: Vector3 = workshop.plans.size_of(part)
		_part_title.text = "%s: %s (%s, %s x %s x %s mm)" % [plan.get("name", tag.plan), part.get("name", tag.part),
				part.get("wood", "wood"), _mm(s.x), _mm(s.y), _mm(s.z)]
	else:
		_part_title.text = "Not a part"
	var showing: bool = checking != null and checking.result.has("spots") and not checking.stale
	_check_button.text = "Clear the check (K)" if showing else "Check against the drawing (K)"
	_check_button.disabled = tag.is_empty()
	_check_text.text = "\n".join(said)


static func _mm(v: float) -> String:
	return ("%.0f" % v) if absf(v - roundf(v)) < 0.05 else ("%.1f" % v)


func update_status() -> void:
	var view: Vector2 = workshop.get_viewport().get_visible_rect().size
	if workshop.mode != workshop.Mode.WORK:
		_plan.text = ""
		_crosshair.position = 0.5 * view - 0.5 * _crosshair.get_combined_minimum_size()
		_prompt.text = workshop.prompt()
		_prompt.position = 0.5 * view + Vector2(-0.5 * _prompt.get_combined_minimum_size().x, 18)
		_update_line()
		return
	# Beside the pointer: the plan, or how to make one.
	var line := plan_text()
	if workshop.is_planning():
		line += "\nclick: strike it" if workshop.is_chopping() else "\nleft-drag: make it"
	elif line != "" or workshop.is_engaged():
		pass
	elif workshop.current != "":
		line = "left-drag: use it   right-drag: pivot it   hold Space: plan it first"
	_plan.text = line
	# Below and right of the pointer; left of it where the line would run off the view.
	var pointer: Vector2 = workshop.pointer_at()
	var at: Vector2 = pointer + Vector2(18, 14)
	var size := _plan.get_combined_minimum_size()
	if at.x + size.x > view.x - 8.0:
		at.x = maxf(8.0, pointer.x - 18.0 - size.x)
	at.y = clampf(at.y, 8.0, maxf(8.0, view.y - 8.0 - size.y))
	_plan.position = at
	# The attitude gauge above and left of the pointer (right of it by the view's left edge).
	_gauge.visible = workshop.attitude_shown() and workshop.current != "" and workshop.current != "layout"
	if _gauge.visible:
		var g := pointer + Vector2(-GAUGE_SIZE.x - 14.0, -GAUGE_SIZE.y - 10.0)
		if g.x < 8.0:
			g.x = pointer.x + 18.0
		g.y = maxf(g.y, 8.0)
		_gauge.position = g
		_gauge.queue_redraw()
	_update_line()


## The guiding hand's hold on the tool, in words: a chisel's or gouge's angle and how its
## bevel meets the work, its skew; another's turn; the lean (a rasp's tilt).
func attitude_text() -> String:
	return "   ".join(_attitude_parts())


func _attitude_parts() -> Array[String]:
	var a: Dictionary = workshop.attitude()
	var parts: Array[String] = []
	if a.has("angle"):
		parts.append("%.1f° to the work, %s" % [a.angle, BITES[a.bite][0]])
		parts.append("skew %.0f°" % a.skew)
	else:
		parts.append("turned %.0f°" % wrapf(a.turn, -180.0, 180.0))
	if a.tool in workshop.LEANS or a.tool == "rasp":
		parts.append(("tilt %.1f°" if a.tool == "rasp" else "lean %.1f°") % a.lean)
	return parts


## The attitude gauge: the tool seen side on at its angle to the work (a chisel's or
## gouge's: its bevel's colour says how it meets the work), from above (its skew, or its
## turn), end on (its lean); and in words.
func _draw_gauge() -> void:
	var a: Dictionary = workshop.attitude()
	var font := ThemeDB.fallback_font
	var dim := Color(1, 1, 1, 0.45)
	var ink := Color(1, 1, 1, 0.9)
	var tool_colour: Color = workshop.TOOL_COLOURS.get(a.tool, Color.WHITE).lightened(0.2)
	_gauge.draw_rect(Rect2(Vector2.ZERO, GAUGE_SIZE), Color(0.05, 0.05, 0.06, 0.72))
	var centres := [Vector2(40, 34), Vector2(120, 34), Vector2(200, 34)]
	# Side on: the work's face, and the tool rising from its edge at its angle.
	var c: Vector2 = centres[0] + Vector2(DIAL * 0.7, DIAL * 0.6)
	_gauge.draw_line(c - Vector2(DIAL * 1.4, 0), c + Vector2(DIAL * 0.3, 0), dim, 2.0)
	if a.has("angle"):
		var up := Vector2(-cos(deg_to_rad(a.angle)), -sin(deg_to_rad(a.angle)))
		var bite: Color = BITES[a.bite][1]
		_gauge.draw_line(c, c + up * DIAL * 1.5, tool_colour, 3.0)
		# The bevel: its angle, under the blade (where it would ride flat).
		var bevel := Vector2(-cos(deg_to_rad(a.bevel)), -sin(deg_to_rad(a.bevel)))
		_gauge.draw_line(c, c + bevel * DIAL * 0.9, bite, 2.0)
	else:
		_gauge.draw_line(c + Vector2(-DIAL * 1.2, -3), c + Vector2(0, -3), tool_colour, 3.0)
	# From above: the way it goes (up), and its edge across (a chisel's or gouge's, skewed) or
	# the tool turned.
	c = centres[1]
	_gauge.draw_arc(c, DIAL, 0.0, TAU, 32, dim, 1.0)
	if a.has("skew"):
		_gauge.draw_line(c + Vector2(0, DIAL * 0.8), c - Vector2(0, DIAL * 0.8), dim, 1.0)
		var edge := Vector2.RIGHT.rotated(deg_to_rad(a.skew)) * DIAL * 0.8
		_gauge.draw_line(c - edge, c + edge, tool_colour, 3.0)
	else:
		var way := Vector2.UP.rotated(-deg_to_rad(a.turn)) * DIAL * 0.8
		_gauge.draw_line(c - way, c + way, tool_colour, 3.0)
		_gauge.draw_circle(c + way, 3.0, tool_colour)
	# End on: the work's face, and the tool's leaned on it.
	c = centres[2] + Vector2(0, DIAL * 0.6)
	_gauge.draw_line(c - Vector2(DIAL, 0), c + Vector2(DIAL, 0), dim, 2.0)
	var across := Vector2.RIGHT.rotated(-deg_to_rad(a.lean)) * DIAL * 0.8
	var normal := Vector2.UP.rotated(-deg_to_rad(a.lean)) * DIAL * 0.9
	_gauge.draw_line(c - across + normal * 0.1, c + across + normal * 0.1, tool_colour, 3.0)
	_gauge.draw_line(c + normal * 0.1, c + normal, tool_colour, 1.0)
	# In words: the angle (a chisel's or gouge's), then the rest.
	var parts := _attitude_parts()
	var lines := [parts[0], "   ".join(parts.slice(1))] if a.has("angle") else ["   ".join(parts), ""]
	for i in 2:
		_gauge.draw_string(font, Vector2(8, GAUGE_SIZE.y - 24 + 16 * i), lines[i], HORIZONTAL_ALIGNMENT_LEFT,
				GAUGE_SIZE.x - 16, 12, ink)


## The status line: the board's edits and how long they took, the frame's GPU time.
func _update_line() -> void:
	var stats: Dictionary = workshop.board.get_stats() if workshop.board != null else {}
	var gpu := RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
	var state := ""
	if stats.get("overlay_edits", 0) > 0:
		state = "   (previewing %d edits)" % stats.overlay_edits
	elif workshop.board != null and workshop.board.is_busy():
		state = "   (refining...)" if stats.get("refine_pending", false) else "   (applying...)"
	_status.text = "%d edits in %d strokes   last edit applied in %.0f ms (bricks on the %s), uploaded in %.0f ms%s   GPU %.1f ms   %d fps" % [
			stats.get("edits", 0), stats.get("steps", 0), stats.get("update_ms", 0.0),
			str(stats.get("sampler", "cpu")).to_upper(), stats.get("upload_ms", 0.0), state, gpu,
			Engine.get_frames_per_second()]


## A chisel's, gouge's or spokeshave's plan: what the wood lets it do.
func _edge_text(plan: Dictionary, s: Dictionary, parts: Array[String]) -> String:
	var depth: float = plan.get("depth", 0.0)
	if plan.get("chop", false):
		var named: int = maxi(workshop.BLOWS.find(s.get("blow", 1.0)), 0)
		parts.append("chopping: a %s blow %.1f mm, the slit %.1f mm deep" % [workshop.BLOW_NAMES[named],
				plan.get("blow", 0.0), depth])
	else:
		parts.append("%.2f mm deep (asked %.2f)" % [depth, s.depth])
		if plan.get("corner", false):
			parts.append("across the corner")
		elif plan.get("snapped", false):
			parts.append("along the edge" + (", level with the cut's floor (%.2f mm)" % plan.level if plan.has("level") else ""))
		if s.has("angle"):
			parts.append("%.0f° to the work" % s.angle)
		if absf(s.get("skew", 0.0)) > 0.5:
			parts.append("skewed %.0f°" % s.skew)
		if not plan.get("direct", false): # (a direct stroke goes as far as it is dragged)
			parts.append("%.0f mm" % plan.get("length", 0.0))
		parts.append("%.0f of %.0f N" % [plan.get("force", 0.0), plan.get("available", 0.0)])
	var grain: float = plan.get("grain", 0.0)
	var along := "along the grain" if grain < 0.15 else ("across the grain" if grain < 0.6 else "severing the fibres")
	var slope: int = plan.get("slope", 0)
	if slope > 0:
		along += ", downhill"
	elif slope < 0:
		along += ", uphill"
	var lines: Array[String] = ["   ".join(parts), along]
	# Where a rule stops it short, first: where, and why.
	var stop: String = plan.get("stop", "")
	var stop_at: float = plan.get("stop_at", -1.0)
	for w in plan.get("warnings", PackedStringArray()):
		var text: String = WARNINGS.get(w, w)
		if w == "skates":
			text = text % int(workshop.variant().get("bevel", 25.0) + 2.0)
		elif w == "blocked":
			text = text % plan.get("wall", 0.0)
		if w == stop and stop_at >= 0.0:
			lines.insert(2, "! stops at %.0f mm: %s" % [stop_at, text])
		else:
			lines.append("! " + text)
	return "\n".join(lines)


func _panel(parent: Control, at: Vector2) -> VBoxContainer:
	var panel := PanelContainer.new()
	panel.position = at
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	parent.add_child(panel)
	var box := VBoxContainer.new()
	panel.add_child(box)
	return box


## Anchors a panel (its box's parent) to a corner, 12 pixels in, sized to its content.
func _pin(box: Control, corner: Control.LayoutPreset) -> void:
	var panel := box.get_parent() as Control
	panel.grow_horizontal = Control.GROW_DIRECTION_BEGIN if corner == Control.PRESET_TOP_RIGHT else Control.GROW_DIRECTION_END
	panel.grow_vertical = Control.GROW_DIRECTION_BEGIN if corner == Control.PRESET_BOTTOM_LEFT else Control.GROW_DIRECTION_END
	panel.set_anchors_and_offsets_preset(corner, Control.PRESET_MODE_MINSIZE, 12)


func _button(parent: Control, text: String, pressed: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(pressed)
	parent.add_child(b)
	return b


func _choice(parent: Control, label: String, items: Array, selected: int, chosen: Callable) -> OptionButton:
	var row := HBoxContainer.new()
	parent.add_child(row)
	var caption := Label.new()
	caption.text = label
	caption.custom_minimum_size.x = 60
	row.add_child(caption)
	var option := OptionButton.new()
	option.focus_mode = Control.FOCUS_NONE
	for item in items:
		option.add_item(item)
	option.select(selected)
	option.item_selected.connect(chosen)
	row.add_child(option)
	return option


## A slider for a workshop setting of its own: `changed` gets the value.
func _value_slider(parent: Control, label: String, lo: float, hi: float, step: float, value: float, format: String,
		changed: Callable) -> void:
	var row := HBoxContainer.new()
	parent.add_child(row)
	var caption := Label.new()
	caption.text = label
	caption.custom_minimum_size.x = 60
	row.add_child(caption)
	var slider := HSlider.new()
	slider.min_value = lo
	slider.max_value = hi
	slider.step = step
	slider.value = value
	slider.custom_minimum_size.x = 120
	slider.focus_mode = Control.FOCUS_NONE
	row.add_child(slider)
	var shown := Label.new()
	shown.text = format % value
	row.add_child(shown)
	slider.value_changed.connect(func(v):
		shown.text = format % v
		changed.call(v))


## A slider for one of a tool's settings, over the range the wheel sets it in while
## planning (workshop.INTENSITY / TILT).
func _slider(parent: Control, tool: String, key: String, label: String, format: String) -> void:
	var spec = workshop.INTENSITY[tool] if workshop.INTENSITY[tool][0] == key else workshop.TILT[tool]
	var lo: float = spec[2]
	var hi: float = spec[3]
	var step: float = spec[1] / 5.0 # as finely as Ctrl+wheel
	var value: float = workshop.settings[tool][key]
	var changed := func(v): workshop.set_setting(tool, key, v)
	var row := HBoxContainer.new()
	parent.add_child(row)
	var caption := Label.new()
	caption.text = label
	caption.custom_minimum_size.x = 60
	row.add_child(caption)
	var slider := HSlider.new()
	slider.min_value = lo
	slider.max_value = hi
	slider.step = step
	slider.value = value
	slider.custom_minimum_size.x = 160
	slider.focus_mode = Control.FOCUS_NONE
	row.add_child(slider)
	var shown := Label.new()
	shown.text = format % value
	row.add_child(shown)
	slider.value_changed.connect(func(v):
		shown.text = format % v
		changed.call(v))
	_sliders[tool + "/" + key] = [slider, shown, format]
