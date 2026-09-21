extends Control

## Builds a small result set, prints it the way a SQL shell would, draws the
## same rows on screen, and (when asked) saves a screenshot before quitting.
##
## User args, passed after a bare `--`:
##   --screenshot=<path>  save a PNG of the rendered frame, then quit
##   --quit               quit once the result has been printed

const EMPLOYEES: Array[Dictionary] = [
	{"id": 1, "name": "Ada Lovelace", "team": "Engine", "salary": 142000.0},
	{"id": 2, "name": "Grace Hopper", "team": "Compiler", "salary": 155000.0},
	{"id": 3, "name": "Alan Turing", "team": "Engine", "salary": 138500.0},
	{"id": 4, "name": "Radia Perlman", "team": "Network", "salary": 149250.0},
	{"id": 5, "name": "Barbara Liskov", "team": "Compiler", "salary": 161000.0},
	{"id": 6, "name": "Karen Sparck Jones", "team": "Search", "salary": 133750.0},
]

const QUERY_TEXT := "SELECT name, team, salary FROM employees\n  WHERE salary > 135000 ORDER BY salary DESC LIMIT 4;"

var _result: Query


func _ready() -> void:
	var employees := Query.new(
		PackedStringArray(["id", "name", "team", "salary"]), EMPLOYEES
	)
	_result = (employees
		.where(func(row): return row["salary"] > 135000.0)
		.order_by("salary", true)
		.limit(4)
		.select(PackedStringArray(["name", "team", "salary"])))

	_print_result()
	_build_ui()

	var screenshot_path := _user_arg("--screenshot")
	if screenshot_path != "":
		await _save_screenshot(screenshot_path)
		get_tree().quit()
	elif _has_user_arg("--quit"):
		get_tree().quit()


func _print_result() -> void:
	print("SQLDEMO — Godot %s (%s)" % [
		Engine.get_version_info()["string"],
		RenderingServer.get_video_adapter_name() if DisplayServer.get_name() != "headless" else "headless",
	])
	print("")
	print(QUERY_TEXT)
	print("")
	print(_result.to_ascii_table())


func _build_ui() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)

	var background := ColorRect.new()
	background.color = Color(0.08, 0.09, 0.12)
	background.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(background)

	var margin := MarginContainer.new()
	margin.set_anchors_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 48)
	add_child(margin)

	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 18)
	margin.add_child(column)

	column.add_child(_label("SQLDEMO", 34, Color(0.96, 0.97, 1.0)))
	column.add_child(_label(QUERY_TEXT, 17, Color(0.53, 0.78, 0.98)))

	var panel := PanelContainer.new()
	var panel_style := StyleBoxFlat.new()
	panel_style.bg_color = Color(0.12, 0.14, 0.19)
	panel_style.set_corner_radius_all(8)
	panel_style.set_content_margin_all(18)
	panel.add_theme_stylebox_override("panel", panel_style)
	column.add_child(panel)

	var grid := GridContainer.new()
	grid.columns = _result.columns.size()
	grid.add_theme_constant_override("h_separation", 42)
	grid.add_theme_constant_override("v_separation", 10)
	panel.add_child(grid)

	for header in _result.columns:
		grid.add_child(_label(header.to_upper(), 16, Color(0.55, 0.6, 0.72)))
	for row in _result.rows:
		for header in _result.columns:
			var value: Variant = row[header]
			var text: String = "%.2f" % value if value is float else str(value)
			grid.add_child(_label(text, 20, Color(0.91, 0.93, 0.97)))

	column.add_child(_label(
		"%d rows in set · rendered by Godot %s" % [
			_result.rows.size(), Engine.get_version_info()["string"]
		], 15, Color(0.45, 0.5, 0.6)))


func _label(text: String, size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	return label


func _save_screenshot(path: String) -> void:
	# Wait for a frame to actually reach the framebuffer before reading it back.
	await RenderingServer.frame_post_draw
	var viewport_texture := get_viewport().get_texture()
	var image: Image = viewport_texture.get_image() if viewport_texture != null else null
	if image == null or image.is_empty():
		printerr("No frame to capture — the display server is '%s', which does not render." % DisplayServer.get_name())
		return
	var absolute := ProjectSettings.globalize_path(path) if path.begins_with("res://") or path.begins_with("user://") else path
	DirAccess.make_dir_recursive_absolute(absolute.get_base_dir())
	var error := image.save_png(absolute)
	if error != OK:
		printerr("Could not save screenshot to %s (error %d)" % [absolute, error])
		return
	print("")
	print("Screenshot saved to %s (%dx%d)" % [absolute, image.get_width(), image.get_height()])


func _user_arg(name: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(name + "="):
			return arg.substr(name.length() + 1)
	return ""


func _has_user_arg(name: String) -> bool:
	return OS.get_cmdline_user_args().has(name)
