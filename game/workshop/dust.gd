extends MultiMeshInstance3D

## Dust: grains of the wood the saw, the rasps, the scraper and the sanding tools take off
## (the board's take_debris() "dust"; debris.gd owns this). A quick puff: nothing collides
## with it, and every grain is gone within about a second.
##   - A puff throws up to PUFF grains, bigger when it carries more wood (dust takes BULK
##     times the room the wood did), up to WIDEST across, PALER than the wood (dust scatters
##     light).
##   - A grain flies from where it left the work, the way it was thrown with a little
##     scatter, under gravity. Where it lands is found once, when it is thrown: its arc
##     cast in three pieces against the physics space.
##   - Landed, it shrinks away over LINGER. Landed on the ground (a body with the meta
##     "ground": the bench, the floor), its wood goes into a pile there: one mound for all
##     the dust within REACH, growing as dust comes, so sawdust heaps up beyond a kerf's ends.
##     Dust landing on the board, or on anything else, just goes.
##   - Piles stay until clear() (Sweep); undo_step() takes a stroke's share back.
## World space throughout (metres); wood volumes are mm^3.

const MM := 0.001
const GRAINS := 3000 # in the air or shrinking away at once, at most (more are thrown unseen)
const PUFF := 16
const BULK := 2.5
const PALER := 0.15
const WIDEST := 0.0025 # m: the most a grain is drawn across
const THROW := 0.08 # m/s along the puff's direction
const SCATTER := 0.04 # m/s at random
const LIFT := 0.0005 # m above the work a grain starts from
const OUT := 0.0015 # m along the way it is thrown
const GRAVITY := 9.8
const PER_FRAME := 20 # grains thrown a frame at most (the rest wait for the next)
const LINGER := 0.3 # s a landed grain takes to shrink away
const REACH := 0.025 # m: dust landing this close to a pile's centre (or within it) joins it
const SAME_LEVEL := 0.005 # m: a pile's dust lands within this of its base (bench or floor)
const FLANK := 0.5 # a pile's height over its radius (flanks of about 37 degrees at the steepest)

var thrown := 0.0 ## mm^3 of wood thrown (since clear(), less what undo took back)
var update_usec := 0 ## what the last frame's grains cost
## The piles on the ground: {"view": MeshInstance3D, "base": Vector3 (the mean of where its
## dust landed, by wood), "by": {step: [mm^3 of wood, landing points and colours summed
## times their wood]}, "total": mm^3, "radius", "height" (m)}.
var piles: Array[Dictionary] = []
var _thrown_by := {} # step -> mm^3 thrown
var _free := PackedInt32Array() # slots not in use
# Grains in the air or shrinking away, in step: slot (-1: unseen), where from, how fast, time
# since thrown, when it lands, where, when and where it met a steep face (and slid down it),
# its turn and size, its step, wood, colour, and whether it lands on the ground (2 once its
# wood is in a pile).
var _fly_slot := PackedInt32Array()
var _fly_from := PackedVector3Array()
var _fly_velocity := PackedVector3Array()
var _fly_t := PackedFloat32Array()
var _fly_land := PackedFloat32Array()
var _fly_rest := PackedVector3Array()
var _fly_kink := PackedFloat32Array() # when it met a steep face and dropped straight down (INF: never)
var _fly_side := PackedVector3Array() # where
var _fly_basis: Array[Basis] = []
var _fly_step := PackedInt32Array()
var _fly_wood := PackedFloat32Array()
var _fly_colour := PackedColorArray()
var _fly_ground := PackedByteArray()
var _rng := RandomNumberGenerator.new()
var _query := PhysicsRayQueryParameters3D.new()
var _mound: ArrayMesh # a pile of radius 1 and height 1
# Grains waiting to be thrown: [at, direction, size, wood, colour, step] each.
var _waiting: Array[Array] = []


func _ready() -> void:
	var grains := MultiMesh.new()
	grains.transform_format = MultiMesh.TRANSFORM_3D
	grains.use_colors = true
	grains.mesh = BoxMesh.new()
	(grains.mesh as BoxMesh).size = Vector3(1.0, 0.25, 1.0) # a flake, scaled per grain
	grains.instance_count = GRAINS
	grains.visible_instance_count = GRAINS
	for slot in GRAINS:
		grains.set_instance_transform(slot, _hidden())
	multimesh = grains
	var look := StandardMaterial3D.new()
	look.vertex_color_use_as_albedo = true
	look.roughness = 1.0
	material_override = look
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	for slot in range(GRAINS - 1, -1, -1):
		_free.push_back(slot)
	_mound = _mound_mesh()
	_rng.seed = 1


