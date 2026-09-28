extends Node3D

## The workshop: a room with a workbench, and eight hand tools, every one an SDF body. You
## walk about it in first person (WASD, the mouse to look, Shift to hurry; room.gd,
## player.gd); the hotbar at the bottom holds the tools (1 to 9, the wheel; 0 empty-handed),
## the one in hand held in view. E picks up a piece of work lying about (carried in front
## of the eyes; R turns it, the wheel reaches it), takes a new board from a stack on the
## lumber rack, or lets go of what is carried: let go on the vise, an empty vise takes it,
## squared (flat, along the bench, on its top); F takes the piece in the vise out. At the
## bench, E steps up to it: the view comes down over the work in the vise (the mouse freed;
## Esc steps back), the piece the tools work on. There, with a tool (Tab for its
## variants), point at the work: its footprint shows where it would go. Then:
##   use   left-drag on the board: the tool sets to work where it was pressed, going the
##         way the drag goes (the sanding tools follow it anywhere; a chisel held up to
##         chop strikes a blow at the click). Let go to finish: one undo step. A line by
##         the pointer says what the stroke comes to: how deep the wood lets it go, the
##         force it takes, the grain, and what goes wrong.
##   plan  or first hold Space over the board: the stroke is locked in there and runs
##         towards the pointer, its cut hatched on the board before it is made. The wheel
##         sets how hard it works (a chisel's or gouge's depth, a plane's shaving, the
##         pressure on the others). Then press the left button and drag: the tool follows
##         the plan as far as you take it. Keep holding Space to plan the next pass from
##         the same spot.
##   hand  right-drag: the guiding hand pivots the tool on its edge, hovering or planned
##         (up / down a chisel's or gouge's angle to the work, C: straight up, to chop;
##         left / right its skew, or another tool's turn; the wheel its lean, or the
##         rasp's tilt). Q / E skew or turn it 15 degrees.
## The tools cut as the wood lets them (core tools/cutting.h):
##   chisel          pares along its path, as deep as a hand can push it in this wood and
##                   grain. From an edge or a cut it goes in at once; in the middle of a
##                   face it has to be tipped past its bevel, and dives. Against the grain it
##                   tears out. Chopped (C, then a click for each mallet blow) it drives a
##                   slit, and near an open face pops the chip off. Variants: bench chisels,
##                   a paring chisel (never struck), a mortise chisel, a skew chisel
##   gouge           a carving gouge (#3, #7), a veiner or a V-tool: with its corners out of
##                   the wood it goes deeper, anywhere, than a chisel
##   saw             stroke it back and forth along its line: the kerf deepens on each push
##                   (towards its toe), slower through a long chord and harder wood, as far
##                   as its back lets it (60 mm); the kerf runs where the teeth have been
##   rasp            back and forth along its line, cutting on the push: takes the surface
##                   down steadily where its face goes, resting on the high spots, and never
##                   tears the grain; tilted, chamfers an arris; its round face hollows
##   planes          (the slot's id: spokeshave) a spokeshave: its sole on the work, an even
##                   shaving of the depth it is set to: on a flat face, over a curve,
##                   bridging hollows shorter than its sole; as deep as two hands push the
##                   chip (deeper on a narrow edge), its mouth passes (0.8 mm) and its toe
##                   rides (a step stops it). A block plane (a 150 mm sole, a 35 mm iron
##                   inside its sides), with or without a chamfer fence (held across an
##                   arris by edge lock, at 45 degrees to the faces, or on the plane between
##                   two gauge lines); a shoulder plane (its iron flush with its sides: into
##                   a rebate's inside corner)
##   card scraper    back and forth, cutting on the push: a hundredth of a millimetre from
##                   each point its burr passes over, for cleaning up tear-out
##   sanding block   rests on the highest points under it and takes them down first, only
##                   where it rubs (a hundredth of a millimetre a metre at 120 grit in ash);
##                   faster on a narrow edge. Variants: a cork block, a small pad
##   sanding sponge  rounds over the arrises and ridges it rubs (a smoothing layer)
## The tool in hand stays out of sight until it works, so it never hides where it goes.
## Every tool goes no faster than a hand works it (WORKING_SPEED; a chisel, gouge or
## spokeshave slower as the wood resists; times the workshop's pace), and takes wood off at
## the real tool's rate (times the pace): dragged ahead, it follows; let go, the stroke ends
## where the tool got to. While it works, it pushes aside loose pieces in its way (it never
## cuts under them). Where a rule stops a plan short (a step ahead, the
## blade meeting the work, a gap too narrow, a chip too thick) a red mark shows where, and the
## line by the pointer says why. Ctrl+wheel sets depths in hundredths of a millimetre.
## Locked or pressed near an edge (a step, or a fold sharper than edge_fold) and aimed within
## edge_aim of its way, a chisel's or gouge's stroke runs along the edge (drawn in blue):
## across an outside corner, cutting it off (a chamfer); otherwise its side flush with it,
## and beside an earlier cut level with its floor. Alt: freely.
## Esc drops the plan or the stroke in progress, Ctrl+Z / Ctrl+Shift+Z undo and redo.
## Middle-drag orbits the camera, Shift+middle-drag pans, the wheel zooms.
##
## Every piece of work is a rigid body holding its SdfBody (the one in the vise frozen). A saw
## cut that goes right through leaves the piece in two: the smaller one comes away as a
## piece of its own and slides off the kerf, the larger stays in the vise. So does a piece
## that cuts meeting leave free (a rebate sawn off the end: one cut down, one in from the
## end): it shows as its own body once it has been cut out, then falls or rests as it will
## on the piece it came from, whose collider follows its surface from then on.
## Undo straight after puts them back together.
##
## What the tools take off comes away too (debris.gd): a chisel's, gouge's or spokeshave's
## shaving curls up off the edge as it goes and drops when it breaks or the stroke ends;
## tear-out and a chop's pop-off throw chips. They lie a moment where they fall, then fade
## away. The saw, the rasps, the scraper and the sanding tools throw puffs of dust that go as
## they land; what reaches the bench or the floor heaps up in a pile there. Undo takes a
## stroke's back, and Sweep clears the piles.
##
## The world is in metres with y up. Bodies are in millimetres with z up: each body node is
## scaled by 0.001 and turned -90 degrees about x.

const MM := 0.001
const HOVER_LIFT := 15.0 # mm the tool floats above where it will engage
const OrbitCamera := preload("res://workshop/orbit_camera.gd")
const Room := preload("res://workshop/room.gd")
const Player := preload("res://workshop/player.gd")
const WorkshopUi := preload("res://workshop/workshop_ui.gd")
const Debris := preload("res://workshop/debris.gd")
const Layout := preload("res://workshop/layout.gd")
const FADE_TIME := 0.1 # s for the tool in hand to fade in when it acts, and out after
const ARM_DISTANCE := 2.0 # mm a direct stroke's drag goes before it shows its direction
const SETTLE_REACH := 0.003 # m below an island it looks for what it rests on (see _settle)
const SETTLE_INTO := 0.00018 # m it starts into that (the solver's slop is 0.2 mm)
const SHAPE_MARGIN := 0.0001 # m: islands' and the hollowed board's shapes, sharp to a tenth of a mm
const PUSH_CLEAR := 0.0003 # m above a pushed piece's shapes its way is checked from (over the 0.2 mm it rests in)
## mm/s a tool goes over the work at most, as a hand works it (times the pace): a push tool
## paring freely (at the force the hand can give, a fifth of it); the others' strokes.
const WORKING_SPEED := {"chisel": 40.0, "gouge": 30.0, "spokeshave": 150.0, "saw": 300.0, "rasp": 250.0,
		"scraper": 200.0, "sanding_block": 300.0, "sanding_sponge": 300.0}
## The tools pushed along a planned path (the others follow the pointer over their line or
## plane).
const PUSHED := ["chisel", "gouge", "spokeshave"]
const BLOW_INTERVAL := 0.35 # s between mallet blows, at the quickest
const EDGE_REACH := 6.0 # mm round where a stroke is locked that edge lock looks for an edge
const FLUSH := 0.05 # mm a tool locked to an edge keeps its side off it
const EDGE_COLOUR := Color(0.35, 0.9, 1.0)
## A chop's blow: a tap, a firm blow, a heavy one (SdfBody's "blow").
const BLOWS := [0.3, 1.0, 1.6]
const BLOW_NAMES := ["tap", "firm", "heavy"]

## IDLE: pointing. PLANNING: a stroke locked in with the right button. ARMED: a direct
## stroke (the left button, no plan) waiting for its drag to show which way it goes.
## ACTING: a stroke being made.
enum { IDLE, PLANNING, ARMED, ACTING }
## WALK: about the room, in first person. WORK: at the bench, over the work in the vise.
enum Mode { WALK, WORK }
const REACH := 2.0 # m: how far away a hand reaches (E)
## A piece carried: this far in front of the eyes (m), nearer or farther by the wheel.
const HOLD := 0.55
const HOLD_NEAREST := 0.35
const HOLD_FARTHEST := 1.2
const LET_GO_SPEED := 1.0 # m/s at most, as it leaves the hands
## Each piece's undo steps number from its own multiple of this (debris.gd's step keys).
const STEPS_PER_PIECE := 100000
## The tool in hand while walking: where it is held in front of the eyes (camera space, m),
## its working direction forward and its face up, turned a little inwards.
const HAND := Vector3(0.2, -0.2, -0.42)
const HAND_TURN := 0.35 # radians

