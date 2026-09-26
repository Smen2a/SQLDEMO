extends Node3D

## The workshop: a board on a bench and eight hand tools, every one an SDF body. Pick a
## tool (click it on the bench, or 1 to 8; Tab for its variants) and point at the board:
## its footprint shows where it would go. Then:
##   use   left-drag on the board: the tool sets to work where it was pressed, going the
##         way the drag goes (the sanding tools follow it anywhere; a chisel held up to
##         chop strikes a blow at the click). Let go to finish: one undo step. A line by
##         the pointer says what the stroke comes to: how deep the wood lets it go, the
##         force it takes, the grain, and what goes wrong.
##   plan  or first hold the right button on the board: the stroke is locked in there and
##         runs towards the pointer, its cut hatched on the board before it is made. The
##         wheel sets how hard it works (a chisel's or gouge's depth, the spokeshave's
##         shaving, the saw's feed, the pressure on the others); Shift+wheel a chisel's or
##         gouge's angle to the work (C: straight up, to chop), or the rasp's tilt about
##         its line; Q / E skew a chisel's edge, or turn the sanding tools. Then press the
##         left button and drag: the tool follows the plan as far as you take it. Keep
##         holding the right to plan the next pass from the same spot.
## The tools cut as the wood lets them (core tools/cutting.h):
##   chisel          pares along its path, as deep as a hand can push it in this wood and
##                   grain. From an edge or a cut it goes in at once; in the middle of a
##                   face it has to be tipped past its bevel, and dives. Against the grain it
##                   tears out. Chopped (C, then a click for each mallet blow) it drives a
##                   slit, and near an open face pops the chip off. Variants: bench chisels,
##                   a paring chisel (never struck), a mortise chisel, a skew chisel
##   gouge           a carving gouge (#3, #7), a veiner or a V-tool: with its corners out of
##                   the wood it goes deeper, anywhere, than a chisel
##   saw             stroke it back and forth along its line: the kerf deepens as it goes
##   rasp            back and forth along its line: takes the surface down steadily and never
##                   tears the grain; tilted, chamfers an arris; its round face hollows
##   spokeshave      its sole on the work, an even shaving of the depth it is set to: on a
##                   flat face, over a curve, bridging hollows shorter than its sole
##   card scraper    back and forth: a whisper a stroke, for cleaning up tear-out
##   sanding block   takes the surface down wherever it rubs, flat
##   sanding sponge  rounds over the arrises and ridges it rubs (a smoothing layer)
## The tool in hand stays out of sight until it works, so it never hides where it goes.
## A chisel, gouge or spokeshave goes no faster than a hand works it (WORKING_SPEED, slower
## as the wood resists, times the workshop's pace): dragged ahead, it follows; let go, the
## stroke ends where the tool got to. While it works, its blade pushes aside loose pieces in
## its way (it never cuts under them). Where a rule stops a plan short (a step ahead, the
## blade meeting the work, a gap too narrow, a chip too thick) a red mark shows where, and the
## line by the pointer says why. Ctrl+wheel sets depths in hundredths of a millimetre.
## Esc drops the plan or the stroke in progress, Ctrl+Z / Ctrl+Shift+Z undo and redo.
## Middle-drag orbits the camera, Shift+middle-drag pans, the wheel zooms.
##
## A saw cut that goes right through leaves the board in two pieces: the smaller one comes
## away as a rigid body and slides off the kerf, the larger stays on the bench as the
## board. So does a piece that cuts meeting leave free (a rebate sawn off the end: one cut
## down, one in from the end): it shows as its own body once it has been cut out, then
## falls or rests as it will on the board, whose collider follows its surface from then on.
## Undo straight after puts them back together.
##
## What the tools take off comes away too (debris.gd): a chisel's, gouge's or spokeshave's
## shaving curls up off the edge as it goes and drops when it breaks or the stroke ends;
## tear-out and a chop's pop-off throw chips; the saw, the rasps, the scraper and the sanding
## tools throw dust, which piles up where it lands. They lie where they fall; undo takes a
## stroke's back, and Sweep clears the bench.
##
## The world is in metres with y up. Bodies are in millimetres with z up: each body node is
## scaled by 0.001 and turned -90 degrees about x.

const MM := 0.001
const HOVER_LIFT := 15.0 # mm the tool floats above where it will engage
const OrbitCamera := preload("res://workshop/orbit_camera.gd")
const WorkshopUi := preload("res://workshop/workshop_ui.gd")
const Debris := preload("res://workshop/debris.gd")
const FADE_TIME := 0.1 # s for the tool in hand to fade in when it acts, and out after
const ARM_DISTANCE := 2.0 # mm a direct stroke's drag goes before it shows its direction
const SETTLE_REACH := 0.003 # m below an island it looks for what it rests on (see _settle)
const SETTLE_INTO := 0.00018 # m it starts into that (the solver's slop is 0.2 mm)
const SHAPE_MARGIN := 0.0001 # m: islands' and the hollowed board's shapes, sharp to a tenth of a mm
## mm/s a push tool goes at most, paring freely; at the force the hand can give, a fifth of it.
const WORKING_SPEED := {"chisel": 40.0, "gouge": 30.0, "spokeshave": 40.0}
const BLOW_INTERVAL := 0.35 # s between mallet blows, at the quickest
## A chop's blow: a tap, a firm blow, a heavy one (SdfBody's "blow").
const BLOWS := [0.3, 1.0, 1.6]
const BLOW_NAMES := ["tap", "firm", "heavy"]

## IDLE: pointing. PLANNING: a stroke locked in with the right button. ARMED: a direct
## stroke (the left button, no plan) waiting for its drag to show which way it goes.
## ACTING: a stroke being made.
enum { IDLE, PLANNING, ARMED, ACTING }

