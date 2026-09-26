extends MultiMeshInstance3D

## Dust: grains of the wood the saw, the rasps, the scraper and the sanding tools take off
## (the board's take_debris() "dust"; debris.gd owns this). Cosmetic: nothing collides
## with it.
##   - A puff throws up to PUFF grains, bigger when it carries more wood (dust takes BULK
##     times the room the wood did), up to WIDEST across (beyond that the pile grows taller
##     instead), PALER than the wood (dust scatters light).
##   - A grain flies from where it left the work, the way it was thrown with a little
##     scatter, under gravity. Where it lands is found once, when it is thrown: its arc
##     cast in three pieces against the physics space (the bench, the floor, the board's
##     collider, pieces lying about).
##   - Grains that land on something facing up stack on a CELL grid over the world. One
##     landing on a pile rolls to a lower neighbouring cell while the pile is steeper than 45
##     degrees (or off an edge): sawdust heaps up under the kerf's ends, sanding dust lies on
##     the face.
##   - At most GRAINS are kept, in one MultiMesh; the oldest go first.
## World space throughout (metres); wood volumes are mm^3.

const MM := 0.001
const GRAINS := 30000
const PUFF := 16
const BULK := 2.5
const PALER := 0.15
const WIDEST := 0.0025 # m: the most a grain is drawn across (more wood: a taller pile, not boulders)
const CELL := 0.001 # m: the grid grains stack on
const ROLLS := 4    # cells a grain may roll on landing
const THROW := 0.08 # m/s along the puff's direction
const SCATTER := 0.04 # m/s at random
const LIFT := 0.0005 # m above the work a grain starts from
const GRAVITY := 9.8
const PER_FRAME := 20 # grains thrown a frame at most (the rest wait for the next)
const AROUND: Array[Vector2i] = [Vector2i(1, 0), Vector2i(1, 1), Vector2i(0, 1), Vector2i(-1, 1), Vector2i(-1, 0),
		Vector2i(-1, -1), Vector2i(0, -1), Vector2i(1, -1)]

var volume := 0.0 ## mm^3 of wood in the grains kept
var update_usec := 0 ## what the last frame's flights cost
var _count := 0 # slots in use (up to GRAINS)
var _next := 0  # the slot the next grain takes (the oldest, once all are in use)
var _step := PackedInt32Array() # per slot: the board's undo step that made it (-1: none)
var _wood := PackedFloat32Array() # mm^3 of wood it stands for
var _rest := PackedVector3Array() # where it lies (or will, once it lands)
var _height := PackedFloat32Array() # m it adds to the pile it lies on
var _size := PackedFloat32Array() # m across
# Grains in flight, in step: slot, where from, how fast, how long in the air, when they land,
# their turn and size.
var _fly_slot := PackedInt32Array()
var _fly_from := PackedVector3Array()
var _fly_velocity := PackedVector3Array()
var _fly_t := PackedFloat32Array()
var _fly_land := PackedFloat32Array()
var _fly_basis: Array[Basis] = []
var _tops := {} # Vector2i cell -> the top of what lies there (m); probed surfaces too
var _rng := RandomNumberGenerator.new()
var _query := PhysicsRayQueryParameters3D.new()
# Grains waiting to be thrown: [at, direction, size, wood, colour, step] each.
var _waiting: Array[Array] = []


func _ready() -> void:
	var grains := MultiMesh.new()
	grains.transform_format = MultiMesh.TRANSFORM_3D
	grains.use_colors = true
	grains.mesh = BoxMesh.new()
	(grains.mesh as BoxMesh).size = Vector3(1.0, 0.25, 1.0) # a flake, scaled per grain
	grains.instance_count = GRAINS
	grains.visible_instance_count = 0
	multimesh = grains
	var look := StandardMaterial3D.new()
	look.vertex_color_use_as_albedo = true
	look.roughness = 1.0
	material_override = look
	cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_step.resize(GRAINS)
	_step.fill(-1)
	_wood.resize(GRAINS)
	_rest.resize(GRAINS)
	_height.resize(GRAINS)
	_size.resize(GRAINS)
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
		for k in n:
			var off := Vector2.from_angle(_rng.randf() * TAU) * spread * sqrt(_rng.randf())
			_waiting.append([dust.points[i] + Vector3(off.x, 0.0, off.y), direction, size, wood / n, colour, step])
		volume += wood # counted now: it is on its way
	_throw_some()


## Grains in flight, or waiting to be thrown.
func flying() -> int:
	return _fly_slot.size() + _waiting.size()


