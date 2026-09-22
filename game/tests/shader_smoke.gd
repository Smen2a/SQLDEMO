extends "res://tests/harness.gd"

## Renders a quad with shader_smoke.gdshader, so a broken shared SDF include shows up as
## a SHADER ERROR in the log (which tools/test_godot.sh fails on).


func _ready() -> void:
	var camera := Camera3D.new()
	camera.position = Vector3(0, 0, 1.2)
	add_child(camera)

	var quad := MeshInstance3D.new()
	quad.mesh = QuadMesh.new()
	var material := ShaderMaterial.new()
	material.shader = load("res://tests/shader_smoke.gdshader")
	quad.material_override = material
	add_child(quad)

	print("shader smoke: renderer %s" % RenderingServer.get_video_adapter_name())
	capture_and_quit()