## Per tool, what the wheel sets while planning: [setting, step, lowest, highest, format].
const INTENSITY := {
	"chisel": ["depth", 0.05, 0.01, 1.5, "%.2f mm deep"],
	"gouge": ["depth", 0.05, 0.01, 2.5, "%.2f mm deep"],
	"saw": ["feed", 0.005, 0.005, 0.1, "feed %.3f mm per mm"],
	"rasp": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"spokeshave": ["depth", 0.02, 0.02, 0.5, "%.2f mm shaving"],
	"scraper": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"sanding_block": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"sanding_sponge": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
}
## And what Shift+wheel sets (a chisel's or gouge's angle to the work: 60 or more chops).
const TILT := {
	"chisel": ["angle", 1.0, 10.0, 90.0, "%.0f° to the work"],
	"gouge": ["angle", 1.0, 10.0, 90.0, "%.0f° to the work"],
	"rasp": ["tilt", 5.0, -60.0, 60.0, "tilted %.0f°"],
}
const CHOP_ANGLE := 60.0
## A plan's path (mm) until the pointer is dragged further than this from where it locked.
const DEFAULT_LENGTH := {"chisel": 20.0, "gouge": 20.0, "saw": 60.0, "rasp": 60.0, "spokeshave": 40.0,
		"scraper": 60.0, "sanding_block": 40.0, "sanding_sponge": 40.0}
## A direct stroke's path (mm): as far as the drag goes, up to this.
const DIRECT_LENGTH := {"chisel": 300.0, "gouge": 300.0, "spokeshave": 300.0, "rasp": 150.0, "scraper": 150.0}

const TOOL_NAMES: Array[String] = ["chisel", "gouge", "saw", "rasp", "spokeshave", "scraper", "sanding_block",
		"sanding_sponge"]
const TOOL_COLOURS := {
	"chisel": Color(1.0, 0.82, 0.25),
	"gouge": Color(1.0, 0.58, 0.2),
	"saw": Color(0.4, 0.85, 1.0),
	"rasp": Color(0.8, 0.8, 0.95),
	"spokeshave": Color(0.45, 0.95, 0.8),
	"scraper": Color(0.95, 0.95, 0.55),
	"sanding_block": Color(0.6, 1.0, 0.45),
	"sanding_sponge": Color(1.0, 0.55, 0.8),
}
const SPONGE_REACH := 10.0 # mm round its centre that the sponge bears on (core SandingSponge)

## Per tool: a chisel's or gouge's variant (SdfBody.tool_catalog()), depth (mm, at most),
## angle to the work and skew (degrees); saw feed (mm deeper per mm of stroke); grit and
## pressure (1: an ordinary hand's worth).
var settings := {
	"chisel": {"variant": "bench_12", "depth": 0.2, "angle": 30.0, "skew": 0.0, "blow": 1.0},
	"gouge": {"variant": "gouge_7_12", "depth": 0.3, "angle": 30.0, "skew": 0.0, "blow": 1.0},
	"saw": {"feed": 0.03},
	"rasp": {"variant": "rasp_cabinet", "pressure": 1.0, "tilt": 0.0},
	"spokeshave": {"depth": 0.1},
	"scraper": {"pressure": 1.0},
	"sanding_block": {"grit": 120, "pressure": 1.0},
	"sanding_sponge": {"grit": 120, "pressure": 1.0},
}
var wood := "board" ## board, board_oak or board_walnut
## How fast work goes against real life (1: as a real hand would), for every tool's speed and
## rate.
var pace := 1.0

var board                 # SdfBody
var tools := {}           # name -> SdfBody
var current := ""         # the tool in hand, or ""
var yaw := 0.0            # the tool's turn about the surface normal, radians
var camera: Camera3D

var _rest := {}           # name -> Transform3D where each tool lies on the bench
var _outline: MeshInstance3D
var _ui
var _pointer := Vector2.ZERO
var _hit := {}            # the board under the pointer (SdfBody.raycast)
var _state := IDLE
var _engaged := false     # a stroke is on the board (ACTING)
var _right_held := false
## The stroke being planned or made: {"point", "normal" (world), "plane", "along" (the
## tool's facing), "path" (unit, the direction it works in), "length" (mm), "seed",
## "direct" (made without a plan)}.
var _lock := {}
var _plan := {}           # what SdfBody.plan_stroke made of it
## The chisels and gouges (SdfBody.tool_catalog()): family -> [{id, label, width, ...}].
var variants := {}
var _progress := 0.0      # mm a push tool has gone along its path
var _target := 0.0        # mm along it the pointer asks for (the tool follows at its working speed)
var _last_blow := -1.0    # s: when the mallet last struck
var _blade := BoxShape3D.new() # round the tool in hand's blade, while it works (_place_blade)
var _blade_at := Transform3D()
var _blade_on := false
var _edge_velocity := Vector3.ZERO # m/s: the edge's, while it works
var _opacity := 0.0       # of the tool in hand, in the main view
var _plane := Plane()
var _engage_pose := Transform3D()
var _engage_time := 0.0
## Pieces sawn off, oldest first: {"body": RigidBody3D, "piece": SdfBody, "steps": the
## board's step count once its half-space landed, "spawn": where the body started}.
var offcuts: Array[Dictionary] = []
## An offcut's collider: "box" (its bounds), "hull" (convex, from its surface) or "auto"
## (a box when the piece fills nearly all its bounds, as sawn strips do: a box rests and
## slides more steadily than a hull of many points).
var offcut_collider := "auto"
var _board_collider: StaticBody3D # the board's hull, while offcuts or debris lie about
## What the tools took off (debris.gd), and the board's undo step the stroke in hand makes.
var debris
var _debris_step := 0


func _ready() -> void:
	_build_world()
	debris = Debris.new()
	add_child(debris)
	board = _new_body()
	board.load_demo(wood)
	for v in board.tool_catalog():
		if not variants.has(v.family):
			variants[v.family] = []
		variants[v.family].append(v)
	board.edited.connect(func(_stats): _ui.refresh())
	board.separated.connect(_on_separated, CONNECT_DEFERRED)
	for tool in TOOL_NAMES:
		var body = _new_body()
		body.load_tool(tool, settings[tool])
		tools[tool] = body
	_place_rests()
	for tool in TOOL_NAMES:
		tools[tool].global_transform = _rest[tool]

	var orbit = OrbitCamera.new()
	orbit.fov = 40.0
	orbit.near = 0.005
	orbit.far = 20.0
	orbit.target = Vector3(0.0, 0.012, 0.0)
	orbit.distance = 0.42
	orbit.pitch = -0.8
	add_child(orbit)
	camera = orbit

	_outline = MeshInstance3D.new()
	_outline.mesh = ImmediateMesh.new()
	_outline.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var lines := StandardMaterial3D.new()
	lines.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	lines.vertex_color_use_as_albedo = true
	_outline.material_override = lines
	add_child(_outline)

	_blade.margin = SHAPE_MARGIN

	_ui = WorkshopUi.new()
	_ui.workshop = self
	add_child(_ui)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	select_tool("chisel")