## Per tool, what the wheel sets while planning: [setting, step, lowest, highest, format].
const INTENSITY := {
	"chisel": ["depth", 0.05, 0.01, 1.5, "%.2f mm deep"],
	"gouge": ["depth", 0.05, 0.01, 2.5, "%.2f mm deep"],
	"saw": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"rasp": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"spokeshave": ["depth", 0.02, 0.02, 1.0, "%.2f mm shaving"],
	"scraper": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"sanding_block": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"sanding_sponge": ["pressure", 0.25, 0.25, 3.0, "pressure %.2f"],
	"layout": ["distance", 0.5, 1.0, 60.0, "%.1f mm in"], # (the marking gauge's)
}
## And the angle the guiding hand tilts (a chisel's or gouge's to the work: 60 or more chops;
## a rasp's about its line, its lean). adjust(steps, true) sets it too.
const TILT := {
	"chisel": ["angle", 1.0, 10.0, 90.0, "%.0f° to the work"],
	"gouge": ["angle", 1.0, 10.0, 90.0, "%.0f° to the work"],
	"rasp": ["tilt", 5.0, -60.0, 60.0, "tilted %.0f°"],
}
const CHOP_ANGLE := 60.0
## The guiding hand (right-drag): degrees the tool pivots for a pixel of the pointer's motion
## (Ctrl: a fifth as far), and a notch of the wheel leans it.
const PIVOT_RATE := 0.25
const LEAN_STEP := 2.5
const LEAN_LIMIT := 30.0
## The tools it leans: rolled about the way they go (the saw: a bevelled kerf). The rasp's
## lean is its tilt.
const LEANS := ["chisel", "gouge", "saw", "spokeshave"]
## Seconds the attitude gauge stays by the pointer once the hand lets the tool go.
const ATTITUDE_LINGER := 1.5
## Steering a pushed tool as it goes (a chisel, gouge or plane, made directly): the edge
## follows the pointer, turning towards it once it is further than LAZY (mm) from the edge
## and heads STEER_TURN degrees or more off the edge's way (at most STEER_MOST at a time); and
## where the surface under the edge turns STEER_TURN, the tool is set to it afresh.
const LAZY := 4.0
const STEER_TURN := 3.0
const STEER_MOST := 30.0
## A plan's path (mm) until the pointer is dragged further than this from where it locked.
const DEFAULT_LENGTH := {"chisel": 20.0, "gouge": 20.0, "saw": 60.0, "rasp": 60.0, "spokeshave": 40.0,
		"scraper": 60.0, "sanding_block": 40.0, "sanding_sponge": 40.0}
## A direct stroke's path (mm): as far as the drag goes, up to this.
const DIRECT_LENGTH := {"chisel": 300.0, "gouge": 300.0, "spokeshave": 300.0, "rasp": 150.0, "scraper": 150.0}

const TOOL_NAMES: Array[String] = ["chisel", "gouge", "saw", "rasp", "spokeshave", "scraper", "sanding_block",
		"sanding_sponge", "layout"]
const TOOL_COLOURS := {
	"chisel": Color(1.0, 0.82, 0.25),
	"gouge": Color(1.0, 0.58, 0.2),
	"saw": Color(0.4, 0.85, 1.0),
	"rasp": Color(0.8, 0.8, 0.95),
	"spokeshave": Color(0.45, 0.95, 0.8),
	"scraper": Color(0.95, 0.95, 0.55),
	"sanding_block": Color(0.6, 1.0, 0.45),
	"sanding_sponge": Color(1.0, 0.55, 0.8),
	"layout": Color(1.0, 0.45, 0.3),
}
const SPONGE_REACH := 10.0 # mm round its centre that the sponge bears on (core SandingSponge)

## Per tool: a chisel's or gouge's variant (SdfBody.tool_catalog()), depth (mm, at most),
## angle to the work, skew and lean (degrees); grit and pressure (1: an ordinary hand's worth).
var settings := {
	"chisel": {"variant": "bench_12", "depth": 0.2, "angle": 30.0, "skew": 0.0, "lean": 0.0, "blow": 1.0},
	"gouge": {"variant": "gouge_7_12", "depth": 0.3, "angle": 30.0, "skew": 0.0, "lean": 0.0, "blow": 1.0},
	"saw": {"pressure": 1.0, "lean": 0.0}, # ("feed": a set feed, either way, for tests)
	"rasp": {"variant": "rasp_cabinet", "pressure": 1.0, "tilt": 0.0},
	"spokeshave": {"variant": "spokeshave", "depth": 0.1, "lean": 0.0}, # the Planes slot
	"scraper": {"pressure": 1.0},
	"sanding_block": {"variant": "block", "grit": 120, "pressure": 1.0},
	"sanding_sponge": {"grit": 120, "pressure": 1.0},
	"layout": {"variant": "gauge", "distance": 6.0}, # a marking gauge (mm in from the edge) or a knife
}
var wood := "board" ## the board in the vise at the start (and set_wood's): board, board_oak or board_walnut
## How fast work goes against real life (1: as a real hand would), for every tool's speed and
## rate.
var pace := 1.0
## Edge lock (chisels and gouges): locked within EDGE_REACH of an edge (a step, or a fold
## sharper than `edge_fold` degrees) and aimed within `edge_aim` degrees of its way, a stroke
## runs along it, the tool's side flush with it; started beside an earlier cut, its depth is
## set level with that cut's floor. `edge_aim` 0: off. Alt places a stroke freely.
var edge_aim := 20.0
var edge_fold := 25.0

var board                 # the SdfBody of the piece in the vise (the tools work on it), or null
var tools := {}           # name -> SdfBody
var current := ""         # the tool in hand, or ""
var yaw := 0.0            # the tool's turn about the surface normal, radians
var camera: Camera3D      # the one in use: the player's eyes (walking) or the view over the bench
var mode := Mode.WALK
var room                  # room.gd
var player                # player.gd
## Every piece of work (a RigidBody3D with meta "workpiece", holding its SdfBody as meta "sdf"),
## the one in the vise, and the one carried.
var pieces: Array[RigidBody3D] = []
var clamped: RigidBody3D
var held: RigidBody3D
var _hold := HOLD
var _hold_turn := Basis() # the carried piece's turn, relative to the player's facing
var _next_piece := 1

var _orbit                # the view over the bench (orbit_camera.gd)
var _glide := 1.0         # 0 to 1: how far the view has come down to the bench
var _glide_from := Transform3D()
var _outline: MeshInstance3D
var _ui
var _pointer := Vector2.ZERO
var _hit := {}            # the board under the pointer (SdfBody.raycast)
var _state := IDLE
var _engaged := false     # a stroke is on the board (ACTING)
var _plan_held := false   # Space (or lock()) held: a stroke is planned first, and the next after it
var _pivoting := false    # the right button held: the guiding hand has the tool
var _pivot_from := Vector2.ZERO # where the pointer was when it took it (it comes back there)
var _refit := false       # the tool's model is to be held at its new angle
var _attitude_shown := 0.0 # s the attitude gauge stays by the pointer
var _aim = null           # where the pointer asks a pushed tool to go (world, on its plane), or null
var _trail: Array[Vector3] = [] # the way the pointer has gone, dragging a pushed tool (every 0.5 mm)
var _trail_at := 0        # the first point of it the edge has not yet come to
var _resteer := false     # the hand has changed its hold mid-stroke: go on at the new attitude
var _surface_checked := 0.0 # mm along the segment where the surface under the edge was last looked at
## The stroke being planned or made: {"point", "normal" (world), "plane", "along" (the
## tool's facing), "path" (unit, the direction it works in), "length" (mm), "seed",
## "direct" (made without a plan)}.
var _lock := {}
var _plan := {}           # what SdfBody.plan_stroke made of it
## The chisels and gouges (SdfBody.tool_catalog()): family -> [{id, label, width, ...}].
var variants := {}
var layout # layout.gd: the marking gauge and knife, and the lines they leave
var _progress := 0.0      # mm a push tool has gone along its path
var _target := 0.0        # mm along it the pointer asks for (the tool follows at its working speed)
var _at := Vector3.ZERO   # where another tool is on its line or plane (world)
var _goal := Vector3.ZERO # where the pointer asks it to be (it follows at its working speed)
var _last_blow := -1.0    # s: when the mallet last struck
var _blade := BoxShape3D.new() # round the tool in hand's blade, while it works (_place_blade)
var _blade_at := Transform3D()
var _blade_on := false
var _edge_velocity := Vector3.ZERO # m/s: the tool's, while it works
var _opacity := 0.0       # of the tool in hand, in the main view
var _plane := Plane()
var _engage_pose := Transform3D()
var _engage_time := 0.0
## Pieces sawn off, oldest first: {"body": RigidBody3D, "piece": SdfBody, "from": the piece
## it came off (a RigidBody3D), "steps": that piece's step count once its half-space landed,
## "spawn": where the body started, "island"}.
var offcuts: Array[Dictionary] = []
## An offcut's collider: "box" (its bounds), "hull" (convex, from its surface) or "auto"
## (a box when the piece fills nearly all its bounds, as sawn strips do: a box rests and
## slides more steadily than a hull of many points).
var offcut_collider := "auto"
## What the tools took off (debris.gd), and the undo step the stroke in hand makes (keyed by
## piece: see _step_key).
var debris
var _debris_step := 0


func _ready() -> void:
	room = Room.new()
	add_child(room)
	debris = Debris.new()
	add_child(debris)
	for tool in TOOL_NAMES:
		var body = _new_body()
		_load_tool(body, tool)
		tools[tool] = body
		_show_tool(tool, 0.0) # (out of sight until it is taken from the hotbar)
	for v in tools["chisel"].tool_catalog():
		if not variants.has(v.family):
			variants[v.family] = []
		variants[v.family].append(v)
	variants["layout"] = [{"id": "gauge", "family": "layout", "label": "Marking gauge"},
			{"id": "knife", "family": "layout", "label": "Marking knife and square"}]
	layout = Layout.new()
	layout.workshop = self
	add_child(layout)
	# An ash board in the vise.
	_clamp(_new_piece(wood))

	var orbit = OrbitCamera.new()
	orbit.fov = 40.0
	orbit.near = 0.005
	orbit.far = 20.0
	orbit.target = Vector3(0.0, 0.012, 0.0)
	orbit.distance = 0.42
	orbit.pitch = -0.8
	add_child(orbit)
	_orbit = orbit
	# The person: standing in front of the bench, looking at the vise.
	player = Player.new()
	add_child(player)
	player.position = Vector3(0.0, Room.FLOOR, 0.75)
	player.face(Vector3(0.0, 0.0, 0.0))
	_walk()

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
	select_tool("")


