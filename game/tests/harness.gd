extends Node3D

## Shared driver for scenes run from tools/run.sh. User args, after a bare `--`:
##   --screenshot=<path>  save a PNG of the rendered frame before quitting
##   --frames=<n>         frames to render before capturing (default 4)


## Renders a few frames, optionally saves a screenshot, then quits. Headless runs have
## no renderer (frame_post_draw never fires), so they quit straight away.
func capture_and_quit() -> void:
	if DisplayServer.get_name() == "headless":
		get_tree().quit()
		return
	for i in int(user_arg("--frames", "4")):
		await RenderingServer.frame_post_draw
	var path := user_arg("--screenshot", "")
	if path != "":
		_save_screenshot(path)
	get_tree().quit()


func user_arg(name: String, default: String) -> String:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with(name + "="):
			return arg.substr(name.length() + 1)
	return default


func _save_screenshot(path: String) -> void:
	var texture := get_viewport().get_texture()
	var image: Image = texture.get_image() if texture != null else null
	if image == null or image.is_empty():
		printerr("No frame to capture: display server '%s' does not render." % DisplayServer.get_name())
		return
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	if image.save_png(path) != OK:
		printerr("Could not save screenshot to %s" % path)
		return
	print("Screenshot saved to %s (%dx%d)" % [path, image.get_width(), image.get_height()])