## The grains lying in `box` (world): {"count", "volume" (mm^3 of wood), "top" (the
## highest, m)}.
func lying_in(box: AABB) -> Dictionary:
	var count := 0
	var wood := 0.0
	var top := -INF
	var moving := {}
	for slot in _fly_slot:
		moving[slot] = true
	for slot in _count:
		if _step[slot] >= 0 and not moving.has(slot) and box.has_point(_rest[slot]):
			count += 1
			wood += _wood[slot]
			top = maxf(top, _rest[slot].y)
	return {"count": count, "volume": wood, "top": top}


## Takes back the grains the board's undo step `step` made (and any after it).
func undo_step(step: int) -> void:
	for i in range(_waiting.size() - 1, -1, -1):
		if _waiting[i][5] >= step:
			volume -= _waiting[i][3]
			_waiting.remove_at(i)
	for slot in _count:
		if _step[slot] >= step:
			_forget(slot)
	for i in range(_fly_slot.size() - 1, -1, -1):
		if _step[_fly_slot[i]] < 0:
			_land_now(i, false)
	_restack()


## Grains lying in `box` (world) whose support has gone (a piece they lay on slid away) fall
## again.
func resettle(box: AABB) -> void:
	for slot in _count:
		if _step[slot] < 0 or not box.has_point(_rest[slot]):
			continue
		var at: Vector3 = _rest[slot]
		if not _ray(at + Vector3.UP * MM, at + Vector3.DOWN * MM).is_empty():
			continue
		var basis := Basis(Vector3.UP, _rng.randf() * TAU).scaled(Vector3.ONE * _size[slot])
		_fly(slot, at, Vector3.ZERO, basis)
	_restack()


## Sweeps it all away.
func clear() -> void:
	for slot in _count:
		_forget(slot)
	_count = 0
	_next = 0
	volume = 0.0
	_waiting.clear()
	for i in range(_fly_slot.size() - 1, -1, -1):
		_land_now(i, false)
	_tops.clear()
	multimesh.visible_instance_count = 0


func _process(delta: float) -> void:
	if not _waiting.is_empty():
		_throw_some()
	if _fly_slot.is_empty():
		return
	var t0 := Time.get_ticks_usec()
	for i in range(_fly_slot.size() - 1, -1, -1):
		var t: float = _fly_t[i] + delta
		if t >= _fly_land[i]:
			_land_now(i, true)
			continue
		_fly_t[i] = t
		var at: Vector3 = _fly_from[i] + _fly_velocity[i] * t + Vector3.DOWN * (0.5 * GRAVITY * t * t)
		multimesh.set_instance_transform(_fly_slot[i], Transform3D(_fly_basis[i], at))
	update_usec = Time.get_ticks_usec() - t0


## The flight `i` ends: its grain lies where it landed (`show`), and the flight goes (the
## last one takes its place).
func _land_now(i: int, show: bool) -> void:
	if show:
		multimesh.set_instance_transform(_fly_slot[i], Transform3D(_fly_basis[i], _rest[_fly_slot[i]]))
	var last := _fly_slot.size() - 1
	_fly_slot[i] = _fly_slot[last]
	_fly_from[i] = _fly_from[last]
	_fly_velocity[i] = _fly_velocity[last]
	_fly_t[i] = _fly_t[last]
	_fly_land[i] = _fly_land[last]
	_fly_basis[i] = _fly_basis[last]
	_fly_slot.resize(last)
	_fly_from.resize(last)
	_fly_velocity.resize(last)
	_fly_t.resize(last)
	_fly_land.resize(last)
	_fly_basis.resize(last)


func _throw_some() -> void:
	for k in mini(PER_FRAME, _waiting.size()):
		var w: Array = _waiting.pop_front()
		_throw(w[0], w[1], w[2], w[3], w[4], w[5])


func _throw(at: Vector3, direction: Vector3, size: float, wood: float, colour: Color, step: int) -> void:
	var slot := _next
	_next = (_next + 1) % GRAINS
	if _step[slot] >= 0:
		_forget(slot) # the oldest goes
	_count = maxi(_count, slot + 1)
	multimesh.visible_instance_count = _count
	_step[slot] = step
	_wood[slot] = wood
	var shade := _rng.randf_range(0.88, 1.04) # no two grains quite alike
	multimesh.set_instance_color(slot, Color(colour.r * shade, colour.g * shade, colour.b * shade))
	var scatter := Vector3(_rng.randf_range(-1, 1), _rng.randf_range(0, 1), _rng.randf_range(-1, 1))
	var velocity := direction * THROW * _rng.randf_range(0.5, 1.5) + scatter * SCATTER
	var basis := Basis(Vector3.UP, _rng.randf() * TAU).scaled(Vector3.ONE * size)
	_height[slot] = BULK * wood * 1e-9 / (CELL * CELL)
	_size[slot] = size
	_fly(slot, at + Vector3.UP * LIFT, velocity, basis)