# --- walking and working ---------------------------------------------------------------

## Steps up to the bench: the view comes down over the work in the vise (in `glide` 0.3 s,
## or at once), the mouse is freed, and the tools work on it. False with nothing to work on.
func enter_work(glide := true) -> bool:
	if mode == Mode.WORK:
		return true
	if board == null:
		return false
	mode = Mode.WORK
	player.active = false
	player.move_input = Vector2.ZERO
	_glide_from = player.camera.global_transform
	_glide = 0.0 if glide else 1.0
	_orbit.target = board.global_transform * board.get_body_bounds().get_center()
	_orbit._apply()
	_orbit.set_process_unhandled_input(true)
	_orbit.make_current()
	camera = _orbit
	_orbit_to_glide()
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	_show_tool_in_hand()
	_ui.refresh()
	return true


## Steps back from the bench: anything planned or being made is dropped, and the view is the
## player's eyes again.
func leave_work() -> void:
	if mode == Mode.WALK:
		return
	cancel()
	end_pivot()
	_walk()
	_ui.refresh()


func _walk() -> void:
	mode = Mode.WALK
	_glide = 1.0
	_orbit.set_process_unhandled_input(false)
	player.active = true
	player.camera.make_current()
	camera = player.camera
	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	_show_tool_in_hand()


## E: what is in reach in front of the eyes. Carrying something, let it go; at the bench (or
## the piece in the vise), step up to it; a piece lying about, pick it up.
func interact() -> void:
	if mode != Mode.WALK:
		return
	if held != null:
		let_go()
		return
	var hit := look_at_thing()
	if hit.is_empty():
		return
	var thing: Object = hit.collider
	if thing.has_meta("bench") or (thing == clamped and clamped != null):
		enter_work()
	elif thing.has_meta("workpiece"):
		pick_up(thing)
	elif thing.has_meta("stock"):
		take_stock(thing)


## A new board from a stack on the rack (room.gd), into the hands: off the top of the stack,
## lying as the stack's boards do, and carried from there. The stack stays as it was.
func take_stock(stack: Object) -> RigidBody3D:
	if held != null:
		return null
	var piece := _new_piece(stack.get_meta("stock"))
	# Its length (body x) along the stack's (the rack's, z), just clear of the top board.
	var top: Vector3 = stack.get_meta("top")
	piece.global_transform = Transform3D(Basis(Vector3.UP, PI / 2), top + Vector3(0.0, 0.0125 + 0.002, 0.0))
	pick_up(piece)
	_hold_turn = Basis() # (brought round flat and across the view, as a board is carried)
	return piece


## F: the piece in the vise, if the eyes are on it, taken out into the hands.
func take_out() -> void:
	if mode != Mode.WALK or held != null or clamped == null:
		return
	var hit := look_at_thing()
	if not hit.is_empty() and hit.collider == clamped:
		pick_up(clamped)


## Picks a piece up: it is carried in front of the eyes (turned as it was, relative to the
## way they face). Out of the vise, it is unclamped.
func pick_up(piece: RigidBody3D) -> void:
	if held != null or piece == null:
		return
	if piece == clamped:
		_unclamp()
	held = piece
	held.freeze = false
	held.sleeping = false
	held.gravity_scale = 0.0
	held.can_sleep = false
	_hold = clampf(player.eye().distance_to(held.global_position), HOLD_NEAREST, HOLD)
	_hold_turn = Basis(Vector3.UP, -player.yaw) * held.global_basis.orthonormalized()
	_show_tool_in_hand()


## Lets the carried piece go. With the eyes on the empty vise, it goes in (squared: see
## _put_in_vise); anywhere else it falls where it is (gently: its speed as it leaves the
## hands is at most LET_GO_SPEED).
func let_go() -> void:
	if held == null:
		return
	var piece := held
	var into_vise := clamped == null and at_vise() and not vise_blocked(piece)
	held = null
	piece.gravity_scale = 1.0
	piece.can_sleep = true
	if into_vise:
		_put_in_vise(piece)
	else:
		piece.linear_velocity = piece.linear_velocity.limit_length(LET_GO_SPEED)
		piece.angular_velocity = piece.angular_velocity.limit_length(2.0)
	_show_tool_in_hand()
	_ui.refresh()


## Whether the eyes are on the vise (the bench between its jaws, or the piece in it), in reach.
func at_vise() -> bool:
	var hit := look_at_thing()
	return not hit.is_empty() and (hit.collider == room.bench or (clamped != null and hit.collider == clamped)) \
			and room.in_vise(hit.position)


## Puts a piece in the vise, squared as a vise holds it: the way through it nearest the
## vertical turned exactly up (a board flat, on its edge or on end, as it was held), the
## longer of its other two along the bench (to the nearer end), and set down on the bench
## top between the jaws, its middle at the vise's.
func _put_in_vise(piece: RigidBody3D) -> void:
	var pose := _vise_pose(piece)
	# Shavings and chips lying where it goes fade away (rather than being found inside it).
	for shape in _shapes_of(piece):
		debris.fade_within(shape.shape, pose * shape.transform)
	piece.freeze = true
	piece.linear_velocity = Vector3.ZERO
	piece.angular_velocity = Vector3.ZERO
	piece.global_transform = pose
	_clamp(piece)


## Where a piece goes in the vise (its rigid body's transform), squared (see _put_in_vise).
func _vise_pose(piece: RigidBody3D) -> Transform3D:
	var sdf = piece.get_meta("sdf")
	var inner: Basis = sdf.transform.basis.orthonormalized() # the body's turn in its rigid body
	var axes: Basis = piece.global_basis.orthonormalized() * inner # its x, y, z in the world
	var size: Vector3 = sdf.get_body_bounds().size
	var up := 0
	for i in 3:
		if absf(axes[i].y) > absf(axes[up].y):
			up = i
	var along := (up + 1) % 3
	if size[(up + 2) % 3] > size[along]:
		along = (up + 2) % 3
	var square: Array[Vector3] = [Vector3.ZERO, Vector3.ZERO, Vector3.ZERO]
	square[up] = Vector3.UP * signf(axes[up].y)
	square[along] = Vector3.RIGHT * (1.0 if axes[along].x >= 0.0 else -1.0)
	var third := 3 - up - along
	square[third] = square[(third + 1) % 3].cross(square[(third + 2) % 3])
	var turn := Basis(square[0], square[1], square[2]) * inner.inverse()
	var extent := _extent(sdf, Transform3D(turn, Vector3.ZERO) * sdf.transform)
	var middle := extent.get_center()
	return Transform3D(turn, Vector3(-middle.x, -extent.position.y, -middle.z))


## Whether another loose piece lies where a piece would go in the vise (it would be found
## inside it): then the vise does not take it.
func vise_blocked(piece: RigidBody3D) -> bool:
	var pose := _vise_pose(piece)
	var space := get_world_3d().direct_space_state
	for shape in _shapes_of(piece):
		var query := PhysicsShapeQueryParameters3D.new()
		query.shape = shape.shape
		query.transform = pose * shape.transform
		query.collision_mask = 1
		query.exclude = [piece.get_rid()]
		for hit in space.intersect_shape(query, 8):
			if hit.collider is RigidBody3D and not hit.collider.freeze:
				return true
	return false


## Where a piece's surface reaches (its hull, or failing that its bounds) with its body at
## `placed` (world): an axis-aligned box.
func _extent(sdf, placed: Transform3D) -> AABB:
	var points: PackedVector3Array = sdf.get_hull_points()
	if points.is_empty():
		return placed * sdf.get_body_bounds()
	var box := AABB(placed * points[0], Vector3.ZERO)
	for p in points:
		box = box.expand(placed * p)
	return box


## R: the carried piece turned a quarter about the vertical.
func turn_held() -> void:
	if held != null:
		_hold_turn = Basis(Vector3.UP, PI / 2) * _hold_turn


## The wheel while carrying: nearer (-) or farther.
func reach_held(steps: int) -> void:
	_hold = clampf(_hold + 0.05 * steps, HOLD_NEAREST, HOLD_FARTHEST)


## What the eyes rest on within reach (a physics ray from the middle of the view): the
## ray's result ({"collider", "position", ...}), or {}.
func look_at_thing() -> Dictionary:
	var query := PhysicsRayQueryParameters3D.create(player.eye(), player.eye() + player.forward() * REACH, 1)
	query.exclude = [player.get_rid()] if held == null else [player.get_rid(), held.get_rid()]
	return get_world_3d().direct_space_state.intersect_ray(query)


## What E would do now, for the line under the crosshair ("" for nothing).
func prompt() -> String:
	if mode != Mode.WALK:
		return ""
	if held != null:
		var turning := "   R: turn it   wheel: nearer, farther"
		if at_vise():
			if clamped != null:
				return "E: let go (the vise holds the %s)" % _name_of(clamped) + turning
			if vise_blocked(held):
				return "E: let go (something lies in the vise)" + turning
			return "E: put the %s in the vise" % _name_of(held) + turning
		return "E: let go" + turning
	var hit := look_at_thing()
	if hit.is_empty():
		return ""
	var thing: Object = hit.collider
	if thing == clamped and clamped != null:
		return "E: work on the %s   F: take it out of the vise" % _name_of(thing)
	if thing.has_meta("bench"):
		return "E: work at the bench" if board != null else "put a piece in the vise to work on it"
	if thing.has_meta("workpiece"):
		return "E: pick up the %s" % _name_of(thing)
	if thing.has_meta("stock"):
		var wood_name: String = thing.get_meta("wood_name")
		return "E: take %s %s board" % ["an" if "aeiou".contains(wood_name[0]) else "a", wood_name]
	return ""


