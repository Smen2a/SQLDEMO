extends Node3D

## What the tools take off the work, made visible: the board's take_debris() (core
## tools/debris.h). Cosmetic: nothing here goes back into a body.
##   shavings  a chisel's, gouge's or spokeshave's shaving grows at the edge as the stroke
##             goes: it rides up the blade's back and curls over (the tighter the thinner it
##             is: a radius of 1.5 mm + 12 times its thickness, winding in a thickness a
##             turn so the turns never meet). It comes off shorter and thicker than the cut
##             (COMPRESSION: the same wood). Where it breaks (across the grain, over a gap)
##             and when the stroke ends, it comes away: a light rigid body that falls. It is
##             the chip the edge took: as wide and as thick as the wood over it (half the
##             chisel's width, pared half off the work), on that side of the edge.
##   chips     tear-out, breakout, a chop's pop-off: a block of the wood thrown off the face.
##   chunks    crumbs: bits under 30 mm^3 that cuts left apart from the work (a corner a few
##             cuts meet round, a sliver a saw took off), taken out of it (SdfBody's
##             crumbled(): add_chunks()). Each is its hull, faceted and coloured per corner,
##             and drops from where it was.
##   dust      the saw's, the rasps', the scraper's and the sanding tools': a puff of grains
##             that goes as soon as it lands, what reaches the ground heaped into a pile
##             there (dust.gd).
## Shavings, chips and chunks are passing things: once one has lain still for REST it fades
## out over FADE and is gone. Each piece belongs to the board's undo step that made it
## (undo_step() takes it back). The newest LIVE pieces move under physics; older ones fade
## out; past MOST the oldest go. World space throughout (metres); the report's volumes are
## mm^3.

const Dust := preload("res://workshop/dust.gd")
const MM := 0.001
const COMPRESSION := 0.7
const LIVE := 24
const MOST := 60
const REST := 2.0 # s a piece lies still before it fades
const FADE := 0.5 # s it takes to fade out
const STILL := 0.005 # m/s: slower than this (for SETTLING), a piece is at rest
const SETTLING := 0.2 # s
const FALLING := 3.0 # s after it came away a piece counts as at rest, still or not
const MARGIN := 0.0001 # m: sharp shapes at millimetre scale (Godot's default rounds them away)
const LAYER := 4 # its pieces' physics layer bit (the world and loose pieces are on 1)
const LIFT := 0.0005 # m a piece is set clear of where it came from before it falls
const CLEAR_FROM := 0.03 # m above where it came away that a piece born inside something is set down from

## Pieces come away as these, oldest first: {"body": RigidBody3D, "view": its MeshInstance3D,
## "step": the board's step that made it, "kind": "shaving", "chip" or "chunk", "volume":
## mm^3, and when (the clock, s) it "came" away, was last "moving", came to "rest" and began
## to "fade" (-1: not yet)}.
var pieces: Array[Dictionary] = []
## How many of each kind came away (since the workshop started): {"shaving", "chip", "chunk"}.
var made := {"shaving": 0, "chip": 0, "chunk": 0}
## The shaving being taken, if any: its samples (world space) and what they came to.
var _live := {}
var _live_mesh: MeshInstance3D
var _edge := Transform3D()
var _shavings: StandardMaterial3D
var _chunks: StandardMaterial3D
var feed_usec := 0 ## what the last feed() cost
var dust ## dust.gd: the grains and piles
var _clock := 0.0 # s of game time


func _ready() -> void:
	_shavings = StandardMaterial3D.new()
	_shavings.vertex_color_use_as_albedo = true
	_shavings.cull_mode = BaseMaterial3D.CULL_DISABLED # a thin ribbon, seen from both sides
	_shavings.roughness = 0.75
	_chunks = StandardMaterial3D.new()
	_chunks.vertex_color_use_as_albedo = true
	_chunks.roughness = 0.8
	_live_mesh = MeshInstance3D.new()
	_live_mesh.mesh = ArrayMesh.new()
	_live_mesh.material_override = _shavings
	add_child(_live_mesh)
	dust = Dust.new()
	add_child(dust)