# --- driving the tools (input handlers call these; tests do too) ----------------------

func select_tool(tool: String) -> void:
	if _engaged:
		release()
	_drop_plan()
	if current != "" and tools.has(current):
		# Back on the bench: seen by everyone again.
		_show_tool(current, 1.0)
	current = tool
	_opacity = 0.0
	if current != "":
		_show_tool(current, 0.0)
	_ui.refresh()


## Moves the pointer to a screen position and looks at what is under it.
func hover_screen(position: Vector2) -> void:
	_pointer = position
	if _state == IDLE:
		_hit = _surface_at(position)


## The board under a screen position: the point hit, with the normal of the surface around
## it at the scale of a tool (a plane through hits 4 mm either side), so that a kerf or a
## groove under the pointer does not turn the tool on its side.
func _surface_at(position: Vector2) -> Dictionary:
	var hit: Dictionary = board.raycast(camera.project_ray_origin(position), camera.project_ray_normal(position), 10.0)
	if hit.is_empty() or hit.get("stale", false):
		# While an edit is being applied the body answers every ray with its last hit, so
		# there is nothing to fit a plane to: keep that hit's own normal.
		return hit
	var pixel := 2.0 * float(hit.distance) * tan(deg_to_rad(camera.fov) * 0.5) / get_viewport().get_visible_rect().size.y
	var k: float = 4.0 * MM / maxf(pixel, 1e-6)
	var around: Array[Vector3] = []
	for offset in [Vector2(k, 0), Vector2(-k, 0), Vector2(0, k), Vector2(0, -k)]:
		var at: Vector2 = position + offset
		var h: Dictionary = board.raycast(camera.project_ray_origin(at), camera.project_ray_normal(at), 10.0)
		if h.is_empty():
			return hit # at the board's edge: the point's own normal
		around.append(h.position)
	var n := (around[0] - around[1]).cross(around[2] - around[3])
	if n.length() < 1e-12:
		return hit # the hits around coincide (stale, or a sliver): the point's own normal
	n = n.normalized()
	if n.dot(hit.normal) < 0.0:
		n = -n
	hit.normal = n
	return hit


## Right button down: locks a stroke in where the pointer is on the board (the tool in hand
## set on the surface there), and plans it towards the pointer.
func lock(position: Vector2) -> void:
	_right_held = true
	if _state != IDLE or current == "":
		return
	hover_screen(position)
	if _hit.is_empty() or _hit.get("stale", false):
		return
	var normal: Vector3 = _hit.normal
	var facing := _along(normal)
	# The seed makes a plan's chips (tear-out) the same as the stroke's.
	_lock = {"point": _hit.position, "normal": normal, "plane": Plane(normal, _hit.position), "along": facing,
			"path": facing, "length": DEFAULT_LENGTH[current], "seed": randi() % 100000}
	_state = PLANNING
	camera.wheel_zoom = false
	aim(position)
	_replan()


## Pointer motion while planning: the path runs from where the stroke was locked towards
## the pointer (once it is a few millimetres away), as far as the pointer.
func aim(position: Vector2) -> void:
	_pointer = position
	if _state != PLANNING:
		return
	var point = _lock.plane.intersects_ray(camera.project_ray_origin(position), camera.project_ray_normal(position))
	if point == null:
		return
	var n: Vector3 = _lock.normal
	var d: Vector3 = point - _lock.point
	d -= n * d.dot(n)
	if d.length() < 3.0 * MM:
		return
	_lock.path = d.normalized()
	# Edge tools and the saw face the way they work; the sanding tools can be turned (Q / E).
	_lock.along = _lock.path if _faces_its_path() else _lock.path.rotated(n, yaw)
	_lock.length = clampf(d.length() / MM, 3.0, 400.0)
	_replan()


## Right button up: a plan not acted on is dropped; a stroke in progress carries on.
func unlock() -> void:
	_right_held = false
	if _state == PLANNING:
		_drop_plan()


## The wheel while planning: `steps` notches of the tool's intensity, or of its tilt; `fine`
## (Ctrl), a fifth of a notch each. Chopping, the intensity is the blow: tap, firm, heavy.
func adjust(steps: int, tilt: bool, fine := false) -> void:
	if _state != PLANNING:
		return
	if is_chopping() and not tilt:
		var at := BLOWS.find(settings[current].get("blow", 1.0))
		set_setting(current, "blow", BLOWS[clampi((at if at >= 0 else 1) + signi(steps), 0, BLOWS.size() - 1)])
		_replan()
		return
	var spec = (TILT if tilt else INTENSITY).get(current)
	if spec == null:
		return
	var step: float = spec[1] / 5.0 if fine else spec[1]
	var value: float = settings[current][spec[0]]
	set_setting(current, spec[0], clampf(snappedf(value + steps * step, step), spec[2], spec[3]))
	_replan()


## Left button down: with a stroke planned, the tool sets to work along it. Otherwise a
## tool lying on the bench under the pointer is picked up, or the tool in hand sets to work
## where the pointer is on the board, without a plan: the sanding tools (and a chop) at
## once, the others once the drag shows which way they go.
func press(position: Vector2) -> void:
	if _state == PLANNING:
		act()
		return
	if _state != IDLE:
		return
	hover_screen(position)
	var board_distance: float = _hit.distance if not _hit.is_empty() else INF
	var picked := _tool_under(position, board_distance)
	if picked != "":
		select_tool(picked)
		return
	if current == "" or _hit.is_empty() or _hit.get("stale", false):
		return
	var normal: Vector3 = _hit.normal
	var facing := _along(normal)
	_lock = {"point": _hit.position, "normal": normal, "plane": Plane(normal, _hit.position), "along": facing,
			"path": facing, "length": DIRECT_LENGTH.get(current, DEFAULT_LENGTH[current]),
			"seed": randi() % 100000, "direct": true}
	_state = ARMED
	if not _faces_its_path() or is_chopping():
		act() # at once