## What a piece is called: its wood, and whether it is a board or an offcut.
func _name_of(piece: Object) -> String:
	return "%s %s" % [piece.get_meta("wood_name", "wood"), piece.get_meta("kind", "piece")]


## While the view comes down to the bench: part way from the eyes to the view over the work.
func _orbit_to_glide() -> void:
	if _glide >= 1.0:
		_orbit._apply()
		return
	_orbit._apply()
	var t := smoothstep(0.0, 1.0, _glide)
	_orbit.global_transform = _glide_from.interpolate_with(_orbit.global_transform, t)


## The tool in hand: held in view while walking; out of sight at the bench until it works.
func _show_tool_in_hand() -> void:
	for tool in TOOL_NAMES:
		var shown := (1.0 if mode == Mode.WALK else _opacity) if tool == current and held == null else 0.0
		_show_tool(tool, shown)


# --- driving the tools (input handlers call these; tests do too) ----------------------

func select_tool(tool: String) -> void:
	if _engaged:
		release()
	end_pivot()
	_drop_plan()
	current = tool
	_opacity = 0.0
	_orbit.wheel_zoom = tool != "layout" # (with the layout tool, the wheel sets the gauge)
	_show_tool_in_hand()
	_ui.refresh()


## The next or the one before on the hotbar (the wheel, walking): the tools, then empty hands.
func next_slot(steps: int) -> void:
	var slots: Array = TOOL_NAMES.duplicate()
	slots.append("")
	select_tool(slots[posmod(slots.find(current) + steps, slots.size())])


## Moves the pointer to a screen position and looks at what is under it.
func hover_screen(position: Vector2) -> void:
	_pointer = position
	if current == "layout":
		layout.hover(position)
		return
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
	hit.own_normal = hit.normal # (the surface's own there, for the edge lock)
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


## Space down: locks a stroke in where the pointer is on the board (the tool in hand set on
## the surface there), and plans it towards the pointer.
## `free` (Alt): no edge lock.
func lock(position: Vector2, free := false) -> void:
	_plan_held = true
	if _state != IDLE or current == "" or current == "layout":
		return
	hover_screen(position)
	if _hit.is_empty() or _hit.get("stale", false):
		return
	var normal: Vector3 = _hit.normal
	var facing := _along(normal)
	# The seed makes a plan's chips (tear-out) the same as the stroke's.
	_lock = {"point": _hit.position, "normal": normal, "plane": Plane(normal, _hit.position), "along": facing,
			"path": facing, "length": DEFAULT_LENGTH[current], "seed": randi() % 100000}
	_note_edge(free, _hit.get("own_normal", normal))
	_state = PLANNING
	_orbit.wheel_zoom = false
	aim(position)
	_replan()


## Pointer motion while planning: the path runs from where the stroke was locked towards
## the pointer (once it is a few millimetres away), as far as the pointer.
func aim(position: Vector2) -> void:
	_pointer = position
	if _state != PLANNING:
		return
	var free: Plane = _lock.get("free_plane", _lock.plane)
	var point = free.intersects_ray(camera.project_ray_origin(position), camera.project_ray_normal(position))
	if point == null:
		return
	var n: Vector3 = free.normal
	var d: Vector3 = point - _lock.get("free_point", _lock.point)
	d -= n * d.dot(n)
	if d.length() < 3.0 * MM:
		return
	_snap(d.normalized())
	if not _lock.snapped:
		_lock.path = d.normalized()
	# Edge tools and the saw face the way they work; the sanding tools can be turned (Q / E).
	_lock.along = _lock.path if _faces_its_path() else _lock.path.rotated(_lock.normal, yaw)
	var reach: Vector3 = point - _lock.point
	_lock.length = clampf(reach.dot(_lock.path) / MM if _lock.snapped else d.length() / MM, 3.0, 400.0)
	_replan()


## Looks for an edge near where the stroke was locked (a chisel's or gouge's, not `free`;
## `surface`: the surface's own normal there, not the plane fitted round it, which an edge
## nearby tilts), and keeps where it was locked: _snap() places the stroke from there.
func _note_edge(free: bool, surface: Vector3) -> void:
	_lock.free_point = _lock.point
	_lock.free_normal = _lock.normal
	_lock.free_plane = _lock.plane
	_lock.surface_normal = surface
	_lock.snapped = false
	_lock.corner = false
	_lock.edge = {}
	_lock.free = free # (Alt: no edge lock, and not held to the marked lines)
	if _edge_locks():
		_lock.edge = board.find_edge(_lock.point, surface, EDGE_REACH, edge_fold)


## Whether the stroke being locked looks for an edge: a chisel's or gouge's (not chopping),
## or a plane's with a chamfer fence; not made freely (Alt), and edge lock not off.
func _edge_locks() -> bool:
	return not _lock.get("free", false) and edge_aim > 0.0 and not is_chopping() and \
			(current == "chisel" or current == "gouge" or _fenced())


## The stroke asks to go `asked` (unit, in the plane it was locked on). Within `edge_aim` of
## the way of the edge it was locked near, it runs along that edge instead:
##   - an outside corner (the far side falls away, no floor beyond: an arris): across it,
##     the tool's face bisecting the two faces, cutting the corner off (a chamfer);
##   - otherwise (a wall, an earlier cut's edge, a fold turning up): the tool's side FLUSH
##     off it; started on the higher side of an earlier cut, its depth is set (once) level
##     with that cut's floor.
## Otherwise it starts where it was locked.
func _snap(asked: Vector3) -> void:
	_lock.point = _lock.get("free_point", _lock.point)
	_lock.normal = _lock.get("free_normal", _lock.normal)
	_lock.plane = _lock.get("free_plane", _lock.plane)
	_lock.snapped = false
	_lock.corner = false
	var edge: Dictionary = _lock.get("edge", {})
	var n: Vector3 = _lock.normal
	if not _edge_along(edge, asked) and _fenced() and _edge_locks() and _lock.has("surface_normal"):
		# A chamfer fence pressed by the end of an arris (nearer the end's edge, across the
		# way: a plane is started at the end): the arris a little way ahead along the drag.
		# (A chisel pressed there pares flat: it would be across the corner.)
		var ahead: Dictionary = board.find_edge(_lock.point + asked * (EDGE_REACH * MM), _lock.surface_normal,
				EDGE_REACH, edge_fold)
		if _edge_along(ahead, asked):
			edge = ahead
			_lock.edge = ahead
	if not _edge_along(edge, asked):
		return
	var way: Vector3 = edge.direction - n * n.dot(edge.direction)
	way = way.normalized()
	if way.dot(asked) < 0.0:
		way = -way
	# Abreast of where it was pressed.
	var abreast: Vector3 = edge.point + edge.direction * edge.direction.dot(_lock.point - edge.point)
	var far: Vector3 = edge.get("far_normal", Vector3.ZERO)
	if edge.convex and not edge.floor and far != Vector3.ZERO:
		# Across the corner: onto it along the bisector of its two faces (a chamfer fence's:
		# of the piece's faces there, whatever fold of a chamfer begun the lock found).
		var face: Vector3 = _lock.get("surface_normal", n)
		var bisector := _fence_normal(face, far) if _fenced() else (face + far).normalized()
		var on: Dictionary = board.raycast(abreast + bisector * 0.005, -bisector, 0.01)
		if on.is_empty() or on.get("stale", false):
			return
		_lock.point = on.position
		_lock.normal = bisector
		_lock.plane = Plane(bisector, on.position)
		_lock.path = (way - bisector * bisector.dot(way)).normalized()
		_lock.snapped = true
		_lock.corner = true
		return
	if _fenced():
		return # (a chamfer fence rides an arris, not a wall)
	var width: float = variant().get("width", 12.0) * MM
	var start: Vector3 = abreast + edge.across * (0.5 * width + FLUSH * MM)
	var hit: Dictionary = board.raycast(start + n * 0.01, -n, 0.03)
	if hit.is_empty() or hit.get("stale", false):
		return
	# On the surface beside the edge, as it is there.
	n = hit.normal
	_lock.point = hit.position
	_lock.normal = n
	_lock.plane = Plane(n, hit.position)
	_lock.path = (way - n * n.dot(way)).normalized()
	_lock.snapped = true
	if edge.floor and edge.step < 0.0 and not _lock.has("level"):
		# Beside an earlier cut, on its higher side: level with its floor (the wheel adjusts it).
		var spec: Array = INTENSITY[current]
		_lock.level = -edge.step
		settings[current].depth = clampf(snappedf(-edge.step, 0.01), spec[2], spec[3])


## Whether an edge (SdfBody.find_edge's) runs within edge_aim of the way `asked`.
func _edge_along(edge: Dictionary, asked: Vector3) -> bool:
	if edge.is_empty():
		return false
	var n: Vector3 = _lock.normal
	var way: Vector3 = edge.direction - n * n.dot(edge.direction)
	if way.length() < 1e-4:
		return false
	return rad_to_deg(acos(clampf(absf(way.normalized().dot(asked)), 0.0, 1.0))) <= edge_aim


## Whether the tool in hand is a plane with a chamfer fence.
func _fenced() -> bool:
	return current == "spokeshave" and variant().get("fence", false)


