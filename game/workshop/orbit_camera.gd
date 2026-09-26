extends Camera3D

## Orbits a target point: middle-drag turns, Shift+middle-drag pans, the wheel zooms (the
## right button is the workshop's: it locks a stroke in).

@export var target := Vector3.ZERO
@export var distance := 0.4          ## metres from the target
@export var yaw := 0.0               ## radians about the world up axis
@export var pitch := -0.75           ## radians; negative looks down
@export var min_distance := 0.05
@export var max_distance := 3.0
## Whether the wheel zooms (the workshop takes it while a stroke is being planned).
var wheel_zoom := true

var _turning := false
var _panning := false


func _ready() -> void:
	_apply()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		match button.button_index:
			MOUSE_BUTTON_MIDDLE:
				_turning = button.pressed and not button.shift_pressed
				_panning = button.pressed and button.shift_pressed
			MOUSE_BUTTON_WHEEL_UP:
				if button.pressed and wheel_zoom:
					distance = max(distance * 0.9, min_distance)
			MOUSE_BUTTON_WHEEL_DOWN:
				if button.pressed and wheel_zoom:
					distance = min(distance / 0.9, max_distance)
		_apply()
	elif event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		if _turning:
			yaw -= motion.relative.x * 0.006
			pitch = clamp(pitch - motion.relative.y * 0.006, -1.5, 0.3)
			_apply()
		elif _panning:
			# Move the target in the view plane, as far as the pointer moved on screen.
			var per_pixel := 2.0 * distance * tan(deg_to_rad(fov) * 0.5) / get_viewport().get_visible_rect().size.y
			target += (-global_basis.x * motion.relative.x + global_basis.y * motion.relative.y) * per_pixel
			_apply()


func _apply() -> void:
	var offset := Vector3(cos(pitch) * sin(yaw), -sin(pitch), cos(pitch) * cos(yaw)) * distance
	look_at_from_position(target + offset, target, Vector3.UP)