## The stroke begins: the tool appears where it was set, and drags carry it on. A chop is
## one mallet blow, made at once.
func act() -> void:
	if _state != PLANNING and _state != ARMED:
		return
	var now := Time.get_ticks_msec() / 1000.0
	if is_chopping() and now - _last_blow < BLOW_INTERVAL:
		return # the mallet is still coming back up
	_engage_pose = tools[current].global_transform
	_engage_time = Time.get_ticks_msec() / 1000.0
	_plane = _lock.plane
	_progress = 0.0
	_target = 0.0
	_engaged = board.begin_stroke(current, _lock.point, _lock.normal, _lock.along, _stroke_settings())
	if not _engaged:
		if _state == ARMED:
			_drop_plan()
		return
	_state = ACTING
	_debris_step = board.get_stats().get("steps", 0) + 1
	if _board_collider == null:
		# What it takes off will land on the board: its collider, in the physics space before
		# the first of it comes.
		_update_board_collider(true)
	if _lock.get("direct", false):
		# What the stroke comes to, for the line by the pointer (a chisel's, gouge's or
		# spokeshave's: the plan it was made from; nothing for the others).
		_plan = board.get_plan()
	if is_chopping():
		_last_blow = now
		board.move_stroke(_lock.point)
		release()


## Pointer motion while acting: a push tool (the chisel) goes on along its path as far as
## the pointer, never back and never past the plan's end; the saw slides along its line; the
## sanding tools follow the pointer over the plane they were set on.
func drag_screen(position: Vector2) -> void:
	_pointer = position
	if _state == ARMED:
		# A direct stroke goes the way the drag goes, once it has gone far enough to tell.
		var at = _lock.plane.intersects_ray(camera.project_ray_origin(position), camera.project_ray_normal(position))
		if at == null:
			return
		var n: Vector3 = _lock.normal
		var d: Vector3 = at - _lock.point
		d -= n * d.dot(n)
		if d.length() < ARM_DISTANCE * MM:
			return
		_lock.path = d.normalized()
		_lock.along = _lock.path
		act()
	if not _engaged:
		return
	var point = _plane.intersects_ray(camera.project_ray_origin(position), camera.project_ray_normal(position))
	if point == null:
		return
	var start: Vector3 = _lock.point
	var path: Vector3 = _lock.path
	match current:
		"chisel", "gouge", "spokeshave":
			# Where the pointer asks it to be: _process takes it there at its working speed.
			_target = clampf((point - start).dot(path) / MM, _target, _lock.length)
		"saw", "rasp", "scraper":
			board.move_stroke(start + path * (point - start).dot(path))
		_:
			board.move_stroke(point)


## Left button up: the cut is finished and becomes one undo step. With the right button
## still held, the next pass is planned from the same spot.
func release() -> void:
	if _state == ARMED:
		_drop_plan() # pressed and let go without a drag: nothing to make
		return
	if not _engaged:
		return
	var edge: Transform3D = board.get_tool_pose()
	board.end_stroke()
	_engaged = false
	_state = IDLE
	_take_debris(edge)
	if _right_held and not _lock.get("direct", false):
		_state = PLANNING
		_replan()
	else:
		_drop_plan()


func cancel() -> void:
	if _engaged:
		board.cancel_stroke()
		_engaged = false
		_take_debris(board.get_tool_pose())
	_state = IDLE
	_drop_plan()


func is_planning() -> bool:
	return _state == PLANNING


## Whether the tool in hand is held up to chop (a chisel or gouge at 60 degrees or more).
func is_chopping() -> bool:
	return (current == "chisel" or current == "gouge") and settings[current].angle >= CHOP_ANGLE


## The variant of the tool in hand (SdfBody.tool_catalog()'s entry), or {}.
func variant() -> Dictionary:
	if not variants.has(current):
		return {}
	for v in variants[current]:
		if v.id == settings[current].variant:
			return v
	return {}


## The next of the tool in hand's variants (Tab).
func next_variant() -> void:
	if not variants.has(current):
		return
	var list: Array = variants[current]
	var at := list.find(variant())
	set_setting(current, "variant", list[(at + 1) % list.size()].id)


func _faces_its_path() -> bool:
	return current in ["chisel", "gouge", "saw", "rasp", "spokeshave", "scraper"]


## A stroke's settings: the tool's, with the plan's length and seed, and the pace.
func _stroke_settings() -> Dictionary:
	var s: Dictionary = settings[current].duplicate()
	s["length"] = _lock.length
	s["seed"] = _lock.seed
	s["pace"] = pace # (for the tools that take off at a rate)
	return s


## What the plan came to (SdfBody.plan_stroke), with the lock: {} when nothing is planned
## (or a direct stroke says nothing of itself).
func get_plan() -> Dictionary:
	if _state == IDLE or _state == ARMED or (_lock.get("direct", false) and _plan.is_empty()):
		return {}
	var plan := _plan.duplicate()
	plan.merge(_lock)
	return plan


func _replan() -> void:
	_plan = board.plan_stroke(current, _lock.point, _lock.normal, _lock.along, _lock.length, _stroke_settings())
	board.set_plan_tint(Color(TOOL_COLOURS[current], 0.55))
	_ui.refresh()


func _drop_plan() -> void:
	if _state == PLANNING or _state == ARMED:
		_state = IDLE
	_lock = {}
	_plan = {}
	board.clear_plan()
	camera.wheel_zoom = true


## A tool's visibility: `opacity` 0 hides it (the tool in hand, until it works).
func _show_tool(tool: String, opacity: float) -> void:
	var body = tools[tool]
	body.opacity = opacity
	body.visible = opacity > 0.0
	# Its shadow would give it away (and it has none of its own while it fades).
	body.live_shadows = opacity >= 1.0