## Takes in what came off (SdfBody.take_debris()) for the board's undo step `step`, with the
## edge where the tool is now (SdfBody.get_tool_pose()): the live shaving grows from there.
func feed(report: Dictionary, step: int, edge: Transform3D) -> void:
	var t0 := Time.get_ticks_usec()
	if report.get("cancelled", false):
		_drop_live()
	var moved := not edge.is_equal_approx(_edge)
	_edge = edge
	if report.has("shaving"):
		var s: Dictionary = report.shaving
		for i in s.points.size():
			if s.starts[i] == 1 and not _live.is_empty():
				_release_live() # it broke: what came before comes away
			if _live.is_empty():
				_live = {"points": PackedVector3Array(), "thickness": PackedFloat32Array(),
						"width": PackedFloat32Array(), "offset": PackedFloat32Array(), "colours": PackedColorArray(),
						"volume": 0.0,
						"step": step, "spacing": float(report.get("step", 0.5 * MM)),
						"rise": float(report.get("rise", 20.0)), "density": float(report.get("density", 0.7))}
			_live.points.push_back(s.points[i])
			_live.thickness.push_back(s.thickness[i])
			_live.width.push_back(s.width[i])
			_live.offset.push_back(s.offset[i] if s.has("offset") else 0.0)
			_live.colours.push_back(s.colours[i])
			_live.volume += s.volume[i]
		moved = true
	for chip in report.get("chips", []):
		_add_chip(chip, step, float(report.get("density", 0.7)))
	if report.has("dust"):
		dust.add(report.dust, step)
	if not _live.is_empty() and moved:
		_shape_live()
	if report.get("ended", false):
		_release_live()
	_keep_cheap()
	feed_usec = Time.get_ticks_usec() - t0


## How many samples the shaving being taken holds (0: none).
func live_samples() -> int:
	return 0 if _live.is_empty() else _live.points.size()


## Whether nothing lies about: no pieces, no dust in the air or piled.
func is_empty() -> bool:
	return pieces.is_empty() and dust.flying() == 0 and dust.piles.is_empty()


## Takes back the pieces and dust the board's undo step `step` made (and any after it, up to
## `before`: the steps are numbered per piece of work, each piece's in a range of its own).
func undo_step(step: int, before: int = 1 << 62) -> void:
	for i in range(pieces.size() - 1, -1, -1):
		if pieces[i].step >= step and pieces[i].step < before:
			pieces[i].body.queue_free()
			pieces.remove_at(i)
	dust.undo_step(step, before)


## Sweeps the bench: every piece goes, and the dust.
func clear() -> void:
	_drop_live()
	for piece in pieces:
		piece.body.queue_free()
	pieces.clear()
	dust.clear()


func _drop_live() -> void:
	_live = {}
	(_live_mesh.mesh as ArrayMesh).clear_surfaces()


## Pieces that have lain still for REST fade out, and go.
func _process(delta: float) -> void:
	_clock += delta
	for i in range(pieces.size() - 1, -1, -1):
		var piece: Dictionary = pieces[i]
		var body: RigidBody3D = piece.body
		if piece.fade < 0.0:
			if piece.rest < 0.0:
				var still := body.freeze or body.sleeping or body.linear_velocity.length() < STILL
				if not still:
					piece.moving = _clock
				if _clock - piece.moving >= SETTLING or _clock - piece.came >= FALLING:
					piece.rest = _clock
			elif _clock - piece.rest >= REST:
				_begin_fade(piece)
			continue
		var left: float = 1.0 - (_clock - piece.fade) / FADE
		if left <= 0.0:
			body.queue_free()
			pieces.remove_at(i)
		else:
			(piece.view.material_override as StandardMaterial3D).albedo_color.a = left