## Sends the grain in `slot` flying from `from`: where it lands, found now, and it lies there
## once its flight is over.
func _fly(slot: int, from: Vector3, velocity: Vector3, basis: Basis) -> void:
	var land := _land(from, velocity)
	_rest[slot] = _pile(land.point, land.normal, _size[slot], slot)
	_fly_slot.push_back(slot)
	_fly_from.push_back(from)
	_fly_velocity.push_back(velocity)
	_fly_t.push_back(0.0)
	_fly_land.push_back(land.t)
	_fly_basis.push_back(basis)
	multimesh.set_instance_transform(slot, Transform3D(basis, from))


## Where the arc from `from` at `velocity` first meets something: cast in three pieces,
## ending 5 mm, 30 mm and a metre below where it started. {"point", "normal", "t"}.
func _land(from: Vector3, velocity: Vector3) -> Dictionary:
	var t0 := 0.0
	var a := from
	for drop in [0.005, 0.03, 1.0]:
		var t1: float = (velocity.y + sqrt(velocity.y * velocity.y + 2.0 * GRAVITY * drop)) / GRAVITY
		var b: Vector3 = from + velocity * t1 + Vector3.DOWN * (0.5 * GRAVITY * t1 * t1)
		var hit := _ray(a, b)
		if not hit.is_empty():
			var along: float = (hit.position - a).length() / maxf((b - a).length(), 1e-9)
			return {"point": hit.position, "normal": hit.normal, "t": lerpf(t0, t1, along)}
		t0 = t1
		a = b
	return {"point": a, "normal": Vector3.UP, "t": t0}


## Where a grain landing at `point` (on a face whose normal is `normal`) comes to lie: down
## a steep face to what is under it, then on the pile in its cell, rolling to a lower
## neighbour while the pile is steeper than 45 degrees.
func _pile(point: Vector3, normal: Vector3, size: float, slot: int) -> Vector3:
	if normal.y < 0.5:
		var below := _ray(point + normal * MM, point + normal * MM + Vector3.DOWN)
		if below.is_empty():
			return point
		point = below.position
	var cell := Vector2i(floori(point.x / CELL), floori(point.z / CELL))
	var top: float = maxf(point.y, _tops.get(cell, point.y))
	# On bare ground it stays; on a pile it rolls while there is somewhere lower to go.
	var rolls := ROLLS if top > point.y + CELL else 0
	for _roll in rolls:
		# To the first neighbour lower by more than a cell's width, looked for from a random
		# side (the lowest would cost every neighbour's look every time).
		var first := _rng.randi() % 8
		var rolled := false
		for k in 8:
			var next: Vector2i = cell + AROUND[(first + k) % 8]
			var there := _top(next, top)
			if there < top - CELL:
				cell = next
				top = there
				rolled = true
				break
		if not rolled:
			break
	_tops[cell] = top + _height[slot]
	return Vector3((cell.x + _rng.randf()) * CELL, top + 0.2 * size, (cell.y + _rng.randf()) * CELL)


## The top of what lies in `cell`: its pile, or the surface under it (probed from `above`
## down, and kept); far below where there is nothing.
func _top(cell: Vector2i, above: float) -> float:
	if _tops.has(cell):
		return _tops[cell]
	var x := (cell.x + 0.5) * CELL
	var z := (cell.y + 0.5) * CELL
	var hit := _ray(Vector3(x, above + CELL, z), Vector3(x, above - 1.0, z))
	var surface: float = hit.position.y if not hit.is_empty() else above - 1.0
	_tops[cell] = surface
	return surface


func _ray(a: Vector3, b: Vector3) -> Dictionary:
	_query.from = a
	_query.to = b
	return get_world_3d().direct_space_state.intersect_ray(_query)


## The grain in `slot` goes.
func _forget(slot: int) -> void:
	if _step[slot] < 0:
		return
	volume -= _wood[slot]
	_step[slot] = -1
	_wood[slot] = 0.0
	multimesh.set_instance_transform(slot, Transform3D(Basis.from_scale(Vector3.ZERO), Vector3.ZERO))


## The piles again from the grains lying about (after some went).
func _restack() -> void:
	var tops := {}
	for slot in _count:
		if _step[slot] < 0:
			continue
		var at: Vector3 = _rest[slot]
		var cell := Vector2i(floori(at.x / CELL), floori(at.z / CELL))
		tops[cell] = maxf(tops.get(cell, -INF), at.y + _height[slot])
	_tops = tops