## A chamfer fence's hold across an arris: the bisector of the piece's two faces there (the
## piece's own axes nearest the faces the edge lock found, `face` and `far`), so that on a
## chamfer begun, whose folds are what the lock finds, it is still held at 45 degrees to the
## faces it rides.
func _fence_normal(face: Vector3, far: Vector3) -> Vector3:
	var basis: Basis = board.global_basis.orthonormalized()
	var sum: Vector3 = basis.inverse() * (face + far)
	var order := [0, 1, 2]
	order.sort_custom(func(i, j): return absf(sum[i]) > absf(sum[j]))
	var bisector := Vector3.ZERO
	for k in 2:
		bisector[order[k]] = signf(sum[order[k]])
	return (basis * bisector).normalized()


## Space up: a plan not acted on is dropped; a stroke in progress carries on.
func unlock() -> void:
	_plan_held = false
	if _state == PLANNING:
		_drop_plan()


# --- the guiding hand ----------------------------------------------------------------------

## Right button down: the guiding hand takes the tool in hand. The pointer stays where it is
## (the mouse is held), and its motion pivots the tool on its edge (pivot()); the wheel leans
## it (lean()). Hovering or planning (the plan follows live); not yet mid-stroke.
func begin_pivot() -> void:
	if current == "" or current == "layout" or (_engaged and not _steerable()) or mode != Mode.WORK:
		return
	_pivoting = true
	_pivot_from = _pointer
	_attitude_shown = ATTITUDE_LINGER
	_orbit.wheel_zoom = false # (the wheel leans it)
	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


## Right button up: the hand lets go, and the pointer is back where it was, over the edge.
func end_pivot() -> void:
	if not _pivoting:
		return
	_pivoting = false
	_attitude_shown = ATTITUDE_LINGER
	_orbit.wheel_zoom = _state != PLANNING and current != "layout"
	_refit_tool()
	if DisplayServer.get_name() != "headless":
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
		Input.warp_mouse(_pivot_from)


func is_pivoting() -> bool:
	return _pivoting


## Where the pointer is on the view (while the guiding hand has the tool, where it was).
func pointer_at() -> Vector2:
	return _pivot_from if _pivoting else get_viewport().get_mouse_position()


## The guiding hand moves `relative` pixels. Up and down raise and lower the handle: a
## chisel's or gouge's angle to the work (paring stays below a chop, a chop above it: C goes
## between them). Left and right skew its edge across the push, or turn another tool about
## the surface. `fine` (Ctrl): a fifth as far.
func pivot(relative: Vector2, fine := false) -> void:
	if current == "" or current == "layout" or (_engaged and not _steerable()):
		return
	var rate := PIVOT_RATE / (5.0 if fine else 1.0)
	_attitude_shown = ATTITUDE_LINGER
	if current == "chisel" or current == "gouge":
		var s: Dictionary = settings[current]
		var lo := CHOP_ANGLE if is_chopping() else float(TILT[current][2])
		var hi := float(TILT[current][3]) if is_chopping() else CHOP_ANGLE - 1.0
		var angle := clampf(s.angle - relative.y * rate, lo, hi)
		if angle != s.angle:
			s.angle = angle
			_refit = true # (its model is held at its angle: set once the hand lets go)
		s.skew = clampf(s.skew + relative.x * rate, -45.0, 45.0)
	else:
		yaw -= deg_to_rad(relative.x * rate)
		if _state == PLANNING and not _faces_its_path():
			_lock.along = _lock.path.rotated(_lock.normal, yaw)
	_held_changed()


## The wheel while the guiding hand has the tool: `steps` notches of lean, the tool rolled
## about the way it goes (a chisel's corner lower, a gouge rolled, a saw's kerf bevelled; a
## rasp's tilt). `fine` (Ctrl): a fifth of a notch.
func lean(steps: int, fine := false) -> void:
	if _engaged and not _steerable():
		return
	if current == "rasp":
		var spec: Array = TILT.rasp
		var step: float = spec[1] / 5.0 if fine else spec[1]
		settings.rasp.tilt = clampf(settings.rasp.tilt + step * steps, spec[2], spec[3])
	elif current in LEANS:
		var step := LEAN_STEP / (5.0 if fine else 1.0)
		settings[current].lean = clampf(settings[current].lean + step * steps, -LEAN_LIMIT, LEAN_LIMIT)
	else:
		return
	_attitude_shown = ATTITUDE_LINGER
	_held_changed()


## The tool's attitude, for the gauge by the pointer: {"tool", "turn" (degrees about the
## surface), "lean"}; a chisel's or gouge's also "angle" (to the work), "skew", "bevel", and
## "bite": how its bevel meets the work mid-face (core tools/cutting): "rides" (it skates,
## not tipped 2 degrees past its bevel), "bites" (it dives at the difference), "digs in" (more
## than 8 degrees past), or "chops".
func attitude() -> Dictionary:
	var s: Dictionary = settings.get(current, {})
	var a := {"tool": current, "turn": rad_to_deg(yaw),
			"lean": s.get("tilt", 0.0) if current == "rasp" else s.get("lean", 0.0)}
	if current == "chisel" or current == "gouge":
		var bevel: float = variant().get("bevel", 25.0)
		a.angle = s.angle
		a.skew = s.skew
		a.bevel = bevel
		if is_chopping():
			a.bite = "chops"
		elif s.angle < bevel + 2.0:
			a.bite = "rides"
		elif s.angle <= bevel + 8.0:
			a.bite = "bites"
		else:
			a.bite = "digs in"
	return a


## Whether the attitude gauge shows by the pointer (the hand has the tool, or just had it).
func attitude_shown() -> bool:
	return _pivoting or _attitude_shown > 0.0


## The hand has moved the tool: a plan follows it; a stroke being made goes on from where its
## edge is, held so (_steer()); and the panel.
func _held_changed() -> void:
	if _state == PLANNING:
		_replan()
		return
	if _engaged:
		_resteer = true
	if _ui != null:
		_ui.refresh()


## The tool's model held at its angle, once the hand has changed it.
func _refit_tool() -> void:
	if not _refit or current == "":
		return
	_refit = false
	_load_tool(tools[current], current)
	_show_tool(current, _opacity)


## Whether the stroke being made can be steered as it goes: a chisel's, gouge's or plane's,
## not a chop.
func _steerable() -> bool:
	return _engaged and current in PUSHED and not is_chopping()


## Steers the pushed tool's stroke (SdfBody.steer_stroke) from where its edge is:
##   - the hand changed its hold (the guiding hand, mid-stroke): at the new attitude, the way
##     it was going (the bevel then steers its depth: tipped past it, it dives; on it, level;
##     lowered under it, it lifts out);
##   - a stroke made directly, not held to an edge or to lines: along the way the pointer
##     went, towards the first point of its trail LAZY or more ahead of the edge, where that
##     heads STEER_TURN or more off its way (turned at most STEER_MOST at a time): a curving
##     drag cuts that curve, however far ahead the pointer runs;
##   - made so, the surface under the edge has turned STEER_TURN (looked at each millimetre):
##     set to it afresh, so its angle to the work holds over a curve or a slope.
## At least half a millimetre on from the last segment.
func _steer() -> void:
	if not _steerable() or _progress < 0.5:
		return
	if _plan.get("stop", "") != "" and _progress >= _plan.get("length", INF) - 0.01:
		return # (stopped short, or lifted out: the edge goes no further)
	var edge: Vector3 = _lock.point + _lock.path * (_progress * MM)
	var way: Vector3 = _lock.path
	var normal: Vector3 = _lock.normal
	var why := _resteer
	# (Free hand: not held along an edge or to lines, nor to a plan it was shown.)
	var free_hand: bool = _aim != null and _lock.get("direct", false) and not _lock.get("snapped", false) \
			and _lock.get("limits", {}).is_empty()
	if free_hand:
		# Along the way the pointer went, not straight at where it is now: towards the first
		# point of its trail at least LAZY ahead of the edge.
		while _trail_at < _trail.size():
			var q: Vector3 = _trail[_trail_at] - edge
			q -= normal * q.dot(normal)
			if q.length() >= LAZY * MM and q.dot(way) > 0.0:
				break
			_trail_at += 1
		if _trail_at < _trail.size():
			var d: Vector3 = _trail[_trail_at] - edge
			d -= normal * d.dot(normal)
			var off := way.angle_to(d)
			if off >= deg_to_rad(STEER_TURN) and off < PI / 2:
				way = way.slerp(d.normalized(), minf(1.0, deg_to_rad(STEER_MOST) / off)).normalized()
				why = true
	if free_hand and _progress - _surface_checked >= 1.0:
		_surface_checked = _progress
		var under := _normal_at(edge, normal)
		if under.angle_to(normal) >= deg_to_rad(STEER_TURN):
			normal = under
			why = true
	if not why:
		return
	way = (way - normal * way.dot(normal)).normalized()
	var s := _stroke_settings()
	s["length"] = _lock.length
	if _resteer and (current == "chisel" or current == "gouge"):
		# Held another way by the hand, its bevel steers how deep it goes, as deep as a hand
		# can push it (the depth set is where a stroke begun mid-face levels).
		s["depth"] = INTENSITY[current][3]
	var got: Dictionary = board.steer_stroke(_leaned(normal, way), way, s)
	_resteer = false
	if got.is_empty():
		return
	_refit_tool()
	_lock.point = got.start
	_lock.normal = normal
	_lock.path = got.path
	_lock.along = _lock.path
	_plane = Plane(normal, got.start)
	_lock.plane = _plane
	_progress = 0.0
	_surface_checked = 0.0
	_target = 0.0 if _aim == null else clampf((_aim - _lock.point).dot(_lock.path) / MM, 0.0, _lock.length)
	_plan = board.get_plan()