func undo() -> void:
	cancel()
	board.flush()
	if not offcuts.is_empty() and board.get_stats().get("steps", 0) == offcuts.back().steps:
		# Straight after a split: the pieces go back together.
		var last: Dictionary = offcuts.pop_back()
		last.body.queue_free()
		board.rejoin()
		board.flush()
		_update_board_collider()
		_ui.refresh()
		return
	debris.undo_step(board.get_stats().get("steps", 0)) # what the stroke took off goes back
	board.undo()


func redo() -> void:
	cancel()
	board.redo()


func set_wood(choice: String) -> void:
	cancel()
	for offcut in offcuts:
		offcut.body.queue_free()
	offcuts.clear()
	debris.clear()
	_update_board_collider()
	wood = choice
	board.load_demo(wood)
	_ui.refresh()


## The board came apart across a plane (world space), turned so that the smaller side is in
## front: that side becomes an offcut, a rigid body with a convex hull, nudged away from the
## kerf. The board measured both sides on its worker, so this reads nothing from it that
## waits: the half-spaces land on the pieces' workers over the next frames.
func _on_separated(point: Vector3, normal: Vector3) -> void:
	var piece = board.split(point, normal)
	if piece == null:
		return
	# An island (no plane: cuts meeting left it) is hidden until it has taken in its region;
	# its rigid body waits frozen till then.
	var island := normal == Vector3.ZERO
	# The rigid body sits at the piece's centre of mass (Godot's own follows shape origins).
	var body := RigidBody3D.new()
	add_child(body)
	var centre: Vector3 = board.global_transform * piece.get_centre_of_mass()
	body.global_transform = Transform3D(Basis.IDENTITY, centre)
	piece.transform = Transform3D(board.global_basis, board.global_position - centre)
	body.add_child(piece)
	var collider := _collider_for(piece)
	if island:
		# An island rests in the board's hollows, on convex pieces: both sharp (Godot's default
		# margin, 4 cm, rounds millimetre shapes away).
		collider.shape.margin = SHAPE_MARGIN
	body.add_child(collider)
	body.mass = maxf(piece.get_mass(), 0.005)
	# Sanded wood on a bench top. Below the width-to-height ratio of a sawn strip, so it
	# slides off the kerf rather than toppling over.
	var surface := PhysicsMaterial.new()
	surface.friction = 0.5
	body.physics_material_override = surface
	# As the last saw stroke would: a nudge off the kerf (about 6 mm of slide).
	body.linear_velocity = normal * 0.25
	body.freeze = island and not piece.visible
	# The board's step count once its half-space lands (undo then rejoins the pieces).
	offcuts.append({"body": body, "piece": piece, "steps": board.get_stats().get("steps", 0) + 1,
			"spawn": body.global_transform, "island": island})
	if not body.freeze:
		_update_board_collider()
		# Dust lying on the piece as it was: once it has slid off, whatever it held up falls.
		var held: AABB = (board.global_transform * piece.get_body_bounds()).grow(0.005)
		get_tree().create_timer(1.0).timeout.connect(func(): debris.dust.resettle(held))
	_ui.refresh()


## Lowers a piece about to be let go onto what it rests on (within 3 mm below), and says
## whether there was anything: just into it, within the solver's slop. Let go a kerf's
## width above a surface, a body's first physics step has no contact yet and it falls
## 2.7 mm into it (and out of the far side of a thin one); touching, the contact holds.
func _settle(body: RigidBody3D) -> bool:
	var collider: CollisionShape3D = body.get_child(body.get_child_count() - 1)
	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = collider.shape
	query.transform = body.global_transform * collider.transform
	query.motion = Vector3(0.0, -SETTLE_REACH, 0.0)
	query.exclude = [body.get_rid()]
	var safe: PackedFloat32Array = get_world_3d().direct_space_state.cast_motion(query)
	if safe[0] < 1.0:
		body.global_position.y -= SETTLE_REACH * safe[0] + SETTLE_INTO
	return safe[0] < 1.0


## A collider for a piece (a child of its rigid body, placed by piece.transform).
func _collider_for(piece) -> CollisionShape3D:
	var collider := CollisionShape3D.new()
	var bounds: AABB = piece.get_body_bounds()
	var fill: float = piece.get_volume() / maxf(bounds.size.x * bounds.size.y * bounds.size.z, 1e-6)
	if offcut_collider == "box" or (offcut_collider == "auto" and fill >= 0.9):
		var box := BoxShape3D.new()
		box.size = (piece.transform.basis * bounds.size).abs()
		collider.shape = box
		collider.position = piece.transform * bounds.get_center()
	else:
		var hull := ConvexPolygonShape3D.new()
		var points := PackedVector3Array()
		for p in piece.get_hull_points():
			points.push_back(piece.transform * p)
		hull.points = points
		collider.shape = hull
	return collider


## Sweeps the bench: every shaving and chip goes.
func sweep() -> void:
	debris.clear()
	_update_board_collider()


## What the stroke took off since the last frame (with the edge where the tool is now).
func _take_debris(edge: Transform3D) -> void:
	var report: Dictionary = board.take_debris()
	if report.is_empty() and debris.live_samples() == 0:
		return
	debris.feed(report, _debris_step, edge)
	if _board_collider == null and not debris.is_empty():
		_update_board_collider()


## While offcuts or debris lie about, the board has a (convex) collider too, so they rest
## on it rather than in it. `debris_coming`: a stroke that will throw some is starting.
func _update_board_collider(debris_coming := false) -> void:
	if _board_collider:
		_board_collider.queue_free()
		_board_collider = null
	if offcuts.is_empty() and debris.is_empty() and not debris_coming:
		return
	_board_collider = StaticBody3D.new()
	add_child(_board_collider)
	if offcuts.any(func(o): return o.island):
		# Where islands came out the board is hollowed (a rebate, a notch): convex pieces round
		# where they were, not one hull that would fill the hollows they sit in.
		# The seams between pieces fall a little outside each island (less than half a kerf),
		# so that it does not settle on one and catch on its neighbour's side.
		var holes := []
		for offcut in offcuts:
			if offcut.island:
				holes.append(offcut.piece.get_body_bounds().grow(0.3))
		for points in board.get_collision_hulls(holes):
			var hull := ConvexPolygonShape3D.new()
			var placed := PackedVector3Array()
			for p in points:
				placed.push_back(board.transform * p)
			hull.points = placed
			hull.margin = SHAPE_MARGIN
			var piece_collider := CollisionShape3D.new()
			piece_collider.shape = hull
			_board_collider.add_child(piece_collider)
		return
	# As an offcut's: a box while it fills nearly all its bounds (a board with cuts in it), which
	# pieces rest on steadily. (A hull of its surface has points bunched within a millimetre at
	# its eased corners, and the physics engine lets pieces sink into it.)
	var collider := _collider_for(board)
	collider.shape.margin = SHAPE_MARGIN
	_board_collider.add_child(collider)


