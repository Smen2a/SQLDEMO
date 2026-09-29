extends Node

## Putting parts together. A part carried to its mate in the vise (both laid out from one
## plan, with a joint between them: plans.joints_between) is offered up: looked at, the mate
## shows where the part would go, lined up with the joint at its mouth (a ghost); E puts it
## there. The wheel pushes it in as far as a hand can (the joint's fit, SdfBody.fit: until it
## binds), a click taps it on with the mallet (a tight fit, as far as a blow drives it), and
## the wheel back draws it out. E lets go: in far enough and not loose, the two are one rigid
## body, the tools working on whichever part is hit (workshop.bodies_of); else it drops.
##
## Apart again: F on a joined part draws it back out along its joint (the wheel back, into the
## hands), and undo straight after joining takes it out into the hands; until it is held for
## good. G while offering a part up glues it: joined, the glue sets GLUE_SET seconds later
## (game time), and then the joint is for good. A wedge (a joint whose part opens its mate's
## kerf: driven tighter, WEDGE_SPREAD) driven in holds itself, and the joint whose part it is
## driven into, for good.

const GHOST_COLOUR := Color(0.2, 0.45, 1.0)
const PUSH := 1.0  # mm a notch of the wheel
const FINE := 0.2  # with Ctrl
const DRIVE := 3.0 # mm a firm blow drives a tight fit (a tap less, a heavy blow more)
const SNUG := 0.05 # mm: tighter than this, a hand's push binds (as the fit's snug)
const TIGHT := 0.5 # mm: tighter than this, not even the mallet drives it (the fit's drive)
const HOLDS := 3.0 # mm in: let go further in, not loose, it holds
const BLOW_INTERVAL := 0.3 # s between blows
const GLUE_SET := 60.0 # s (game time) glue takes to set
const WEDGE_SPREAD := 2.5 # mm a wedge is driven into its kerf's sides (it opens the kerf)
const WEDGE_LOCKS := 10.0 # mm a wedge driven in holds its joint

var workshop
## The part being offered up, or {}: its "piece" (the rigid body, frozen on the joint) and the
## "mate" (the rigid body in the vise); the joint ("joint": SdfBody.joint_pose; "name"); which
## of them goes in ("held_moves"); the SdfBodies ("moving", "still": the joint's moving part's
## and its mate's) and their parts' placements ("p_moving", "p_still"); the fit going in
## ("fit": SdfBody.fit's); how far in ("t", mm); what that comes to ("said").
var offer := {}
var _ghost: MeshInstance3D
var _mesh := ImmediateMesh.new()
var _last_blow := -10.0
## Game time (s): the sum of the frames' deltas (glue sets by it).
var clock := 0.0


func _ready() -> void:
	_ghost = MeshInstance3D.new()
	_ghost.mesh = _mesh
	_ghost.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	material.no_depth_test = true
	_ghost.material_override = material
	add_child(_ghost)


func offering() -> bool:
	return not offer.is_empty() and is_instance_valid(offer.piece)


## What the carried part would be offered up to, looked at: the joint with a part of the
## piece in the vise; {} with none.
func candidate() -> Dictionary:
	var held: RigidBody3D = workshop.held
	var mate: RigidBody3D = workshop.clamped
	if held == null or mate == null or not held.get_meta("members", []).is_empty():
		return {}
	var hit: Dictionary = workshop.look_at_thing()
	if hit.is_empty() or hit.collider != mate:
		return {}
	var tag: Dictionary = held.get_meta("part", {})
	if tag.is_empty():
		return {}
	var held_sdf = held.get_meta("sdf")
	for holder in workshop.holders_of(mate):
		var other: Dictionary = holder.get_meta("part", {})
		if other.get("plan", "") != tag.plan:
			continue
		for joint in workshop.plans.joints_between(tag.plan, tag.part, other.part):
			var a_is_held: bool = joint.a == tag.part
			var a_part: Dictionary = workshop.plans.part(tag.plan, joint.a)
			var b_part: Dictionary = workshop.plans.part(tag.plan, joint.b)
			var pose: Dictionary = SdfBody.joint_pose(a_part, joint.get("a_feature", ""), b_part, joint.get("b_feature", ""))
			if pose.is_empty():
				continue
			var a_moves: bool = pose.moving == "a"
			var held_moves := a_moves == a_is_held
			var mate_sdf = holder if holder is SdfBody else holder.get_meta("sdf")
			return {"piece": held, "mate": mate, "joint": pose, "name": joint.get("name", "the joint"),
					"held_moves": held_moves, "moving": held_sdf if held_moves else mate_sdf,
					"still": mate_sdf if held_moves else held_sdf,
					"p_moving": tag.placement if held_moves else other.placement,
					"p_still": other.placement if held_moves else tag.placement,
					"held_name": workshop.plans.part(tag.plan, tag.part).get("name", tag.part).to_lower(),
					"mate_name": workshop.plans.part(tag.plan, other.part).get("name", other.part).to_lower()}
	return {}