## The surface's normal at the work over `at` (world; `n` roughly out of it): a plane through
## hits 4 mm either side, so a kerf or a scribed line there does not tip it. `n` where they
## miss.
func _normal_at(at: Vector3, n: Vector3) -> Vector3:
	var t := n.cross(Vector3.RIGHT if absf(n.x) < 0.9 else Vector3.UP).normalized()
	var b := n.cross(t)
	var hits: Array[Vector3] = []
	for o in [t, -t, b, -b]:
		var h: Dictionary = board.raycast(at + o * 0.004 + n * 0.005, -n, 0.02)
		if h.is_empty() or h.get("stale", false):
			return n
		hits.append(h.position)
	var m := (hits[0] - hits[1]).cross(hits[2] - hits[3])
	if m.length() < 1e-12:
		return n
	m = m.normalized()
	return m if m.dot(n) > 0.0 else -m


## A normal leaned by the guiding hand: rolled about `way` (the way the tool goes) by the
## tool in hand's lean.
func _leaned(normal: Vector3, way: Vector3) -> Vector3:
	var by: float = settings.get(current, {}).get("lean", 0.0)
	if absf(by) < 1e-3 or way.length() < 1e-6 or _fenced(): # (a fence holds the angle)
		return normal
	return normal.rotated(way.normalized(), deg_to_rad(by))


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


## Left button down: with a stroke planned, the tool sets to work along it. Otherwise the
## tool in hand sets to work where the pointer is on the board, without a plan: the sanding
## tools (and a chop) at once, the others once the drag shows which way they go.
func press(position: Vector2, free := false) -> void:
	if current == "layout":
		layout.press(position)
		return
	if _state == PLANNING:
		act()
		return
	if _state != IDLE:
		return
	hover_screen(position)
	if current == "" or _hit.is_empty() or _hit.get("stale", false):
		return
	var normal: Vector3 = _hit.normal
	var facing := _along(normal)
	_lock = {"point": _hit.position, "normal": normal, "plane": Plane(normal, _hit.position), "along": facing,
			"path": facing, "length": DIRECT_LENGTH.get(current, DEFAULT_LENGTH[current]),
			"seed": randi() % 100000, "direct": true}
	_note_edge(free, _hit.get("own_normal", normal))
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
	_lock.limits = layout.hold(_lock, current) # (the lines marked on the work: may set the lock)
	_plane = _lock.plane
	_progress = 0.0
	_target = 0.0
	_at = _lock.point
	_goal = _lock.point
	_refit_tool()
	_engaged = board.begin_stroke(current, _lock.point, _leaned(_lock.normal, _lock.path), _lock.along,
			_stroke_settings())
	if not _engaged:
		if _state == ARMED:
			_drop_plan()
		return
	_state = ACTING
	_aim = null
	_trail.clear()
	_trail_at = 0
	_resteer = false
	_surface_checked = 0.0
	_debris_step = _step_key(board.get_stats().get("steps", 0) + 1)
	layout.forget_redo(clamped)
	if _lock.get("direct", false):
		# What the stroke comes to, for the line by the pointer (a chisel's, gouge's or
		# spokeshave's: the plan it was made from; nothing for the others).
		_plan = board.get_plan()
	if is_chopping():
		_last_blow = now
		board.move_stroke(_lock.point)
		release()


## Pointer motion while acting: a push tool (the chisel) goes on along its path as far as
## the pointer, never back and never past the plan's end; the saw, rasp and scraper slide
## along their line; the sanding tools follow the pointer over the plane they were set on.
## Each follows at its working speed (_process).
func drag_screen(position: Vector2) -> void:
	_pointer = position
	if current == "layout":
		layout.drag(position)
		return
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
		_snap(d.normalized()) # (along an edge it was pressed near, if the drag goes its way)
		if not _lock.snapped:
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
			# Where the pointer asks it to be: _process takes it there at its working speed
			# (and steers it there, _steer()).
			_aim = point
			if _trail.is_empty() or point.distance_to(_trail.back()) > 0.5 * MM:
				_trail.append(point)
			_target = clampf((point - start).dot(path) / MM, _target, _lock.length)
		"saw", "rasp", "scraper":
			_goal = start + path * (point - start).dot(path)
		_:
			_goal = point


## Left button up: the cut is finished and becomes one undo step. With the right button
## still held, the next pass is planned from the same spot.
func release() -> void:
	if current == "layout":
		layout.release()
		return
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
	if _plan_held and not _lock.get("direct", false):
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
	if not _lock.get("limits", {}).is_empty():
		s["limits"] = _lock.limits
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
	_lock.limits = layout.hold(_lock, current) # (the lines marked on the work: may set the lock)
	_plan = board.plan_stroke(current, _lock.point, _leaned(_lock.normal, _lock.path), _lock.along, _lock.length,
			_stroke_settings())
	board.set_plan_tint(Color(TOOL_COLOURS[current], 0.55))
	_ui.refresh()


func _drop_plan() -> void:
	if _state == PLANNING or _state == ARMED:
		_state = IDLE
	_lock = {}
	_plan = {}
	if layout != null:
		layout.held = []
	if board != null:
		board.clear_plan()
	_orbit.wheel_zoom = current != "layout" # (with the layout tool, the wheel sets the gauge)


## A tool's visibility: `opacity` 0 hides it (the tool in hand, until it works).
func _show_tool(tool: String, opacity: float) -> void:
	var body = tools[tool]
	body.opacity = opacity
	body.visible = opacity > 0.0
	# Its shadow would give it away (and it has none of its own while it fades).
	body.live_shadows = opacity >= 1.0


func undo() -> void:
	cancel()
	if board == null:
		return
	board.flush()
	var last := -1
	for i in offcuts.size():
		if offcuts[i].from == clamped:
			last = i
	if last >= 0 and board.get_stats().get("steps", 0) == offcuts[last].steps and offcuts[last].body != held:
		# Straight after a split: the pieces go back together.
		var offcut: Dictionary = offcuts[last]
		offcuts.remove_at(last)
		pieces.erase(offcut.body)
		offcut.body.queue_free()
		board.rejoin()
		board.flush()
		_refresh_collider(clamped)
		_ui.refresh()
		return
	# What the stroke took off goes back (this piece's, from that step on), and the lines it
	# scribed.
	debris.undo_step(_step_key(board.get_stats().get("steps", 0)), _step_key(STEPS_PER_PIECE))
	layout.undo_step(clamped, board.get_stats().get("steps", 0))
	board.undo()


func redo() -> void:
	cancel()
	if board != null:
		board.redo()
		board.flush()
		layout.redo_step(clamped, board.get_stats().get("steps", 0))


## A fresh board of `choice` (board, board_oak, board_walnut) in the vise, and every other
## piece, offcut and bit of debris cleared away.
func set_wood(choice: String) -> void:
	cancel()
	let_go()
	for piece in pieces:
		piece.queue_free()
	pieces.clear()
	offcuts.clear()
	clamped = null
	board = null
	debris.clear()
	wood = choice
	_clamp(_new_piece(wood))
	if mode == Mode.WORK:
		_orbit.target = board.global_transform * board.get_body_bounds().get_center()
		_orbit._apply()
	_ui.refresh()


## The piece in the vise came apart across a plane (world space), turned so that the
## smaller side is in front: that side becomes an offcut, a piece of its own (a rigid body
## with a convex hull), nudged away from the kerf. The piece measured both sides on its
## worker, so this reads nothing from it that waits: the half-spaces land on the pieces'
## workers over the next frames.
func _on_separated(point: Vector3, normal: Vector3, from: RigidBody3D) -> void:
	var sdf = from.get_meta("sdf")
	var split = sdf.split(point, normal)
	if split == null:
		return
	# An island (no plane: cuts meeting left it) is hidden until it has taken in its region;
	# its rigid body waits frozen till then.
	var island := normal == Vector3.ZERO
	# The rigid body sits at the piece's centre of mass (Godot's own follows shape origins).
	var centre: Vector3 = sdf.global_transform * split.get_centre_of_mass()
	var body := _as_piece(split, Transform3D(Basis.IDENTITY, centre), sdf.global_transform,
			from.get_meta("wood_name", "wood"), "offcut")
	if island:
		# An island rests in its piece's hollows, on convex pieces: both sharp (Godot's default
		# margin, 4 cm, rounds millimetre shapes away).
		for shape in _shapes_of(body):
			shape.shape.margin = SHAPE_MARGIN
	# As the last saw stroke would: a nudge off the kerf (about 6 mm of slide).
	body.linear_velocity = normal * 0.25
	body.freeze = island and not split.visible
	# The piece's step count once its half-space lands (undo then rejoins the pieces).
	offcuts.append({"body": body, "piece": split, "from": from, "steps": sdf.get_stats().get("steps", 0) + 1,
			"spawn": body.global_transform, "island": island})
	if not body.freeze:
		# Its piece's collider no longer covers it (hollowed where an island came out), before the
		# piece's edit lands: else the offcut starts inside it and is thrown clear.
		_refresh_collider(from)
	_ui.refresh()


## Lowers a piece about to be let go onto what it rests on (within 3 mm below), and says
## whether there was anything: just into it, within the solver's slop. Let go a kerf's
## width above a surface, a body's first physics step has no contact yet and it falls
## 2.7 mm into it (and out of the far side of a thin one); touching, the contact holds.
func _settle(body: RigidBody3D) -> bool:
	var collider: CollisionShape3D = _shapes_of(body)[0]
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


## What the stroke took off since the last frame (with the edge where the tool is now).
func _take_debris(edge: Transform3D) -> void:
	if board == null:
		return
	var report: Dictionary = board.take_debris()
	if report.is_empty() and debris.live_samples() == 0:
		return
	debris.feed(report, _debris_step, edge)