## Throws the grains of the report's dust (SdfBody.take_debris()["dust"]) for the board's
## undo step `step`.
func add(dust: Dictionary, step: int) -> void:
	for i in dust.points.size():
		var wood: float = dust.volume[i]
		var grain: float = dust.grain[i]
		var n := clampi(ceili(BULK * wood / pow(grain / MM, 3.0)), 1, PUFF)
		var size := clampf(MM * pow(BULK * wood / n, 1.0 / 3.0), grain, maxf(grain, WIDEST))
		var colour: Color = (dust.colours[i] as Color).lerp(Color.WHITE, PALER)
		var direction: Vector3 = dust.directions[i]
		var spread: float = dust.spread[i]
		# Thrown a way (out of a kerf's end), it leaves a little way out, clear of the edge it
		# came over.
		var from: Vector3 = dust.points[i] + direction * OUT
		for k in n:
			var off := Vector2.from_angle(_rng.randf() * TAU) * spread * sqrt(_rng.randf())
			_waiting.append([from + Vector3(off.x, 0.0, off.y), direction, size, wood / n, colour, step])
		thrown += wood # counted now: it is on its way
		_thrown_by[step] = _thrown_by.get(step, 0.0) + wood
	_throw_some()


## Grains in the air or shrinking away, or waiting to be thrown.
func flying() -> int:
	return _fly_slot.size() + _waiting.size()


## mm^3 of wood in the piles.
func piled() -> float:
	var wood := 0.0
	for pile in piles:
		wood += pile.total
	return wood


## The piles whose base lies in `box` (world): {"count", "volume" (mm^3 of wood), "top"
## (the highest, m)}.
func piles_in(box: AABB) -> Dictionary:
	var count := 0
	var wood := 0.0
	var top := -INF
	for pile in piles:
		if box.has_point(pile.base):
			count += 1
			wood += pile.total
			top = maxf(top, pile.base.y + pile.height)
	return {"count": count, "volume": wood, "top": top}


## Takes back what the board's undo step `step` (and any after it, up to `before`) threw: the
## grains still to land, and its share of every pile.
func undo_step(step: int, before: int = 1 << 62) -> void:
	for i in range(_waiting.size() - 1, -1, -1):
		if _waiting[i][5] >= step and _waiting[i][5] < before:
			_waiting.remove_at(i)
	for i in range(_fly_slot.size() - 1, -1, -1):
		if _fly_step[i] >= step and _fly_step[i] < before:
			_end_flight(i)
	for s in _thrown_by.keys():
		if s >= step and s < before:
			thrown -= _thrown_by[s]
			_thrown_by.erase(s)
	for p in range(piles.size() - 1, -1, -1):
		var pile: Dictionary = piles[p]
		for s in pile.by.keys():
			if s >= step and s < before:
				pile.by.erase(s)
		if pile.by.is_empty():
			pile.view.queue_free()
			piles.remove_at(p)
		else:
			_shape_pile(pile)


## Sweeps it all away.
func clear() -> void:
	_waiting.clear()
	for i in range(_fly_slot.size() - 1, -1, -1):
		_end_flight(i)
	for pile in piles:
		pile.view.queue_free()
	piles.clear()
	thrown = 0.0
	_thrown_by.clear()


func _process(delta: float) -> void:
	if not _waiting.is_empty():
		_throw_some()
	if _fly_slot.is_empty():
		return
	var t0 := Time.get_ticks_usec()
	for i in range(_fly_slot.size() - 1, -1, -1):
		var t: float = _fly_t[i] + delta
		_fly_t[i] = t
		var land: float = _fly_land[i]
		if t >= land and _fly_ground[i] == 1:
			_fly_ground[i] = 2
			# (Where it lies is a fifth of its size up: the ground is under that.)
			var ground: Vector3 = _fly_rest[i] + Vector3.DOWN * (0.2 * _fly_basis[i].x.length())
			_to_pile(ground, _fly_wood[i], _fly_colour[i], _fly_step[i])
		if t >= land + LINGER:
			_end_flight(i)
			continue
		var slot: int = _fly_slot[i]
		if slot < 0:
			continue
		if t < land:
			var at: Vector3
			var kink: float = _fly_kink[i]
			if t < kink:
				at = _fly_from[i] + _fly_velocity[i] * t + Vector3.DOWN * (0.5 * GRAVITY * t * t)
			else:
				at = _fly_side[i] + Vector3.DOWN * (0.5 * GRAVITY * (t - kink) * (t - kink))
			multimesh.set_instance_transform(slot, Transform3D(_fly_basis[i], at))
		else:
			# Landed: it shrinks away where it lies.
			var left := 1.0 - (t - land) / LINGER
			multimesh.set_instance_transform(slot, Transform3D(_fly_basis[i].scaled(Vector3.ONE * left), _fly_rest[i]))
	update_usec = Time.get_ticks_usec() - t0