## E with a part to offer up: it goes on its joint at the mouth (out of the hands, held there),
## and how it fits going in is measured.
func offer_up() -> bool:
	var c := candidate()
	if c.is_empty():
		return false
	var piece: RigidBody3D = c.piece
	workshop.held = null
	# (Still moving as the hands steered it: stopped, or it drifts off the joint.)
	piece.linear_velocity = Vector3.ZERO
	piece.angular_velocity = Vector3.ZERO
	piece.freeze_mode = RigidBody3D.FREEZE_MODE_KINEMATIC
	piece.freeze = true
	piece.gravity_scale = 1.0
	piece.can_sleep = true
	offer = c
	offer.glued = false
	_start(0.0)
	return true


## The fit going in measured, and the part put `t` mm in.
func _start(t: float) -> void:
	var joint: Dictionary = offer.joint
	var start := _relative(0.0)
	var axis: Vector3 = (offer.p_still.basis * joint.axis).normalized()
	var region: AABB = offer.p_moving * joint.region
	offer.drive = WEDGE_SPREAD if joint.kind == "wedge" else TIGHT
	offer.fit = offer.moving.fit(offer.still, start, axis, joint.travel, 0.5, region, offer.drive)
	offer.t = t
	offer.said = _state()
	_place()
	workshop._ui.refresh()


## The joint's moving part's body space into its mate's, `t` mm in: its placement, the
## joint's pose (turned the other way round where the parts were laid out mirrored), its
## mate's placement.
func _relative(t: float) -> Transform3D:
	var joint: Dictionary = offer.joint
	var pose: Transform3D = joint.home.translated(joint.axis * (t - joint.travel))
	var moving: Transform3D = offer.p_moving
	var relative: Transform3D = offer.p_still * pose * moving.affine_inverse()
	if relative.basis.determinant() < 0.0:
		relative = relative * (moving * joint.mirror * moving.affine_inverse())
	return relative


## The offered part where the joint has it, `offer.t` mm in.
func _place() -> void:
	var relative := _relative(offer.t)
	var held_sdf = offer.moving if offer.held_moves else offer.still
	var other_sdf = offer.still if offer.held_moves else offer.moving
	var target: Transform3D = other_sdf.global_transform * (relative if offer.held_moves else relative.affine_inverse())
	offer.piece.global_transform = target * held_sdf.transform.affine_inverse()


## How far a push goes (a hand's: SNUG; the mallet's: TIGHT): up to where the fit is tighter
## than that, or it stops.
func _reach(limit: float) -> float:
	var fit: Dictionary = offer.fit
	var last := 0.0
	for step in fit.get("steps", []):
		if step.interference > limit:
			return last
		last = step.t
	return fit.get("stops_at", 0.0)


## The wheel: pushed in `mm` (back out, negative; out past the mouth, back into the hands).
func push(mm: float) -> void:
	if not offering():
		return
	if mm < 0.0:
		if offer.t + mm < 0.0:
			_take_back()
			return
		offer.t += mm
	else:
		offer.t = maxf(offer.t, minf(offer.t + mm, _reach(SNUG)))
	offer.said = _state()
	_place()
	workshop._ui.refresh()


## A click: a blow with the mallet (`blow`: the chop's strength, 1 a firm one), driving a
## tight fit on as far as it will go: the tighter it is, the less far (a wedge goes in
## quickly, then hardly at all).
func tap(blow := 1.0) -> bool:
	if not offering():
		return false
	var now := Time.get_ticks_msec() / 1000.0
	if now - _last_blow < BLOW_INTERVAL:
		return false
	_last_blow = now
	var was: float = offer.t
	var tight := clampf(_tightness_after(offer.t) / offer.drive, 0.0, 1.0)
	offer.t = maxf(offer.t, minf(offer.t + DRIVE * blow * maxf(0.2, 1.0 - tight), _reach(offer.drive)))
	offer.said = _state()
	_place()
	workshop._ui.refresh()
	return offer.t > was


## What the joint comes to where the part is: in words.
func _state() -> String:
	var fit: Dictionary = offer.fit
	var t: float = offer.t
	var travel: float = offer.joint.travel
	if t >= travel - 0.01:
		match fit.kind:
			"loose":
				return "Home, loose: %.2f mm clear. It won't hold by itself." % fit.clearance
			"drives":
				return "Home, driven: it holds."
		return "Home, seated on its shoulder: a snug fit."
	var hand := _reach(SNUG)
	if t >= fit.stops_at - 0.01 and not fit.home:
		return "It won't go further: %s mm in of %s." % [_mm(t), _mm(travel)]
	if t >= hand - 0.01 and hand < _reach(offer.drive):
		return "It binds, %.2f mm tight: tap it on with the mallet (click)." % _tightness_after(t)
	return "%s mm in of %s: push it on (wheel)." % [_mm(t), _mm(travel)]