## A piece's collider, as it is now (after its edits): a box while it fills nearly all its
## bounds (a board with cuts in it: pieces and debris rest on it steadily, where a hull of
## its surface has points bunched within a millimetre at its eased corners, which the
## physics engine lets pieces sink into), else its hull. Where islands came out of it, it
## is hollowed (a rebate, a notch): convex pieces round where they were, not one hull that
## would fill the hollows they sit in.
func _refresh_collider(piece: RigidBody3D) -> void:
	if piece == null:
		return
	var sdf = piece.get_meta("sdf")
	var holes := []
	for offcut in offcuts:
		if offcut.from == piece and offcut.island:
			# The seams between pieces fall a little outside each island (less than half a kerf),
			# so that it does not settle on one and catch on its neighbour's side.
			holes.append(offcut.piece.get_body_bounds().grow(0.3))
	if holes.is_empty():
		var collider := _collider_for(sdf)
		collider.shape.margin = SHAPE_MARGIN
		if _same_collider(piece, collider):
			# (Left as it is: what rests on it keeps its contacts, rather than being jolted by a
			# new shape after every stroke.)
			collider.free()
			return
		_clear_shapes(piece)
		piece.add_child(collider)
		return
	_clear_shapes(piece)
	for points in sdf.get_collision_hulls(holes):
		var hull := ConvexPolygonShape3D.new()
		var placed := PackedVector3Array()
		for p in points:
			placed.push_back(sdf.transform * p)
		hull.points = placed
		hull.margin = SHAPE_MARGIN
		var shape := CollisionShape3D.new()
		shape.shape = hull
		piece.add_child(shape)


## Whether a piece's collider is already `collider` (one box, the same size and place).
func _same_collider(piece: RigidBody3D, collider: CollisionShape3D) -> bool:
	var shapes := _shapes_of(piece)
	if shapes.size() != 1 or not (shapes[0].shape is BoxShape3D) or not (collider.shape is BoxShape3D):
		return false
	return shapes[0].shape.size.is_equal_approx(collider.shape.size) and \
			shapes[0].position.is_equal_approx(collider.position)


func _clear_shapes(piece: RigidBody3D) -> void:
	for shape in _shapes_of(piece):
		piece.remove_child(shape)
		shape.queue_free()


func _shapes_of(piece: RigidBody3D) -> Array:
	return piece.get_children().filter(func(c): return c is CollisionShape3D)


## A new piece of work: a board of `choice` (board, board_oak or board_walnut), loose.
func _new_piece(choice: String) -> RigidBody3D:
	var sdf = ClassDB.instantiate("SdfBody")
	sdf.load_demo(choice)
	# Body millimetres, z up -> world metres, y up; the board's middle at the body's origin.
	var local := Transform3D(Basis(Vector3.RIGHT, -PI / 2) * Basis.from_scale(Vector3.ONE * MM), Vector3.ZERO)
	return _as_piece(sdf, Transform3D(Basis.IDENTITY, Vector3(0.0, 0.0125, 0.0)),
			Transform3D(Basis.IDENTITY, Vector3(0.0, 0.0125, 0.0)) * local, Room.STOCK.get(choice, ["wood"])[0], "board")


## An SdfBody made a piece of work: a rigid body at `at` (world) holding it where `placed`
## puts it (world), with its collider, mass and friction; its edits keep its collider and the
## panel up to date, and a saw through it splits it.
func _as_piece(sdf, at: Transform3D, placed: Transform3D, wood_name: String, kind: String) -> RigidBody3D:
	var body := RigidBody3D.new()
	body.set_meta("workpiece", true)
	body.set_meta("sdf", sdf)
	body.set_meta("wood_name", wood_name)
	body.set_meta("kind", kind)
	body.collision_layer = 1
	body.collision_mask = 1
	# Let go at a height or knocked off the bench, a piece moves further in a physics step than
	# the bench top is thick: swept, it lands on it rather than ending up inside.
	body.continuous_cd = true
	add_child(body)
	body.global_transform = at
	if sdf.get_parent() != null:
		sdf.get_parent().remove_child(sdf)
	body.add_child(sdf)
	sdf.transform = at.affine_inverse() * placed
	sdf.set_meta("piece_id", _next_piece)
	_next_piece += 1
	body.mass = maxf(sdf.get_mass(), 0.005)
	# Sanded wood on a bench top. Below the width-to-height ratio of a sawn strip, so it
	# slides off the kerf rather than toppling over.
	var surface := PhysicsMaterial.new()
	surface.friction = 0.5
	body.physics_material_override = surface
	_refresh_collider(body)
	sdf.edited.connect(func(_stats):
		_refresh_collider(body)
		_ui.refresh())
	sdf.separated.connect(_on_separated.bind(body), CONNECT_DEFERRED)
	pieces.append(body)
	return body


## Puts a piece in the vise: held still where it is, the one the tools work on; the jaws
## close on it.
func _clamp(piece: RigidBody3D) -> void:
	clamped = piece
	board = piece.get_meta("sdf")
	piece.freeze = true
	room.close_jaws(_extent(board, board.global_transform).size.z)


## Takes the piece out of the vise (it is loose again; its edits go with it).
func _unclamp() -> void:
	if clamped == null:
		return
	if mode == Mode.WORK:
		leave_work()
	cancel()
	clamped.freeze = false
	clamped = null
	board = null
	room.close_jaws(0.0)


## The undo step `step` of the piece in the vise, as debris keys it: each piece's steps in a
## range of their own.
func _step_key(step: int) -> int:
	return board.get_meta("piece_id", 0) * STEPS_PER_PIECE + step


## Loads a tool's model into `body` (the Layout slot's: its gauge, set, or its knife).
func _load_tool(body, tool: String) -> void:
	if tool == "layout":
		body.load_tool("marking_gauge" if settings.layout.variant == "gauge" else "marking_knife", settings.layout)
	else:
		body.load_tool(tool, settings[tool])


func set_setting(tool: String, key: String, value) -> void:
	settings[tool][key] = value
	# A chisel's or gouge's model is its variant, held at its angle; a sanding block's, its
	# block or pad; the others' look does not change.
	if ((tool == "chisel" or tool == "gouge") and (key == "variant" or key == "angle")) or \
			((tool == "sanding_block" or tool == "spokeshave") and key == "variant") or tool == "layout":
		_load_tool(tools[tool], tool)
		if tool == current:
			_show_tool(tool, _opacity)
	if _state == PLANNING and tool == current:
		_replan()


func is_engaged() -> bool:
	return _engaged


## Whether the tool in hand is still catching up with where it was dragged.
func lagging() -> bool:
	return _engaged and (_progress < _target - 1e-3 or _at.distance_to(_goal) > 1e-7)


## mm/s the tool in hand goes at most: freely, its WORKING_SPEED; a push tool slower as the
## force the cut takes nears what the hand can give (a fifth of it there); times the pace.
func working_speed() -> float:
	var free: float = WORKING_SPEED.get(current, 40.0)
	var effort := clampf(_plan.get("force", 0.0) / maxf(_plan.get("available", 1.0), 1.0), 0.0, 1.0)
	return free * lerpf(1.0, 0.2, effort) * pace


## What the stroke being made has come to (SdfBody.get_stroke_state): {"depth", "contact",
## "limit"}, or {} with none.
func stroke_state() -> Dictionary:
	return board.get_stroke_state() if _engaged else {}


# --- per frame ---------------------------------------------------------------------------

func _unhandled_input(event: InputEvent) -> void:
	# The hotbar, walking or working: 1 to 9 the tools, 0 empty hands.
	if event is InputEventKey and event.pressed and not event.echo:
		var code := (event as InputEventKey).keycode
		if code >= KEY_1 and code <= KEY_9:
			select_tool(TOOL_NAMES[code - KEY_1])
			return
		if code == KEY_0:
			select_tool("")
			return
	if mode == Mode.WALK:
		_walk_input(event)
		return
	if event is InputEventMouseMotion:
		if _pivoting:
			pivot((event as InputEventMouseMotion).relative, (event as InputEventMouseMotion).ctrl_pressed)
			return
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
					press(button.position, button.alt_pressed)
				else:
					release()
			MOUSE_BUTTON_RIGHT:
				if button.pressed:
					begin_pivot()
				else:
					end_pivot()
			MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN:
				var notch := 1 if button.button_index == MOUSE_BUTTON_WHEEL_UP else -1
				if not button.pressed:
					pass
				elif _pivoting:
					lean(notch, button.ctrl_pressed)
				elif current == "layout":
					layout.adjust(notch, button.ctrl_pressed)
				elif _state == PLANNING:
					adjust(notch, false, button.ctrl_pressed)
	elif event is InputEventKey and not event.pressed and (event as InputEventKey).keycode == KEY_SPACE:
		unlock()
	elif event is InputEventKey and event.pressed and not event.echo:
		var key := event as InputEventKey
		match key.keycode:
			KEY_SPACE:
				# Held: the stroke is planned from where the pointer is, and made by a left-drag.
				lock(_pointer, key.alt_pressed)
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
				# What is planned or being made first; with nothing, a step back from the bench.
				if _state == IDLE and not _engaged:
					leave_work()
				else:
					cancel()
			KEY_Z:
				if key.ctrl_pressed and key.shift_pressed:
					redo()
				elif key.ctrl_pressed:
					undo()
			KEY_Y:
				if key.ctrl_pressed:
					redo()


