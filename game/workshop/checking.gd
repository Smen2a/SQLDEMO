extends Node

## Checking a part against its drawing: the piece in the vise, laid out from a plan (its meta
## "part"), measured against the part as drawn (SdfBody.check_part, core plans/check). Where
## its surface is proud of the drawing (wood still there that the drawing takes away) or short
## of it (wood gone that the drawing keeps) by more than TOLERANCE, dots show on the wood
## (proud warm, short cool, bigger the further off: short ones float where the surface should
## be) and each place is named: the feature or the blank's face it is on, how far off, about
## how much wood. A guide only, with no score: unfinished work (a blank not yet cut to length,
## a mortise not yet chopped) shows as proud, what is left to do.
##
## A check stands until the wood changes (then it says so, and K checks again), the piece
## leaves the vise, or K clears it.

const TOLERANCE := 0.5 # mm
const PROUD_COLOUR := Color(1.0, 0.42, 0.1)
const SHORT_COLOUR := Color(0.2, 0.55, 1.0)
## The blank's faces by the outward normal's axis and sign.
const FACES := [["the end", "the far end"], ["the face edge", "the other edge"], ["the face side", "the back"]]
const FROM := ["from the end", "from the face edge", "from the face side"]

var workshop
## The latest check: {"piece", "sdf", "steps", "part", "spots" (SdfBody.check_part's, the worst
## first), "ms"}; or {"none": why there is none}; {} with none asked for.
var result := {}
## The wood has changed since it was checked.
var stale := false
var _dots: MultiMeshInstance3D


func _process(_delta: float) -> void:
	if result.has("piece") and (workshop.holder() != result.piece or not is_instance_valid(result.sdf)):
		clear()


## Checks the piece in the vise against its drawing; false where it is no part (result.none
## says why).
func check() -> bool:
	clear()
	var piece = workshop.holder()
	var sdf = workshop.board
	var tag: Dictionary = piece.get_meta("part", {}) if piece != null else {}
	var part: Dictionary = workshop.plans.part(tag.get("plan", ""), tag.get("part", "")) if not tag.is_empty() else {}
	if piece == null or sdf == null:
		result = {"none": "Nothing in the vise to check."}
	elif part.is_empty():
		result = {"none": "The %s is not laid out from a plan: nothing to check it against." % workshop._name_of(piece)}
	if result.has("none"):
		workshop._ui.refresh()
		return false
	var got: Dictionary = sdf.check_part(part, tag.placement, TOLERANCE)
	result = {"piece": piece, "sdf": sdf, "steps": sdf.get_stats().get("steps", 0), "part": part,
			"plan": workshop.plans.plans.get(tag.plan, {}), "spots": got.get("spots", []), "ms": got.get("ms", 0.0)}
	stale = false
	sdf.edited.connect(_on_edited)
	_draw_dots()
	workshop._ui.refresh()
	return true


## K: a check, or (with one showing) clears it.
func toggle() -> void:
	if result.has("spots") and not stale:
		clear()
		workshop._ui.refresh()
	else:
		check()


func clear() -> void:
	if result.has("sdf") and is_instance_valid(result.sdf) and result.sdf.edited.is_connected(_on_edited):
		result.sdf.edited.disconnect(_on_edited)
	result = {}
	stale = false
	if _dots != null and is_instance_valid(_dots):
		_dots.queue_free()
	_dots = null


func _on_edited(_stats) -> void:
	if not result.has("sdf") or not is_instance_valid(result.sdf):
		return
	if result.sdf.get_stats().get("steps", 0) != result.steps and not stale:
		stale = true
		if _dots != null and is_instance_valid(_dots):
			_dots.visible = false
		workshop._ui.refresh()


## The check in words: a line for each place (the worst first), or that it is as drawn.
func lines(most := 6) -> Array[String]:
	var out: Array[String] = []
	if result.has("none"):
		out.append(result.none)
		return out
	if not result.has("spots"):
		return out
	if stale:
		out.append("The wood has changed since it was checked: K to check again.")
		return out
	var spots: Array = result.spots
	if spots.is_empty():
		out.append("As drawn, to %s mm." % _mm(TOLERANCE))
	for i in mini(spots.size(), most):
		out.append(describe(spots[i]))
	if spots.size() > most:
		out.append("and %d more" % (spots.size() - most))
	return out


## A place off the drawing, in words: where it is, how far off, what that means.
func describe(spot: Dictionary) -> String:
	var proud: bool = spot.kind == "proud"
	var said := "%s: %s mm %s, %s" % [where(spot), _mm(spot.most), "proud" if proud else "short",
			"wood to take off" if proud else "wood gone past the drawing"]
	if spot.volume >= 100.0:
		said += " (about %s mm³)" % _thousands(spot.volume)
	return said


## Which part of the part a place is on: a feature (and how far its face is from the end, the
## face edge or the face side), or one of the blank's faces.
func where(spot: Dictionary) -> String:
	var n: Vector3 = spot.normal
	var axis := n.abs().max_axis_index()
	var features: Array = result.get("part", {}).get("features", [])
	var feature: int = spot.feature
	if feature < 0 or feature >= features.size():
		var face: String = FACES[axis][1 if n[axis] > 0.0 else 0]
		return face[0].to_upper() + face.substr(1)
	var name: String = features[feature].get("name", features[feature].get("kind", "feature"))
	if absf(n[axis]) < 0.9:
		return "The " + name # (a sloping face: a chamfer, a taper)
	var at: Vector3 = spot.at_part
	return "The %s, %s mm %s" % [name, _mm(at[axis]), FROM[axis]]


## Dots on the wood where it is off: a MultiMesh under the work's SdfBody (its own body space,
## millimetres).
func _draw_dots() -> void:
	var sdf = result.sdf
	var count := 0
	for spot in result.spots:
		count += spot.dots.size()
	if count == 0:
		return
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	sphere.radial_segments = 8
	sphere.rings = 4
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.vertex_color_use_as_albedo = true
	sphere.material = material
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.use_colors = true
	multi.mesh = sphere
	multi.instance_count = count
	var i := 0
	for spot in result.spots:
		var colour: Color = PROUD_COLOUR if spot.kind == "proud" else SHORT_COLOUR
		var dots: PackedVector3Array = spot.dots
		var by: PackedFloat32Array = spot.by
		for d in dots.size():
			var r := clampf(0.45 + 0.2 * by[d], 0.6, 1.5)
			multi.set_instance_transform(i, Transform3D(Basis.from_scale(Vector3.ONE * r), dots[d]))
			multi.set_instance_color(i, colour)
			i += 1
	_dots = MultiMeshInstance3D.new()
	_dots.multimesh = multi
	_dots.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	sdf.add_child(_dots)


func dot_count() -> int:
	return _dots.multimesh.instance_count if _dots != null and is_instance_valid(_dots) and _dots.visible else 0


static func _mm(v: float) -> String:
	return ("%.0f" % v) if absf(v - roundf(v)) < 0.05 else ("%.1f" % v)


static func _thousands(v: float) -> String:
	var whole := str(int(roundf(v / 10.0) * 10.0)) if v >= 1000.0 else str(int(roundf(v)))
	var out := ""
	for i in whole.length():
		if i > 0 and (whole.length() - i) % 3 == 0:
			out += ","
		out += whole[i]
	return out