## The flight `i` ends: its grain goes (the last flight takes its place).
func _end_flight(i: int) -> void:
	var slot: int = _fly_slot[i]
	if slot >= 0:
		multimesh.set_instance_transform(slot, _hidden())
		_free.push_back(slot)
	var last := _fly_slot.size() - 1
	_fly_slot[i] = _fly_slot[last]
	_fly_from[i] = _fly_from[last]
	_fly_velocity[i] = _fly_velocity[last]
	_fly_t[i] = _fly_t[last]
	_fly_land[i] = _fly_land[last]
	_fly_rest[i] = _fly_rest[last]
	_fly_kink[i] = _fly_kink[last]
	_fly_side[i] = _fly_side[last]
	_fly_basis[i] = _fly_basis[last]
	_fly_step[i] = _fly_step[last]
	_fly_wood[i] = _fly_wood[last]
	_fly_colour[i] = _fly_colour[last]
	_fly_ground[i] = _fly_ground[last]
	_fly_slot.resize(last)
	_fly_from.resize(last)
	_fly_velocity.resize(last)
	_fly_t.resize(last)
	_fly_land.resize(last)
	_fly_rest.resize(last)
	_fly_kink.resize(last)
	_fly_side.resize(last)
	_fly_basis.resize(last)
	_fly_step.resize(last)
	_fly_wood.resize(last)
	_fly_colour.resize(last)
	_fly_ground.resize(last)


func _throw_some() -> void:
	for k in mini(PER_FRAME, _waiting.size()):
		var w: Array = _waiting.pop_front()
		_throw(w[0], w[1], w[2], w[3], w[4], w[5])


## Sends a grain flying from `at`: where it lands is found now. With no slot free it flies
## unseen (its wood still reaches a pile).
func _throw(at: Vector3, direction: Vector3, size: float, wood: float, colour: Color, step: int) -> void:
	var slot := -1
	if not _free.is_empty():
		slot = _free[_free.size() - 1]
		_free.resize(_free.size() - 1)
	var shade := _rng.randf_range(0.88, 1.04) # no two grains quite alike
	var tint := Color(colour.r * shade, colour.g * shade, colour.b * shade)
	var scatter := Vector3(_rng.randf_range(-1, 1), _rng.randf_range(0, 1), _rng.randf_range(-1, 1))
	var velocity := direction * THROW * _rng.randf_range(0.5, 1.5) + scatter * SCATTER
	var basis := Basis(Vector3.UP, _rng.randf() * TAU).scaled(Vector3.ONE * size)
	var from := at + Vector3.UP * LIFT
	var land := _land(from, velocity)
	var kink := INF
	var side: Vector3 = land.point
	if land.normal.y < 0.5:
		# A steep face (the end of a kerf, the board's side): down it to what is below.
		side = land.point + land.normal * MM
		var below := _ray(side, side + Vector3.DOWN)
		if not below.is_empty():
			kink = land.t
			land = {"point": below.position, "normal": below.normal, "collider": below.collider,
					"t": land.t + sqrt(2.0 * (side.y - below.position.y) / GRAVITY)}
	var ground: bool = land.normal.y > 0.5 and land.collider != null and land.collider.has_meta("ground")
	_fly_slot.push_back(slot)
	_fly_from.push_back(from)
	_fly_velocity.push_back(velocity)
	_fly_t.push_back(0.0)
	_fly_land.push_back(land.t)
	_fly_rest.push_back(land.point + Vector3.UP * (0.2 * size))
	_fly_kink.push_back(kink)
	_fly_side.push_back(side)
	_fly_basis.push_back(basis)
	_fly_step.push_back(step)
	_fly_wood.push_back(wood)
	_fly_colour.push_back(tint)
	_fly_ground.push_back(1 if ground else 0)
	if slot >= 0:
		multimesh.set_instance_color(slot, tint)
		multimesh.set_instance_transform(slot, Transform3D(basis, from))


