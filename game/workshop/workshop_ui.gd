extends CanvasLayer

## The workshop's controls: tools, their settings, the wood, undo / redo / reset, and a
## status line (edits, how long the last edit took to apply and upload, GPU frame time).

const TOOL_LABELS := {"chisel": "1  Chisel", "saw": "2  Saw", "sanding_block": "3  Sanding block",
		"sanding_sponge": "4  Sanding sponge"}
const WOODS := {"board": "Ash", "board_oak": "Oak", "board_walnut": "Walnut"}
const HINTS := "Hold right on the board: plan a stroke (wheel: how hard, Shift+wheel: angle, Q / E: turn)   " + \
		"then left-drag: make it   Esc: drop it   Ctrl+Z / Ctrl+Shift+Z: undo, redo   " + \
		"Middle-drag: orbit   Shift+middle-drag: pan   Wheel: zoom"

var workshop

var _tool_buttons := {}
var _settings_boxes := {}
var _sliders := {}  # "tool/key" -> [slider, its value label, format]: what the wheel also sets
var _undo: Button
var _redo: Button
var _wood: OptionButton
var _status: Label
var _plan: Label  # beside the pointer: what the stroke being planned comes to
var _root: Control


func _ready() -> void:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(root)
	_root = root

	# Tools and their settings, top left.
	var left := _panel(root, Vector2(12, 12))
	var tool_row := HBoxContainer.new()
	left.add_child(tool_row)
	var group := ButtonGroup.new()
	for tool in TOOL_LABELS:
		var b := _button(tool_row, TOOL_LABELS[tool], func(): workshop.select_tool(tool))
		b.toggle_mode = true
		b.button_group = group
		_tool_buttons[tool] = b
	var chisel := VBoxContainer.new()
	left.add_child(chisel)
	_choice(chisel, "Width", ["6 mm", "12 mm", "25 mm"], 1,
			func(i): workshop.set_setting("chisel", "width", [6.0, 12.0, 25.0][i]))
	_slider(chisel, "chisel", "depth", "Depth", "%.1f mm")
	_slider(chisel, "chisel", "angle", "Angle", "%.0f°")
	var saw := VBoxContainer.new()
	left.add_child(saw)
	_slider(saw, "saw", "feed", "Feed", "%.3f mm per mm")
	var block := VBoxContainer.new()
	left.add_child(block)
	_choice(block, "Grit", ["80", "120", "240"], 1,
			func(i): workshop.set_setting("sanding_block", "grit", [80, 120, 240][i]))
	_slider(block, "sanding_block", "pressure", "Pressure", "%.2f")
	var sponge := VBoxContainer.new()
	left.add_child(sponge)
	_choice(sponge, "Grit", ["60", "120", "220"], 1,
			func(i): workshop.set_setting("sanding_sponge", "grit", [60, 120, 220][i]))
	_slider(sponge, "sanding_sponge", "pressure", "Pressure", "%.2f")
	_settings_boxes = {"chisel": chisel, "saw": saw, "sanding_block": block, "sanding_sponge": sponge}

	# The board, top right.
	var right := _panel(root, Vector2.ZERO)
	var row := HBoxContainer.new()
	right.add_child(row)
	_wood = OptionButton.new()
	_wood.focus_mode = Control.FOCUS_NONE
	for key in WOODS:
		_wood.add_item(WOODS[key])
	_wood.item_selected.connect(func(i): workshop.set_wood(WOODS.keys()[i]))
	row.add_child(_wood)
	_undo = _button(row, "Undo", func(): workshop.undo())
	_redo = _button(row, "Redo", func(): workshop.redo())
	_button(row, "New board", func(): workshop.reset_board())
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
	_pin(right, Control.PRESET_TOP_RIGHT)

	# Status and hints, bottom left.
	var bottom := _panel(root, Vector2.ZERO)
	_status = Label.new()
	bottom.add_child(_status)
	var hints := Label.new()
	hints.text = HINTS
	hints.modulate = Color(1, 1, 1, 0.7)
	bottom.add_child(hints)
	_pin(bottom, Control.PRESET_BOTTOM_LEFT)

	_plan = Label.new()
	_plan.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_plan.add_theme_color_override("font_shadow_color", Color(0, 0, 0, 0.8))
	_plan.add_theme_constant_override("shadow_offset_x", 1)
	_plan.add_theme_constant_override("shadow_offset_y", 1)
	root.add_child(_plan)
	refresh()