## Walking: E reaches for what is in front, the wheel steps along the hotbar, Esc frees the
## mouse (a click takes it back). The player takes the walking keys and the mouse's look.
func _walk_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		match (event as InputEventKey).keycode:
			KEY_E:
				interact()
			KEY_F:
				take_out()
			KEY_R:
				turn_held()
			KEY_ESCAPE:
				Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	elif event is InputEventMouseButton and event.pressed:
		match (event as InputEventMouseButton).button_index:
			MOUSE_BUTTON_WHEEL_UP:
				if held != null:
					reach_held(1)
				else:
					next_slot(-1)
			MOUSE_BUTTON_WHEEL_DOWN:
				if held != null:
					reach_held(-1)
				else:
					next_slot(1)
			MOUSE_BUTTON_LEFT:
				if DisplayServer.get_name() != "headless":
					Input.mouse_mode = Input.MOUSE_MODE_CAPTURED


func _process(delta: float) -> void:
	if not _pivoting:
		_attitude_shown = maxf(_attitude_shown - delta, 0.0)
	if _glide < 1.0:
		_glide = minf(_glide + delta / 0.3, 1.0)
		_orbit_to_glide()
	if mode == Mode.WORK and _state == IDLE:
		# Keep looking under the pointer: the board may have changed under it.
		hover_screen(_pointer)
	var follow := 1.0 - exp(-delta * 18.0)
	if current != "":
		var body = tools[current]
		if mode == Mode.WALK:
			body.global_transform = _hand_pose()
		elif _engaged:
			# Drop onto the work quickly, then ride exactly where the stroke puts it.
			var t: float = clamp((Time.get_ticks_msec() / 1000.0 - _engage_time) / 0.08, 0.0, 1.0)
			body.global_transform = _engage_pose.interpolate_with(board.get_tool_pose(), t)
		elif _state == PLANNING:
			# Set on the work where the stroke starts, out of sight until it acts.
			body.global_transform = board.pose_at(_lock.point, _leaned(_lock.normal, _lock.path), _lock.along, 0.0)
		else:
			body.global_transform = body.global_transform.interpolate_with(_hover_pose(), follow)
	# The tool follows where it was dragged at its working speed.
	var went := Vector3.ZERO
	if _engaged and current in PUSHED:
		# Half a millimetre at a time, steered after each (however long the frame, or fast
		# the pace): a curve is followed as finely at a few frames a second as at sixty.
		var budget := working_speed() * delta
		while budget > 1e-6 and _progress < _target:
			var step := minf(minf(budget, 0.5), _target - _progress)
			_progress += step
			budget -= step
			went += _lock.path * (step * MM)
			board.move_stroke(_lock.point + _lock.path * (_progress * MM))
			_steer()
	elif _engaged and _at != _goal and not is_chopping():
		var reach := working_speed() * delta * MM
		var to_go := _goal - _at
		went = to_go if to_go.length() <= reach else to_go.normalized() * reach
		_at = _goal if to_go.length() <= reach else _at + went
		board.move_stroke(_at)
	_place_blade(went / maxf(delta, 1e-6))
	# (After a stroke too: the sponge's last work lands after it ends.)
	if board != null:
		_take_debris(board.get_tool_pose())
	# A plan asked for while an edit was landing is planned again once it has.
	if _state == PLANNING and board != null and _plan.get("stale", false):
		var fresh: Dictionary = board.get_plan()
		if not fresh.is_empty() and not fresh.get("stale", false):
			_plan = fresh
	# At the bench, the tool in hand is seen only while it works, fading in and out.
	if current != "" and mode == Mode.WORK:
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
				_refresh_collider(offcut.from)
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


## Where the part of the tool in hand that meets the work is while it works, and how fast it
## goes (m/s): a chisel's or gouge's blade (a box round its 30 mm behind the edge, rising at
## its angle to the work); a rasp's face, a scraper's card, a block's or sponge's body, a
## spokeshave's sole. (Not the saw's: its plate slides along its own kerf.)
const _BODY_BOXES := {"rasp": Vector3(200, 25, 6), "scraper": Vector3(1, 60, 100), "spokeshave": Vector3(40, 62, 12),
		"sanding_sponge": Vector3(100, 68, 25)}


func _place_blade(velocity: Vector3) -> void:
	_blade_on = _engaged and current != "saw" and not is_chopping()
	_edge_velocity = velocity if _blade_on else Vector3.ZERO
	if not _blade_on:
		return
	var pose: Transform3D = board.get_tool_pose()
	var x := pose.basis.x.normalized()
	var z := pose.basis.z.normalized()
	if current != "chisel" and current != "gouge":
		var box: Vector3 = _BODY_BOXES.get(current, Vector3.ZERO)
		if current == "sanding_block":
			box = Vector3(variant().get("length", 70.0), variant().get("width", 40.0), 26.0)
		elif current == "spokeshave" and variant().get("id", "spokeshave") != "spokeshave":
			box = Vector3(variant().get("sole", 150.0), variant().get("sole_width", 42.0), 30.0) # (a plane)
		if _blade.size != box * MM:
			_blade.size = box * MM
		# Standing on the work: from its face up.
		_blade_at = Transform3D(Basis(x, z.cross(x), z), pose.origin + z * (box.z * 0.5 * MM))
		return
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
	_carry(delta)


## The carried piece goes where the hands take it: towards its place in front of the eyes,
## turned as it was held, at velocities (so it still meets the walls and the bench, and
## pushes other pieces, rather than passing through them).
func _carry(delta: float) -> void:
	if held == null:
		return
	var eyes: Transform3D = player.camera.global_transform
	var to: Vector3 = eyes.origin - eyes.basis.z * _hold - held.global_position
	held.linear_velocity = (to * 15.0).limit_length(4.0)
	var want := (Basis(Vector3.UP, player.yaw) * _hold_turn).orthonormalized()
	var turn := (want * held.global_basis.orthonormalized().inverse()).get_rotation_quaternion()
	var angle := turn.get_angle()
	if angle > PI:
		angle -= TAU
	held.angular_velocity = turn.get_axis() * angle * 12.0 if absf(angle) > 1e-4 else Vector3.ZERO
	held.sleeping = false


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
			# Along the board: the edge's advance this step, less any way it goes up or down, as
			# far as it is free to go (never into the work, the bench or another piece).
			var advance: Vector3 = _edge_velocity * delta
			advance -= Vector3.UP * advance.dot(Vector3.UP)
			piece.global_position += advance * _free_to_move(piece, advance)
			piece.sleeping = false


## How much of `motion` (0 to 1) a piece can be moved without going into anything: its shapes
## swept along it from a hair above where they are (clear of what it rests on).
func _free_to_move(piece: RigidBody3D, motion: Vector3) -> float:
	var free := 1.0
	var space := get_world_3d().direct_space_state
	for shape in _shapes_of(piece):
		var query := PhysicsShapeQueryParameters3D.new()
		query.shape = shape.shape
		query.transform = shape.global_transform.translated(Vector3.UP * PUSH_CLEAR)
		query.motion = motion
		query.collision_mask = piece.collision_mask
		query.exclude = [piece.get_rid()]
		free = minf(free, space.cast_motion(query)[0])
	return free


## Where the tool in hand is held while walking: in front of the eyes, a little to the right
## and below, its working direction forward and its face up.
func _hand_pose() -> Transform3D:
	var eyes: Transform3D = player.camera.global_transform
	var basis := Basis(Vector3.UP, HAND_TURN) * Basis(Vector3(0, 0, -1), Vector3(-1, 0, 0), Vector3(0, 1, 0))
	return Transform3D(eyes.basis * basis * Basis.from_scale(Vector3.ONE * MM), eyes * HAND)


## Where the tool in hand floats: over the board under the pointer, or over its middle.
func _hover_pose() -> Transform3D:
	if _hit.is_empty():
		var up := Vector3.UP
		return board.pose_at(board.global_position + up * 0.0125, up, _along(up), 60.0)
	var along := _along(_hit.normal)
	return board.pose_at(_hit.position, _leaned(_hit.normal, along), along, HOVER_LIFT)


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


## The footprint of the tool in hand where it would engage: the chisel's edge and push
## direction, the saw's line, the sanding block's face, the sponge's reach.
func _draw_outline() -> void:
	var mesh: ImmediateMesh = _outline.mesh
	mesh.clear_surfaces()
	if current == "" or current == "layout" or _engaged or mode != Mode.WORK:
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
			# The edge, turned by its skew (the hand's and a skew chisel's own; only a flat edge
			# skews: core plan_cut), and the way it goes.
			var edge := s
			if variant().get("flat", true):
				edge = s.rotated(n, -deg_to_rad(settings[current].skew + variant().get("skew", 0.0)))
			segments = [p - edge * w, p + edge * w, p, p + a * 0.012, p + a * 0.012, p + a * 0.009 + s * 0.002,
					p + a * 0.012, p + a * 0.009 - s * 0.002]
		"saw":
			var l := 0.07
			segments = [p - a * l, p + a * l, p - a * l - s * 0.003, p - a * l + s * 0.003,
					p + a * l - s * 0.003, p + a * l + s * 0.003]
		"rasp", "scraper", "spokeshave":
			# Its face or blade across the stroke, as wide as it is.
			var half: float = {"rasp": 12.5, "scraper": 30.0, "spokeshave": variant().get("width", 50.0) * 0.5}[current] * MM
			segments.append_array([p - s * half, p + s * half, p - s * half, p - s * half + a * 0.006,
					p + s * half, p + s * half + a * 0.006])
		"sanding_block":
			var hl: float = variant().get("length", 70.0) * 0.5 * MM
			var hb: float = variant().get("width", 40.0) * 0.5 * MM
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
	# Locked to an edge: the edge, 40 mm of it.
	if (_state == PLANNING or _state == ARMED) and _lock.get("snapped", false):
		var edge: Dictionary = _lock.edge
		var on: Vector3 = edge.point + _lock.normal * 0.0003
		mesh.surface_set_color(EDGE_COLOUR)
		for v in [on - _lock.path * 0.02, on + _lock.path * 0.02]:
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
