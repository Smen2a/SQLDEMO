extends PanelContainer

## The cross-section inset: a small side-on view through a planned or working stroke, cut
## open along its path, so the tool's angle and the cut's depth show as they would in a
## sectioned drawing (and the grain, which the wood's colour follows through its depth).
## An orthographic camera in a SubViewport sharing the workshop's world looks across the
## path; the board and the tool in hand draw themselves cut open along it only in
## orthographic views (SdfBody.set_section), so the tool shows its profile (a chisel's
## bevel, a spokeshave's blade in its mouth) and nothing in front hides the cut. The tool
## in hand, hidden from the main view while a stroke is planned, is on a layer this camera
## sees; the camera starts drawing just short of the section, so tools lying on the bench
## in front of the board stay out of it.

const SIZE := Vector2i(360, 240)

const REACH := 0.2        # m from the camera to the section plane
const IN_FRONT := 0.04    # m in front of the plane it still draws

var board                  # SdfBody, cut open while the inset shows
var tool                   # the SdfBody in hand, cut open with it (or null)
var cull_mask := 0xFFFFF   # the layers the inset camera draws

var _viewport: SubViewport
var _camera: Camera3D
var _caption: Label
var _shown := []  # what it last drew (planning: it draws again only when that changes)


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	var box := VBoxContainer.new()
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(box)
	_caption = Label.new()
	_caption.text = "Section along the stroke"
	_caption.modulate = Color(1, 1, 1, 0.75)
	_caption.custom_minimum_size.x = SIZE.x
	_caption.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(_caption)
	var container := SubViewportContainer.new()
	container.mouse_filter = Control.MOUSE_FILTER_IGNORE
	container.custom_minimum_size = Vector2(SIZE)
	box.add_child(container)
	_viewport = SubViewport.new()
	_viewport.size = SIZE
	_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	container.add_child(_viewport)
	_camera = Camera3D.new()
	_camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	_camera.near = REACH - IN_FRONT
	_camera.far = 1.0
	_camera.cull_mask = cull_mask
	_viewport.add_child(_camera)
	_camera.current = true
	visible = false


## Shows the section along the path through `start` in the direction `along` (world, unit,
## in the surface), with the surface's `normal` up: the path runs left to right and the
## part of the board nearer the viewer is cut away. The view frames `focus` (where the
## tool's edge is) closely enough to see a cut `depth` mm deep, with room above for the tool.
## `live` (a stroke being made) draws every frame; otherwise (a plan) only when the view,
## the caption or the board changes, which spares a second raymarch a frame. `least` (mm):
## the least height it frames (a big tool needs more to be seen whole).
func show_section(start: Vector3, along: Vector3, normal: Vector3, focus: Vector3, depth: float, caption: String,
		live := true, least := 14.0) -> void:
	var across := along.cross(normal).normalized() # towards the viewer: the side cut away
	var height := clampf(depth * 3.0 + 10.0, least, 60.0) * 0.001
	var width := height * float(SIZE.x) / float(SIZE.y)
	# The edge a third of the way in, the surface a little above the middle.
	focus -= across * (focus - start).dot(across)
	var centre := focus + along * (width * 0.2) + normal * (height * 0.1 - depth * 0.0005)
	_camera.size = height
	_camera.global_transform = Transform3D(Basis(along, normal, across), centre + across * REACH)
	board.set_section(start, across)
	if tool != null:
		tool.set_section(start, across)
	_caption.text = caption
	visible = true
	if live:
		_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
		_shown = []
		return
	var stats: Dictionary = board.get_stats()
	var now := [_camera.global_transform, height, caption, stats.get("edits", 0), stats.get("overlay_edits", 0),
			stats.get("update_ms", 0.0)]
	if now != _shown:
		_shown = now
		_viewport.render_target_update_mode = SubViewport.UPDATE_ONCE
	elif _viewport.render_target_update_mode == SubViewport.UPDATE_ALWAYS:
		_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED


func hide_section() -> void:
	if not visible:
		return
	visible = false
	_shown = []
	_viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	board.set_section(Vector3.ZERO, Vector3.ZERO)
	if tool != null:
		tool.set_section(Vector3.ZERO, Vector3.ZERO)


## The tool in hand (null for none): cut open with the board from now on.
func set_tool(body) -> void:
	if tool != null and tool != body:
		tool.set_section(Vector3.ZERO, Vector3.ZERO)
	tool = body