func reset_board() -> void:
	set_wood(wood)


func set_setting(tool: String, key: String, value) -> void:
	settings[tool][key] = value
	# A chisel's or gouge's model is its variant, held at its angle; the others' look does
	# not change.
	if (tool == "chisel" or tool == "gouge") and (key == "variant" or key == "angle"):
		tools[tool].load_tool(tool, settings[tool])
		if tool == current:
			_show_tool(tool, _opacity)
	if _state == PLANNING and tool == current:
		_replan()


func is_engaged() -> bool:
	return _engaged


## Whether the tool in hand is still catching up with where it was dragged.
func lagging() -> bool:
	return _engaged and _progress < _target - 1e-3


## mm/s the push tool in hand goes at most: freely, its WORKING_SPEED; slower as the force the
## cut takes nears what the hand can give (a fifth of it there); times the pace.
func working_speed() -> float:
	var free: float = WORKING_SPEED.get(current, 40.0)
	var effort := clampf(_plan.get("force", 0.0) / maxf(_plan.get("available", 1.0), 1.0), 0.0, 1.0)
	return free * lerpf(1.0, 0.2, effort) * pace


# --- per frame ---------------------------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		match _state:
			ACTING, ARMED:
				drag_screen(event.position)
			PLANNING:
				aim(event.position)
			_:
				hover_screen(event.position)
	elif event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		match button.button_index:
			MOUSE_BUTTON_LEFT:
				if button.pressed:
					press(button.position)
				else:
					release()
			MOUSE_BUTTON_RIGHT:
				if button.pressed:
					lock(button.position)
				else:
					unlock()
			MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN:
				if button.pressed and _state == PLANNING:
					adjust(1 if button.button_index == MOUSE_BUTTON_WHEEL_UP else -1, button.shift_pressed,
							button.ctrl_pressed)
	elif event is InputEventKey and event.pressed and not event.echo:
		var key := event as InputEventKey
		match key.keycode:
			KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8:
				select_tool(TOOL_NAMES[key.keycode - KEY_1])
			KEY_TAB:
				next_variant()
			KEY_C:
				if current == "chisel" or current == "gouge":
					set_setting(current, "angle", 30.0 if is_chopping() else 90.0)
			KEY_Q, KEY_E:
				var turn := 15.0 if key.keycode == KEY_Q else -15.0
				if current == "chisel" or current == "gouge":
					# The hand skews the edge across the push: a slicing cut.
					set_setting(current, "skew", clampf(settings[current].skew + turn, -45.0, 45.0))
				else:
					yaw += deg_to_rad(turn)
					if _state == PLANNING:
						if not _faces_its_path():
							_lock.along = _lock.path.rotated(_lock.normal, yaw)
						_replan()
			KEY_ESCAPE:
				cancel()
			KEY_Z:
				if key.ctrl_pressed and key.shift_pressed:
					redo()
				elif key.ctrl_pressed:
					undo()
			KEY_Y:
				if key.ctrl_pressed:
					redo()


func _process(delta: float) -> void:
	if _state == IDLE:
		# Keep looking under the pointer: the board may have changed under it.
		hover_screen(_pointer)
	var follow := 1.0 - exp(-delta * 18.0)
	for tool in TOOL_NAMES:
		var body = tools[tool]
		if tool == current and _engaged:
			# Drop onto the work quickly, then ride exactly where the stroke puts it.
			var t: float = clamp((Time.get_ticks_msec() / 1000.0 - _engage_time) / 0.08, 0.0, 1.0)
			body.global_transform = _engage_pose.interpolate_with(board.get_tool_pose(), t)
		elif tool == current and _state == PLANNING:
			# Set on the work where the stroke starts, out of sight until it acts.
			body.global_transform = board.pose_at(_lock.point, _lock.normal, _lock.along, 0.0)
		elif tool == current:
			body.global_transform = body.global_transform.interpolate_with(_hover_pose(), follow)
		else:
			body.global_transform = body.global_transform.interpolate_with(_rest[tool], follow)
	# A push tool follows where it was dragged at its working speed.
	var went := 0.0
	if _engaged and WORKING_SPEED.has(current) and _progress < _target:
		went = minf(_target, _progress + working_speed() * delta) - _progress
		_progress += went
		board.move_stroke(_lock.point + _lock.path * (_progress * MM))
	_place_blade(_lock.get("path", Vector3.ZERO) * (went * MM / maxf(delta, 1e-6)))
	# (After a stroke too: the sponge's last work lands after it ends.)
	_take_debris(board.get_tool_pose())
	# A plan asked for while an edit was landing is planned again once it has.
	if _state == PLANNING and _plan.get("stale", false):
		var fresh: Dictionary = board.get_plan()
		if not fresh.is_empty() and not fresh.get("stale", false):
			_plan = fresh
	# The tool in hand is seen in the main view only while it works, fading in and out.
	if current != "":
		var target := 1.0 if _state == ACTING else 0.0
		if _opacity != target:
			_opacity = move_toward(_opacity, target, delta / FADE_TIME)
			_show_tool(current, _opacity)
	# An island's piece shows once its region is in: the board's collider follows its surface
	# as it now is, and a few physics steps later (once the collider is among the bodies it
	# can meet) the island is set down on what is under it, and let go.
	for offcut in offcuts:
		if offcut.body.freeze and offcut.piece.visible:
			if not offcut.has("release"):
				_update_board_collider()
				offcut.release = Engine.get_physics_frames() + 3
				offcut.settled = false
			elif not offcut.settled and Engine.get_physics_frames() >= offcut.release - 1:
				# A step before it is let go, so that the step sees it there.
				offcut.resting = _settle(offcut.body)
				offcut.settled = true
			elif offcut.settled and Engine.get_physics_frames() >= offcut.release:
				offcut.body.freeze = false
				# Set down on something, it lies there until something moves it (it would rock on
				# the few points a board's sampled surface gives it); with nothing under it, it falls.
				offcut.body.sleeping = offcut.resting
	_draw_outline()
	_ui.update_status()