## A piece starts to fade: it stays where it is, nothing lands on it any more, and it is
## drawn in a see-through copy of its material.
func _begin_fade(piece: Dictionary) -> void:
	piece.fade = _clock
	var body: RigidBody3D = piece.body
	body.freeze_mode = RigidBody3D.FREEZE_MODE_STATIC
	body.freeze = true
	for child in body.get_children():
		if child is CollisionShape3D:
			child.disabled = true
	var look: StandardMaterial3D = piece.view.material_override.duplicate()
	look.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	piece.view.material_override = look


## Pieces lying within `shape` placed at `xf` (world) fade away: something is set down there.
func fade_within(shape: Shape3D, xf: Transform3D) -> void:
	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = shape
	query.transform = xf
	query.collision_mask = LAYER
	var hits := {}
	for hit in get_world_3d().direct_space_state.intersect_shape(query, 32):
		hits[hit.collider] = true
	for piece in pieces:
		if piece.fade < 0.0 and hits.has(piece.body):
			_begin_fade(piece)


## A piece's entry in `pieces`, just come away.
func _piece(body: RigidBody3D, view: MeshInstance3D, step: int, kind: String, volume: float) -> Dictionary:
	made[kind] += 1
	return {"body": body, "view": view, "step": step, "kind": kind, "volume": volume, "came": _clock, "moving": _clock,
			"rest": -1.0, "fade": -1.0}


# --- the shaving ------------------------------------------------------------------------

## The shaving's centreline and cross-sections: from the edge (its newest sample) up the
## blade and curling over to its oldest end. Returns {"centres", "inward" (towards the
## curl's centre), "across" (its width's direction), "half_width", "half_thickness",
## "colours"}, a cross-section per sample, newest first.
func _curl(live: Dictionary, edge: Transform3D) -> Dictionary:
	var along := edge.basis.x.normalized()
	var out_of := edge.basis.z.normalized()
	var across := edge.basis.y.normalized()
	var rise := deg_to_rad(live.rise)
	var heading := -along * cos(rise) + out_of * sin(rise) # up the blade's back
	var inward := along * sin(rise) + out_of * cos(rise)   # away from it: the curl's side
	var n: int = live.points.size()
	var mean := 0.0
	for t in live.thickness:
		mean += t
	mean /= maxf(float(n), 1.0)
	var thick := mean / COMPRESSION
	var radius := 1.5 * MM + 12.0 * mean
	var ds: float = live.spacing * COMPRESSION
	var turned := 0.0
	var at: Vector3 = edge.origin + inward * (0.5 * thick)
	var centres := PackedVector3Array()
	var inwards := PackedVector3Array()
	var half_widths := PackedFloat32Array()
	var half_thick := PackedFloat32Array()
	var colours := PackedColorArray()
	for k in n:
		var i := n - 1 - k
		centres.push_back(at + across * live.offset[i]) # (where the wood was across the edge)
		inwards.push_back(inward)
		half_widths.push_back(0.5 * live.width[i])
		half_thick.push_back(0.5 * live.thickness[i] / COMPRESSION)
		colours.push_back(live.colours[i])
		# Wound in a thickness a turn, down to a third of the radius.
		var r := maxf(radius - 1.15 * thick * turned / TAU, 0.35 * radius)
		var turn := ds / r
		var mid_heading := (heading * cos(0.5 * turn) + inward * sin(0.5 * turn))
		at += mid_heading * ds
		var h := heading * cos(turn) + inward * sin(turn)
		inward = inward * cos(turn) - heading * sin(turn)
		heading = h
		turned += turn
	return {"centres": centres, "inward": inwards, "across": across, "half_width": half_widths,
			"half_thickness": half_thick, "colours": colours}