func _tightness_after(t: float) -> float:
	for step in offer.fit.get("steps", []):
		if step.t > t + 0.01:
			return step.interference
	return 0.0


## G while offering: glue on the joint (it sets once joined, GLUE_SET later).
func glue() -> void:
	if offering() and not offer.get("glued", false):
		offer.glued = true
		workshop._ui.refresh()


## E while offering: in far enough, and not loose, the two are one body; else it drops.
func let_go() -> void:
	if not offering():
		return
	if offer.t >= HOLDS and not (offer.fit.kind == "loose" and offer.t >= offer.joint.travel - 0.01):
		join()
	else:
		var piece: RigidBody3D = offer.piece
		offer = {}
		piece.freeze = false
		piece.freeze_mode = RigidBody3D.FREEZE_MODE_STATIC
		workshop._ui.refresh()


## Back into the hands, off the joint.
func _take_back() -> void:
	var piece: RigidBody3D = offer.piece
	offer = {}
	piece.freeze = false
	piece.freeze_mode = RigidBody3D.FREEZE_MODE_STATIC
	workshop.pick_up(piece)
	workshop._ui.refresh()


## The offered part and its mate made one rigid body: its SdfBody moves into the mate's (the
## piece in the vise), taking its part's metas with it; its own rigid body goes.
func join() -> void:
	var piece: RigidBody3D = offer.piece
	var anchor: RigidBody3D = offer.mate
	var sdf = piece.get_meta("sdf")
	for key in ["part", "marks", "marks_undone", "pencil_log", "wood_name", "kind"]:
		if piece.has_meta(key):
			sdf.set_meta(key, piece.get_meta(key))
	var placed: Transform3D = sdf.global_transform
	piece.remove_child(sdf)
	anchor.add_child(sdf)
	sdf.global_transform = placed
	var to = offer.still if offer.held_moves else offer.moving
	var members: Array = anchor.get_meta("members", []).duplicate()
	members.append({"sdf": sdf, "to": to, "joint": offer.joint, "name": offer.name, "t": offer.t,
			"glued_at": clock if offer.get("glued", false) else -1.0, "wedge": offer.joint.kind == "wedge",
			"held_moves": offer.held_moves, "p_moving": offer.p_moving, "p_still": offer.p_still,
			"held_name": offer.held_name, "mate_name": offer.mate_name,
			"steps": [sdf.get_stats().get("steps", 0), to.get_stats().get("steps", 0)]})
	anchor.set_meta("members", members)
	offer = {}
	workshop.pieces.erase(piece)
	piece.queue_free()
	workshop._refresh_collider(anchor)
	workshop._ui.refresh()


## A joined part taken off its mate: a rigid body of its own again, its metas back on it.
func _unjoin(member: Dictionary) -> RigidBody3D:
	var sdf = member.sdf
	var anchor: RigidBody3D = sdf.get_parent()
	var members: Array = anchor.get_meta("members", []).duplicate()
	members.erase(member)
	anchor.set_meta("members", members)
	var placed: Transform3D = sdf.global_transform
	var centre: Vector3 = placed * sdf.get_body_bounds().get_center()
	var piece: RigidBody3D = workshop._as_piece(sdf, Transform3D(Basis.IDENTITY, centre), placed,
			sdf.get_meta("wood_name", "wood"), sdf.get_meta("kind", "part"))
	for key in ["part", "marks", "marks_undone", "pencil_log"]:
		if sdf.has_meta(key):
			piece.set_meta(key, sdf.get_meta(key))
			sdf.remove_meta(key)
	if workshop.board == sdf:
		workshop.board = anchor.get_meta("sdf")
	workshop._refresh_collider(anchor)
	return piece


## F on a joined part: off its mate, back on the joint where it was, to be drawn out (the
## wheel back) or pushed on.
func pull_out(sdf) -> bool:
	var anchor = sdf.get_parent()
	for member in anchor.get_meta("members", []):
		if member.sdf == sdf:
			if locked(member):
				return false
			var glued: bool = member.get("glued_at", -1.0) >= 0.0
			var piece := _unjoin(member)
			piece.freeze_mode = RigidBody3D.FREEZE_MODE_KINEMATIC
			piece.freeze = true
			offer = {"piece": piece, "mate": anchor, "joint": member.joint, "name": member.name,
					"held_moves": member.held_moves, "moving": sdf if member.held_moves else member.to,
					"still": member.to if member.held_moves else sdf, "p_moving": member.p_moving,
					"p_still": member.p_still, "held_name": member.held_name, "mate_name": member.mate_name}
			offer.glued = glued
			_start(member.t)
			return true
	return false


