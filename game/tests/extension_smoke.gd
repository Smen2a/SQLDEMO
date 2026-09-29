extends Node

## Checks the SDF GDExtension loads, its classes are registered and a demo body compiles.


func _ready() -> void:
	if not ClassDB.class_exists("SdfBody"):
		printerr("SdfBody is not registered: the GDExtension did not load")
		get_tree().quit(1)
		return
	var body = ClassDB.instantiate("SdfBody")
	if not body.load_demo("carved_panel"):
		printerr("SdfBody could not build the carved panel")
		body.free()
		get_tree().quit(1)
		return
	print("extension smoke: carved panel ", body.get_stats(), " bounds ", body.get_body_bounds())
	body.free()
	get_tree().quit()