## A ribbon through the curl's cross-sections (a strip at mid-thickness, both sides drawn),
## placed by `to_local`.
func _ribbon(curl: Dictionary, to_local := Transform3D.IDENTITY) -> ArrayMesh:
	var verts := PackedVector3Array()
	var normals := PackedVector3Array()
	var colours := PackedColorArray()
	var across: Vector3 = curl.across
	for k in curl.centres.size():
		var c: Vector3 = curl.centres[k]
		var w: float = curl.half_width[k]
		var normal: Vector3 = to_local.basis * -curl.inward[k]
		verts.push_back(to_local * (c - across * w))
		verts.push_back(to_local * (c + across * w))
		normals.push_back(normal)
		normals.push_back(normal)
		colours.push_back(curl.colours[k])
		colours.push_back(curl.colours[k])
	var mesh := ArrayMesh.new()
	if verts.size() < 4:
		return mesh
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = verts
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colours
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLE_STRIP, arrays)
	return mesh


func _shape_live() -> void:
	_live_mesh.mesh = _ribbon(_curl(_live, _edge))


## The shaving so far comes away as a body of its own, from where it curls now. Its
## collider is the box round it in its own frame (along the cut, across it, out of the
## face): a hull round the curl rocks from facet to facet and never settles.
func _release_live() -> void:
	if _live.is_empty():
		return
	var live := _live
	_drop_live()
	if live.points.size() < 2:
		return
	var curl := _curl(live, _edge)
	var axes := Basis(_edge.basis.x.normalized(), _edge.basis.y.normalized(), _edge.basis.z.normalized())
	var lo := Vector3.INF
	var hi := -Vector3.INF
	for k in curl.centres.size():
		var w: Vector3 = curl.across * curl.half_width[k]
		var t: Vector3 = curl.inward[k] * curl.half_thickness[k]
		for corner in [curl.centres[k] - w - t, curl.centres[k] + w + t]:
			var local: Vector3 = axes.transposed() * corner
			lo = lo.min(local)
			hi = hi.max(local)
	var box := BoxShape3D.new()
	box.size = (hi - lo).max(Vector3.ONE * 0.2 * MM)
	var xf := Transform3D(axes, axes * ((lo + hi) * 0.5))
	# Off the edge it rides clear of the cut and drops. Light and springy, it is soon still.
	var lift: Vector3 = Vector3.UP * (2.0 * curl.half_thickness[0] + LIFT)
	var body := _new_body(xf.translated(lift), box, live.volume * live.density * 1e-6)
	var view := MeshInstance3D.new()
	view.mesh = _ribbon(curl, xf.affine_inverse())
	view.material_override = _shavings
	body.add_child(view)
	body.linear_damp = 2.0
	body.angular_damp = 8.0
	pieces.append(_piece(body, view, live.step, "shaving", live.volume))


# --- chips ------------------------------------------------------------------------------

func _add_chip(chip: Dictionary, step: int, density: float) -> void:
	var size: Vector3 = chip.size
	var box := BoxShape3D.new()
	box.size = size.max(Vector3.ONE * 0.2 * MM)
	var xf: Transform3D = chip.transform
	# Out of the face it broke from, clear of the work, and flicked off it (it lands within
	# a few centimetres).
	var out_of: Vector3 = xf.basis.z
	var body := _new_body(xf.translated(out_of * (size.z + LIFT)), box, float(chip.volume) * density * 1e-6)
	var view := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = box.size
	view.mesh = mesh
	var look := StandardMaterial3D.new()
	look.albedo_color = chip.colour
	look.roughness = 0.8
	view.material_override = look
	body.add_child(view)
	body.linear_velocity = out_of * 0.05 + xf.basis.x * 0.03
	body.angular_velocity = xf.basis.y * 4.0
	body.angular_damp = 3.0
	pieces.append(_piece(body, view, step, "chip", float(chip.volume)))


# --- chunks -----------------------------------------------------------------------------

## Crumbs that came away from a piece of work (SdfBody's crumbled(): world space), for its
## undo step `step`.
func add_chunks(chunks: Array, step: int) -> void:
	for chunk in chunks:
		_add_chunk(chunk, step)
	_keep_cheap()