## Whether a joined part is there for good: its glue set, a wedge driven in, or a wedge
## driven into it.
func locked(member: Dictionary) -> bool:
	return why_locked(member) != ""


## Why a joined part is there for good ("glued", "wedged"), or "".
func why_locked(member: Dictionary) -> String:
	if member.get("glued_at", -1.0) >= 0.0 and clock - member.glued_at >= GLUE_SET:
		return "glued"
	if member.get("wedge", false) and member.t >= WEDGE_LOCKS:
		return "wedged"
	var anchor = member.sdf.get_parent()
	if anchor != null:
		for other in anchor.get_meta("members", []):
			if other.get("wedge", false) and other.to == member.sdf and other.t >= WEDGE_LOCKS:
				return "wedged"
	return ""


## The joined parts of a piece, in words: how each is held.
func joined_lines(piece: RigidBody3D) -> Array[String]:
	var out: Array[String] = []
	if piece == null:
		return out
	for member in piece.get_meta("members", []):
		var why := why_locked(member)
		var held := "held for good (%s)" % why if why != "" else "comes out again (F)"
		if why == "" and member.get("glued_at", -1.0) >= 0.0:
			held = "glued: sets in %.0f s" % maxf(GLUE_SET - (clock - member.glued_at), 0.0)
		out.append("The %s in the %s, %s mm in: %s." % [member.held_name, member.mate_name, _mm(member.t), held])
	return out


## Undo straight after joining (neither part worked since): the joined part out into the
## hands. True where it did.
func undo() -> bool:
	var anchor: RigidBody3D = workshop.clamped
	if anchor == null or workshop.held != null:
		return false
	var members: Array = anchor.get_meta("members", [])
	if members.is_empty():
		return false
	var last: Dictionary = members.back()
	if last.sdf.get_stats().get("steps", 0) != last.steps[0] or last.to.get_stats().get("steps", 0) != last.steps[1]:
		return false
	if last.get("glued_at", -1.0) >= 0.0 and clock - last.glued_at >= GLUE_SET:
		return false # (the glue has set)
	var piece := _unjoin(last)
	workshop.pick_up(piece)
	workshop._ui.refresh()
	return true


## The joined part of the piece in the vise under a ray (world), or null.
func member_at(origin: Vector3, direction: Vector3):
	var anchor: RigidBody3D = workshop.clamped
	if anchor == null or anchor.get_meta("members", []).is_empty():
		return null
	var body = workshop.body_hit(anchor, origin, direction)
	return body if body != null and body != anchor.get_meta("sdf") else null


## The line under the crosshair while offering.
func prompt() -> String:
	if not offering():
		return ""
	var glue := "glued" if offer.get("glued", false) else "G: glue it"
	return "%s   Wheel: push in, draw out (Ctrl: finely)   Click: tap it with the mallet   %s   E: let go" % [offer.said, glue]


## The offered part kept on its joint, each physics step.
func _physics_process(_delta: float) -> void:
	if offering():
		_place()


func _process(delta: float) -> void:
	clock += delta
	_mesh.clear_surfaces()
	if workshop.mode != workshop.Mode.WALK or offering() or workshop.held == null:
		return
	var c := candidate()
	if c.is_empty():
		return
	# The ghost: the carried part's bounds where the joint would put it, at the mouth.
	var saved := offer
	offer = c
	var relative := _relative(0.0)
	offer = saved
	var held_sdf = c.moving if c.held_moves else c.still
	var other_sdf = c.still if c.held_moves else c.moving
	var target: Transform3D = other_sdf.global_transform * (relative if c.held_moves else relative.affine_inverse())
	var box: AABB = held_sdf.get_body_bounds()
	_mesh.surface_begin(Mesh.PRIMITIVE_LINES)
	_mesh.surface_set_color(GHOST_COLOUR)
	for a in 8:
		for axis in 3:
			if a & (1 << axis):
				continue
			var b := a | (1 << axis)
			_mesh.surface_add_vertex(target * _corner(box, a))
			_mesh.surface_add_vertex(target * _corner(box, b))
	_mesh.surface_end()


static func _corner(box: AABB, i: int) -> Vector3:
	return box.position + Vector3(box.size.x if i & 1 else 0.0, box.size.y if i & 2 else 0.0, box.size.z if i & 4 else 0.0)


static func _mm(v: float) -> String:
	return ("%.0f" % v) if absf(v - roundf(v)) < 0.05 else ("%.1f" % v)