## Where the tool in hand's blade is while a chisel or gouge works (a box round it for the
## 30 mm behind its edge, rising at its angle to the work), and how fast its edge goes (m/s).
func _place_blade(velocity: Vector3) -> void:
	_blade_on = _engaged and (current == "chisel" or current == "gouge") and not is_chopping()
	_edge_velocity = velocity if _blade_on else Vector3.ZERO
	if not _blade_on:
		return
	var pose: Transform3D = board.get_tool_pose()
	var x := pose.basis.x.normalized()
	var z := pose.basis.z.normalized()
	var a := deg_to_rad(settings[current].angle)
	var back := -x * cos(a) + z * sin(a)
	var over := x * sin(a) + z * cos(a)
	var size := Vector3(30.0, variant().get("width", 12.0), 4.0) * MM
	if _blade.size != size:
		_blade.size = size
	# From a millimetre ahead of the edge back along the blade, and from its flat back through
	# its thickness.
	_blade_at = Transform3D(Basis(back, over.cross(back), over), pose.origin + back * 0.014 - over * 0.002)


func _physics_process(delta: float) -> void:
	_push_aside(delta)


## Loose pieces (offcuts, islands) the blade meets go on ahead of the edge as it advances:
## never cut under. They are moved with it, not kicked: a push as fast as the edge is less
## than friction takes off a piece of a few grams in one physics step, so it would not move.
## (A solid blade, a kinematic body, wedged under them: at this scale the physics engine let
## it slide under, or tipped them and buried them in the board. A plate standing up at the
## edge flung them off it.)
func _push_aside(delta: float) -> void:
	var speed := _edge_velocity.length()
	if not _blade_on or speed < 1e-5:
		return
	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = _blade
	query.transform = _blade_at
	query.collision_mask = 1 # (the pieces; not the shavings and chips it makes: debris.gd LAYER)
	query.margin = 0.0003 # (touching counts)
	for hit in get_world_3d().direct_space_state.intersect_shape(query, 8):
		var piece = hit.collider
		if piece is RigidBody3D and not piece.freeze:
			# Along the board: the edge's advance this step, less any way it goes up or down.
			var advance: Vector3 = _edge_velocity * delta
			piece.global_position += advance - Vector3.UP * advance.dot(Vector3.UP)
			piece.sleeping = false


## Where the tool in hand floats: over the board under the pointer, or over its middle.
func _hover_pose() -> Transform3D:
	if _hit.is_empty():
		var up := Vector3.UP
		return board.pose_at(board.global_position + up * 0.0125, up, _along(up), 60.0)
	return board.pose_at(_hit.position, _hit.normal, _along(_hit.normal), HOVER_LIFT)


## The tool's facing on a surface, turned by `yaw`: the chisel pushes away from the viewer;
## the saw, the block and the sponge lie across the view, so the saw is seen side on and
## stroked left and right.
func _along(normal: Vector3) -> Vector3:
	normal = normal.normalized() if normal.length() > 1e-6 else Vector3.UP
	var base := -camera.global_basis.z if current == "chisel" or current == "gouge" else camera.global_basis.x
	var along := base - normal * base.dot(normal)
	if along.length() < 1e-4:
		along = camera.global_basis.y - normal * camera.global_basis.y.dot(normal)
	return along.normalized().rotated(normal, yaw)


## The tool (other than the one in hand) nearer than `nearest` under a screen position.
func _tool_under(position: Vector2, nearest: float) -> String:
	var from := camera.project_ray_origin(position)
	var dir := camera.project_ray_normal(position)
	var best := ""
	for tool in TOOL_NAMES:
		if tool == current:
			continue
		var hit: Dictionary = tools[tool].raycast(from, dir, 10.0)
		if not hit.is_empty() and hit.distance < nearest and not hit.has("stale"):
			nearest = hit.distance
			best = tool
	return best


## The footprint of the tool in hand where it would engage: the chisel's edge and push
## direction, the saw's line, the sanding block's face, the sponge's reach.
func _draw_outline() -> void:
	var mesh: ImmediateMesh = _outline.mesh
	mesh.clear_surfaces()
	if current == "" or _engaged:
		return
	var n: Vector3
	var p: Vector3
	var a: Vector3
	if _state == PLANNING or _state == ARMED:
		n = _lock.normal
		p = _lock.point + n * 0.0003
		a = _lock.along
	elif _hit.is_empty():
		return
	else:
		n = _hit.normal
		p = _hit.position + n * 0.0003
		a = _along(n)
	var s := n.cross(a)
	mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	mesh.surface_set_color(TOOL_COLOURS[current])
	var segments: Array[Vector3] = []
	if _state == PLANNING:
		# The planned path, from where the stroke was locked.
		segments.append_array([p, p + _lock.path * (_lock.length * MM)])
	match current:
		"chisel", "gouge":
			var w: float = variant().get("width", 12.0) * 0.5 * MM
			segments = [p - s * w, p + s * w, p, p + a * 0.012, p + a * 0.012, p + a * 0.009 + s * 0.002,
					p + a * 0.012, p + a * 0.009 - s * 0.002]
		"saw":
			var l := 0.07
			segments = [p - a * l, p + a * l, p - a * l - s * 0.003, p - a * l + s * 0.003,
					p + a * l - s * 0.003, p + a * l + s * 0.003]
		"rasp", "scraper", "spokeshave":
			# Its face or blade across the stroke, as wide as it is.
			var half: float = {"rasp": 12.5, "scraper": 30.0, "spokeshave": 25.0}[current] * MM
			segments.append_array([p - s * half, p + s * half, p - s * half, p - s * half + a * 0.006,
					p + s * half, p + s * half + a * 0.006])
		"sanding_block":
			var hl := 0.035
			var hb := 0.020
			var c := [p - a * hl - s * hb, p + a * hl - s * hb, p + a * hl + s * hb, p - a * hl + s * hb]
			for i in 4:
				segments.append(c[i])
				segments.append(c[(i + 1) % 4])
		"sanding_sponge":
			var r := SPONGE_REACH * MM
			for i in 24:
				segments.append(p + (a * cos(TAU * i / 24.0) + s * sin(TAU * i / 24.0)) * r)
				segments.append(p + (a * cos(TAU * (i + 1) / 24.0) + s * sin(TAU * (i + 1) / 24.0)) * r)
	for v in segments:
		mesh.surface_add_vertex(v)
	# Where a rule stops the plan short: a red cross there.
	var stop_at: float = _plan.get("stop_at", -1.0) if _state == PLANNING else -1.0
	if stop_at >= 0.0:
		var at: Vector3 = p + _lock.path * (stop_at * MM)
		var across := n.cross(_lock.path) * 0.004
		var ahead: Vector3 = _lock.path * 0.004
		mesh.surface_set_color(Color(1.0, 0.25, 0.2))
		for v in [at - across - ahead, at + across + ahead, at - across + ahead, at + across - ahead]:
			mesh.surface_add_vertex(v)
	mesh.surface_end()