## A crumb: its hull, faceted (each face its own corners, with the face's normal) and coloured
## per corner, round its centre of volume; its collider the same hull.
func _add_chunk(chunk: Dictionary, step: int) -> void:
	var centre: Vector3 = chunk.centre
	var points: PackedVector3Array = chunk.points
	var triangles: PackedInt32Array = chunk.triangles
	var colours: PackedColorArray = chunk.colours
	var corners := PackedVector3Array()
	for p in points:
		corners.push_back(p - centre)
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var tints := PackedColorArray()
	for t in range(0, triangles.size() - 2, 3):
		var a := corners[triangles[t]]
		var b := corners[triangles[t + 1]]
		var c := corners[triangles[t + 2]]
		var n := (c - a).cross(b - a).normalized() # clockwise from outside: outwards
		for k in 3:
			vertices.push_back(corners[triangles[t + k]])
			normals.push_back(n)
			tints.push_back(colours[triangles[t + k]])
	if vertices.is_empty():
		return
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = tints
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var hull := ConvexPolygonShape3D.new()
	hull.points = corners
	# Where it was (it came away there): it drops, or where the work's collider still covers
	# it (a box round a board), is set down on it.
	var body := _new_body(Transform3D(Basis.IDENTITY, centre), hull, float(chunk.mass))
	var view := MeshInstance3D.new()
	view.mesh = mesh
	view.material_override = _chunks
	body.add_child(view)
	body.angular_damp = 3.0
	pieces.append(_piece(body, view, step, "chunk", float(chunk.volume)))


# --- bodies -----------------------------------------------------------------------------

func _new_body(xf: Transform3D, shape: Shape3D, mass: float) -> RigidBody3D:
	var body := RigidBody3D.new()
	# On their own layer, meeting the world and each other: the blade in hand pushes loose
	# pieces aside, not what it takes off.
	body.collision_layer = LAYER
	body.collision_mask = 1 | LAYER
	# A shaving falls up to a metre, a few centimetres a physics step: swept, it meets the
	# floor or the bench top rather than ending up inside it.
	body.continuous_cd = true
	add_child(body)
	body.global_transform = xf
	shape.margin = MARGIN
	var collider := CollisionShape3D.new()
	collider.shape = shape
	body.add_child(collider)
	# Weighed as a few grams at least: lighter, the solver shakes bodies this small about.
	body.mass = maxf(mass, 0.005)
	var surface := PhysicsMaterial.new()
	surface.friction = 0.6
	body.physics_material_override = surface
	var space := get_world_3d().direct_space_state
	var query := PhysicsShapeQueryParameters3D.new()
	query.shape = shape
	query.transform = xf
	query.collision_mask = 1
	# Born inside a loose piece (one the blade was pushing ahead of the edge, where the shaving
	# curls): the two would be forced apart violently. It goes through that one instead. Not
	# the piece in the vise (frozen), which it must rest on.
	var passes: Array[RID] = [body.get_rid()]
	for hit in space.intersect_shape(query, 8):
		if hit.collider is RigidBody3D and not hit.collider.freeze:
			body.add_collision_exception_with(hit.collider)
			passes.append(hit.rid)
	# Still inside something (the work in the vise, whose collider is its bounds, over a cut's
	# floor; a shaving lying there): set down on it from above instead, just touching.
	query.exclude = passes
	query.collision_mask = 1 | LAYER
	if not space.intersect_shape(query, 1).is_empty():
		query.transform = xf.translated(Vector3.UP * CLEAR_FROM)
		query.motion = Vector3.DOWN * CLEAR_FROM
		var safe: PackedFloat32Array = space.cast_motion(query)
		body.global_position = xf.origin + Vector3.UP * CLEAR_FROM * (1.0 - safe[0])
	return body


## The newest LIVE pieces move; older ones fade out (their colliders off: frozen solid they
## would hang in the air once what they lay on was carried off, and stop what came by); past
## MOST the oldest go at once.
func _keep_cheap() -> void:
	while pieces.size() > MOST:
		pieces.pop_front().body.queue_free()
	for i in pieces.size() - LIVE:
		if pieces[i].fade < 0.0:
			_begin_fade(pieces[i])