## The section inset, on the right under the board's panel.
func add_inset(inset: Control) -> void:
	_root.add_child(inset)
	inset.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	inset.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT, Control.PRESET_MODE_MINSIZE, 12)
	inset.offset_top = 130
	_root.move_child(_plan, -1) # the plan's line stays on top


## One line on the stroke being planned: the tool, what the wheel has set, how far it goes
## and what it comes to.
func plan_text() -> String:
	var plan: Dictionary = workshop.get_plan()
	if plan.is_empty():
		return ""
	var tool: String = workshop.current
	var s: Dictionary = workshop.settings[tool]
	var parts: Array[String] = [TOOL_LABELS[tool].substr(3)]
	var intensity = workshop.INTENSITY.get(tool)
	if intensity != null:
		parts.append(intensity[4] % s[intensity[0]])
	var tilt = workshop.TILT.get(tool)
	if tilt != null:
		parts.append(tilt[4] % s[tilt[0]])
	match tool:
		"saw":
			parts.append("%.0f mm strokes, %.2f mm deeper each" % [plan.length, plan.get("depth", 0.0)])
		"sanding_block":
			parts.append("%.0f mm rub takes %.3f mm" % [plan.length, plan.get("depth", 0.0)])
		"sanding_sponge":
			parts.append("rounds over what it rubs")
		_:
			parts.append("%.0f mm" % plan.length)
	return "   ".join(parts)


## Tool buttons, which settings show, whether undo and redo apply.
func refresh() -> void:
	if workshop == null or _undo == null:
		return
	for tool in _tool_buttons:
		_tool_buttons[tool].set_pressed_no_signal(tool == workshop.current)
		_settings_boxes[tool].visible = tool == workshop.current
	# The wheel sets these too, while a stroke is planned.
	for id in _sliders:
		var parts: PackedStringArray = id.split("/")
		var value: float = workshop.settings[parts[0]][parts[1]]
		_sliders[id][0].set_value_no_signal(value)
		_sliders[id][1].text = _sliders[id][2] % value
	var stats: Dictionary = workshop.board.get_stats()
	_undo.disabled = not stats.get("can_undo", false)
	_redo.disabled = not stats.get("can_redo", false)
	_wood.select(WOODS.keys().find(workshop.wood))


func update_status() -> void:
	# Beside the pointer: the plan, or how to make one.
	var line := plan_text()
	if workshop.is_planning():
		line += "\nleft-drag: make it"
	elif line != "" or workshop.is_engaged():
		pass
	elif workshop.current != "":
		line = "hold right on the board to plan a stroke"
	_plan.text = line
	_plan.position = workshop.get_viewport().get_mouse_position() + Vector2(18, 14)
	var stats: Dictionary = workshop.board.get_stats()
	var gpu := RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
	var state := ""
	if stats.get("overlay_edits", 0) > 0:
		state = "   (previewing %d edits)" % stats.overlay_edits
	elif workshop.board.is_busy():
		state = "   (refining...)" if stats.get("refine_pending", false) else "   (applying...)"
	_status.text = "%d edits in %d strokes   last edit applied in %.0f ms (bricks on the %s), uploaded in %.0f ms%s   GPU %.1f ms   %d fps" % [
			stats.get("edits", 0), stats.get("steps", 0), stats.get("update_ms", 0.0),
			str(stats.get("sampler", "cpu")).to_upper(), stats.get("upload_ms", 0.0), state, gpu,
			Engine.get_frames_per_second()]


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


func _choice(parent: Control, label: String, items: Array, selected: int, chosen: Callable) -> void:
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


## A slider for one of a tool's settings, over the range the wheel sets it in while
## planning (workshop.INTENSITY / TILT).
func _slider(parent: Control, tool: String, key: String, label: String, format: String) -> void:
	var spec = workshop.INTENSITY[tool] if workshop.INTENSITY[tool][0] == key else workshop.TILT[tool]
	var lo: float = spec[2]
	var hi: float = spec[3]
	var step: float = spec[1]
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