# --- the world ---------------------------------------------------------------------------

func _new_body():
	var body = ClassDB.instantiate("SdfBody")
	add_child(body)
	# Body millimetres, z up -> world metres, y up; the board's underside on the bench top.
	body.transform = Transform3D(Basis(Vector3.RIGHT, -PI / 2) * Basis.from_scale(Vector3.ONE * MM),
			Vector3(0.0, 0.0125, 0.0))
	return body


## A tool model's frame (see core tools.h: z away from the work, x its working direction)
## laid out in the world, `lift` metres up.
func _frame(origin: Vector3, z_axis: Vector3, x_axis: Vector3) -> Transform3D:
	var basis := Basis(x_axis, z_axis.cross(x_axis), z_axis)
	return Transform3D(basis * Basis.from_scale(Vector3.ONE * MM), origin)


func _place_rests() -> void:
	# Round the board, lying as they would: the chisel with its blade flat, the saw on its
	# side, the block on its paper, the sponge on a face.
	var chisel := _frame(Vector3(-0.07, 0.0115, 0.1), Vector3.UP, Vector3.LEFT)
	chisel.basis = chisel.basis * Basis(Vector3(0, 1, 0), -deg_to_rad(20.0))
	_rest["chisel"] = chisel
	var gouge := _frame(Vector3(-0.02, 0.0115, 0.145), Vector3.UP, Vector3.LEFT)
	gouge.basis = gouge.basis * Basis(Vector3(0, 1, 0), -deg_to_rad(20.0))
	_rest["gouge"] = gouge
	_rest["rasp"] = _frame(Vector3(0.02, 0.0, -0.2), Vector3.UP, Vector3.LEFT)
	_rest["spokeshave"] = _frame(Vector3(-0.15, 0.0, -0.06), Vector3.UP, Vector3.FORWARD)
	# The scraper stands on its edge against nothing: it lies flat, on its face.
	_rest["scraper"] = _frame(Vector3(0.16, 0.0004, 0.1), Vector3.RIGHT, Vector3.FORWARD)
	var saw := Transform3D(Basis(Vector3.RIGHT, Vector3.UP, Vector3.BACK) * Basis.from_scale(Vector3.ONE * MM),
			Vector3(0.0, 0.011, -0.12))
	_rest["saw"] = saw
	_rest["sanding_block"] = _frame(Vector3(0.1, 0.0, 0.1), Vector3.UP, Vector3.RIGHT)
	_rest["sanding_sponge"] = _frame(Vector3(0.165, 0.0, -0.005), Vector3.UP, Vector3.FORWARD)


func _build_world() -> void:
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.16, 0.17, 0.19)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.53, 0.6)
	env.ambient_light_energy = 0.35
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	var world := WorldEnvironment.new()
	world.environment = env
	add_child(world)

	var sun := DirectionalLight3D.new()
	sun.light_color = Color(1.0, 0.96, 0.9)
	sun.light_energy = 0.9
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 3.0
	add_child(sun)
	sun.look_at_from_position(Vector3.ZERO, Vector3(0.45, -0.75, -0.5), Vector3.UP)

	# A bench: a thick top on four legs, the top's surface at y = 0.
	var oak := StandardMaterial3D.new()
	oak.albedo_color = Color(0.3, 0.2, 0.13)
	oak.roughness = 0.8
	var top := MeshInstance3D.new()
	var slab := BoxMesh.new()
	slab.size = Vector3(0.9, 0.05, 0.55)
	top.mesh = slab
	top.material_override = oak
	top.position = Vector3(0.0, -0.025, 0.0)
	add_child(top)
	# Offcuts land on the bench and the floor.
	var bench := StaticBody3D.new()
	var bench_shape := CollisionShape3D.new()
	var bench_box := BoxShape3D.new()
	bench_box.size = slab.size
	bench_shape.shape = bench_box
	bench_shape.position = top.position
	bench.add_child(bench_shape)
	add_child(bench)
	for sx in [-1.0, 1.0]:
		for sz in [-1.0, 1.0]:
			var leg := MeshInstance3D.new()
			var post := BoxMesh.new()
			post.size = Vector3(0.06, 0.8, 0.06)
			leg.mesh = post
			leg.material_override = oak
			leg.position = Vector3(sx * 0.38, -0.45, sz * 0.22)
			add_child(leg)
	var ground := MeshInstance3D.new()
	var sheet := PlaneMesh.new()
	sheet.size = Vector2(6.0, 6.0)
	ground.mesh = sheet
	var grey := StandardMaterial3D.new()
	grey.albedo_color = Color(0.32, 0.32, 0.33)
	ground.material_override = grey
	ground.position = Vector3(0.0, -0.85, 0.0)
	add_child(ground)
	var floor_body := StaticBody3D.new()
	var floor_shape := CollisionShape3D.new()
	floor_shape.shape = WorldBoundaryShape3D.new()
	floor_shape.position = ground.position
	floor_body.add_child(floor_shape)
	add_child(floor_body)