## Where the arc from `from` at `velocity` first meets something: cast in three pieces,
## ending 5 mm, 30 mm and a metre below where it started. {"point", "normal", "collider",
## "t"}.
func _land(from: Vector3, velocity: Vector3) -> Dictionary:
	var t0 := 0.0
	var a := from
	for drop in [0.005, 0.03, 1.0]:
		var t1: float = (velocity.y + sqrt(velocity.y * velocity.y + 2.0 * GRAVITY * drop)) / GRAVITY
		var b: Vector3 = from + velocity * t1 + Vector3.DOWN * (0.5 * GRAVITY * t1 * t1)
		var hit := _ray(a, b)
		if not hit.is_empty():
			var along: float = (hit.position - a).length() / maxf((b - a).length(), 1e-9)
			return {"point": hit.position, "normal": hit.normal, "collider": hit.collider, "t": lerpf(t0, t1, along)}
		t0 = t1
		a = b
	return {"point": a, "normal": Vector3.UP, "collider": null, "t": t0}


func _ray(a: Vector3, b: Vector3) -> Dictionary:
	_query.from = a
	_query.to = b
	return get_world_3d().direct_space_state.intersect_ray(_query)


# --- piles ------------------------------------------------------------------------------

## A grain's wood reaches the ground at `point`: into the pile there (the nearest one it lies
## within reach of, on the same level), or a new one.
func _to_pile(point: Vector3, wood: float, colour: Color, step: int) -> void:
	var best := -1
	var nearest := INF
	for p in piles.size():
		var pile: Dictionary = piles[p]
		if absf(point.y - pile.base.y) > SAME_LEVEL + pile.height:
			continue
		var d := Vector2(point.x - pile.base.x, point.z - pile.base.z).length()
		if d <= maxf(pile.radius, REACH) and d < nearest:
			nearest = d
			best = p
	var pile: Dictionary
	if best >= 0:
		pile = piles[best]
		point.y = pile.base.y # (landing on the pile, it adds to what the pile stands on)
	else:
		var view := MeshInstance3D.new()
		view.mesh = _mound
		var look := StandardMaterial3D.new()
		look.roughness = 1.0
		view.material_override = look
		view.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(view)
		pile = {"view": view, "base": point, "by": {}, "total": 0.0, "radius": 0.0, "height": 0.0}
		piles.append(pile)
	var share: Array = pile.by.get(step, [0.0, Vector3.ZERO, Color(0, 0, 0, 0)])
	share[0] += wood
	share[1] += point * wood
	share[2] += colour * wood
	pile.by[step] = share
	_shape_pile(pile)


## A pile's mound for its wood (BULK times its volume): at the mean of where its dust landed,
## in the mean of its dust's colours.
func _shape_pile(pile: Dictionary) -> void:
	var wood := 0.0
	var at := Vector3.ZERO
	var colour := Color(0, 0, 0, 0)
	for share in pile.by.values():
		wood += share[0]
		at += share[1]
		colour += share[2]
	pile.total = wood
	pile.base = at / maxf(wood, 1e-9)
	# Profile (1 - r^2)^2: its volume is PI R^2 H / 3, with H = FLANK R.
	var room: float = BULK * wood * 1e-9 # m^3
	var radius := pow(3.0 * room / (PI * FLANK), 1.0 / 3.0)
	pile.radius = radius
	pile.height = FLANK * radius
	pile.view.transform = Transform3D(Basis.from_scale(Vector3(radius, pile.height, radius)),
			pile.base + Vector3.UP * 0.00005)
	var mean: Color = colour / maxf(wood, 1e-9)
	(pile.view.material_override as StandardMaterial3D).albedo_color = Color(mean.r, mean.g, mean.b)


## A mound of radius 1 and height 1: y = (1 - r^2)^2 over the unit disc.
func _mound_mesh() -> ArrayMesh:
	const RINGS := 10
	const AROUND := 24
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	var point := func(r: float, a: float) -> Vector3:
		return Vector3(r * cos(a), pow(1.0 - r * r, 2.0), r * sin(a))
	var normal := func(r: float, a: float) -> Vector3:
		var slope := -4.0 * r * (1.0 - r * r) # dy/dr
		return Vector3(-slope * cos(a), 1.0, -slope * sin(a)).normalized()
	for i in RINGS:
		var r0 := float(i) / RINGS
		var r1 := float(i + 1) / RINGS
		for j in AROUND:
			var a0 := TAU * j / AROUND
			var a1 := TAU * (j + 1) / AROUND
			# (Clockwise seen from above: Godot's front faces.)
			for v in [[r0, a0], [r1, a0], [r1, a1], [r0, a0], [r1, a1], [r0, a1]]:
				st.set_normal(normal.call(v[0], v[1]))
				st.add_vertex(point.call(v[0], v[1]))
	return st.commit()


func _hidden() -> Transform3D:
	return Transform3D(Basis.from_scale(Vector3.ZERO), Vector3.ZERO)
