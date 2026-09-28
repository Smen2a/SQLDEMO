extends Node

## Plans: what the parts of an object are meant to be (game/plans/*.json), and laying a part
## out on the wood. A plan has parts (each a rectangular blank with features cut into it: a
## mortise, a tenon, a kerf, a rebate, a chamfer, a taper; see core plans/part.h) and the
## joints between them.
##
## The sheet in hand (the Layout slot's "plan sheet") is laid on the piece in the vise: the
## face hovered takes the part's face side, the end nearest the pointer its reference end,
## and that face's edge nearest the pointer its face edge. A click draws the part on in
## pencil (SdfBody.part_lines: its size, then its features, on every face the piece has) and
## the piece becomes that part (its meta "part": {plan, part, placement: part space to body
## space}). Pencil only guides: the knife and gauge snap onto it (layout.gd), and their lines
## stop the tools. With "scribe as you lay out" (settings.layout.scribe) the lines are
## scribed at once instead, one undo step.
##
## Pencil is not an edit of the body: what it draws is undone by the workshop's undo straight
## after (the piece's meta "pencil_log"), before any cut.

const PLANS_DIR := "res://plans"

var workshop
## Plans by id (the file's name): the parsed JSON ({name, about, parts, joints}).
var plans := {}
## The sheet in hand: {"plan", "part"} (ids), or {}.
var sheet := {}
var _serial := 0


func _ready() -> void:
	load_plans()


func load_plans() -> void:
	plans = {}
	var dir := DirAccess.open(PLANS_DIR)
	if dir == null:
		return
	for file in dir.get_files():
		if not file.ends_with(".json"):
			continue
		var text := FileAccess.get_file_as_string(PLANS_DIR + "/" + file)
		var plan = JSON.parse_string(text)
		if plan is Dictionary:
			plans[file.get_basename()] = plan
		else:
			push_error("plans: %s is not a plan" % file)


## A plan's part by id, or {}.
func part(plan_id: String, part_id: String) -> Dictionary:
	for p in plans.get(plan_id, {}).get("parts", []):
		if p.id == part_id:
			return p
	return {}


## The part on the sheet in hand, or {}.
func sheet_part() -> Dictionary:
	return {} if sheet.is_empty() else part(sheet.plan, sheet.part)


## Takes a part's sheet in hand: the Layout slot's plan sheet.
func take_sheet(plan_id: String, part_id: String) -> void:
	if part(plan_id, part_id).is_empty():
		return
	sheet = {"plan": plan_id, "part": part_id}
	workshop.set_setting("layout", "variant", "plan")


## A part's size (L, W, T mm).
static func size_of(p: Dictionary) -> Vector3:
	var s: Array = p.get("size", [0, 0, 0])
	return Vector3(s[0], s[1], s[2])


## The sheet in hand laid on the piece in the vise at a point (world) on a face (its normal):
## {"placement" (part space to body space), "stock" (the piece's size along the part's axes),
## "fits", "marks" (its pencil lines, body space: layout.gd's marks), "later" (lines waiting
## for faces the piece does not have yet)}; {"on_end": true} on an end; {} off the piece or
## with no sheet.
func placement_at(point: Vector3, normal: Vector3) -> Dictionary:
	var sdf = workshop.board
	var p := sheet_part()
	if sdf == null or p.is_empty():
		return {}
	var layout = workshop.layout
	var bounds: AABB = sdf.get_body_bounds()
	var lo := bounds.position
	var hi := bounds.end
	var q: Vector3 = layout.to_body(point)
	var n: Vector3 = layout.dir_to_body(normal)
	var i := 0
	for axis in 3:
		if absf(n[axis]) > absf(n[i]):
			i = axis
	if i == 0:
		return {"on_end": true} # (the grain runs along the part: laid on a face or an edge)
	var across := 3 - i # the face's other axis (y or z)
	var corner := Vector3.ZERO
	var near_lo := q.x - lo.x <= hi.x - q.x
	corner.x = lo.x if near_lo else hi.x
	var a_x := Vector3.RIGHT if near_lo else Vector3.LEFT
	var edge_lo: bool = q[across] - lo[across] <= hi[across] - q[across]
	corner[across] = lo[across] if edge_lo else hi[across]
	var a_y := Vector3.ZERO
	a_y[across] = 1.0 if edge_lo else -1.0
	corner[i] = hi[i] if n[i] > 0.0 else lo[i]
	var a_z := Vector3.ZERO
	a_z[i] = -1.0 if n[i] > 0.0 else 1.0
	var placement := Transform3D(Basis(a_x, a_y, a_z), corner)
	var stock := Vector3(hi.x - lo.x, hi[across] - lo[across], hi[i] - lo[i])
	var size := size_of(p)
	var fits := stock.x >= size.x - 0.05 and stock.y >= size.y - 0.05 and stock.z >= size.z - 0.05
	var laid: Dictionary = sdf.part_lines(p, stock.max(size))
	var marks := []
	for line in laid.lines:
		marks.append(_mark_of(line, placement))
	return {"placement": placement, "stock": stock, "fits": fits, "marks": marks, "later": laid.later, "part": p}


## A part-space line (SdfBody.part_lines) placed on the piece: a pencil mark, as layout.gd
## keeps marks (body space), `as` how it holds once knifed or gauged in.
static func _mark_of(line: Dictionary, placement: Transform3D) -> Dictionary:
	var b := placement.basis
	var mark := {"kind": "pencil", "as": line.as, "face": (b * line.face).round(), "origin": placement * line.origin,
			"dir": (b * line.dir).normalized(), "length": line.length, "toward": (b * line.toward).normalized(),
			"distance": line.distance, "from": 0.0, "to": line.length, "feature": line.feature}
	if line.as == "gauge":
		# Where the edge it is gauged from lies, along `toward` (as layout.gd's gauge lines).
		var toward: Vector3 = mark.toward
		var axis := toward.abs().max_axis_index()
		mark.edge = (mark.origin + toward * line.distance)[axis]
	return mark


## Draws the sheet in hand on the piece in the vise as placed (placement_at()): pencil lines,
## undone by undo straight after; or, with settings.layout.scribe, scribed at once (one undo
## step). The piece becomes that part. False where it does not fit.
func transfer(placed: Dictionary) -> bool:
	var piece: RigidBody3D = workshop.clamped
	var sdf = workshop.board
	if piece == null or sdf == null or placed.is_empty() or not placed.get("fits", false):
		return false
	var steps: int = sdf.get_stats().get("steps", 0)
	var marks: Array = placed.marks.duplicate(true)
	var scribed: bool = workshop.settings.layout.get("scribe", false)
	_serial += 1
	if scribed:
		var lines := marks.filter(func(m): return m.as != "guide")
		if lines.is_empty() or not sdf.scribe_lines(lines, workshop.layout.SCRIBE_DEPTH):
			scribed = false
	for mark in marks:
		mark.serial = _serial
		mark.step = steps + 1 if scribed else steps
		if scribed and mark.as != "guide":
			mark.kind = mark.as
	var layout = workshop.layout
	layout.marks_of(piece).append_array(marks)
	if scribed:
		layout.forget_redo(piece)
	_log(piece, {"serial": _serial, "step": steps + 1 if scribed else steps, "scribed": scribed,
			"part_before": piece.get_meta("part", {})})
	piece.set_meta("part", {"plan": sheet.plan, "part": sheet.part, "placement": placed.placement})
	workshop._ui.refresh()
	return true


## A line drawn in pencil on the piece in the vise (layout.gd's pencil): kept as a mark,
## undone by undo straight after.
func pencil(mark: Dictionary) -> void:
	var piece: RigidBody3D = workshop.clamped
	var sdf = workshop.board
	if piece == null or sdf == null:
		return
	_serial += 1
	var steps: int = sdf.get_stats().get("steps", 0)
	mark.serial = _serial
	mark.step = steps
	workshop.layout.marks_of(piece).append(mark)
	_log(piece, {"serial": _serial, "step": steps, "scribed": false, "part_before": piece.get_meta("part", {})})
	workshop._ui.refresh()


func _log(piece: Object, entry: Dictionary) -> void:
	var entries: Array = piece.get_meta("pencil_log", [])
	entries.append(entry)
	piece.set_meta("pencil_log", entries)


## Undo, where the piece's latest action was drawing: pencil straight after takes its lines
## off (and the part it made the piece, if it was a sheet laid on) and says so (the workshop
## then undoes nothing else). A sheet scribed on is its own undo step: the part goes with it,
## and the workshop undoes the step as any other (false).
func undo(piece: Object, steps: int) -> bool:
	if piece == null:
		return false
	var entries: Array = piece.get_meta("pencil_log", [])
	if entries.is_empty() or entries.back().step != steps:
		return false
	var entry: Dictionary = entries.pop_back()
	piece.set_meta("pencil_log", entries)
	if entry.part_before.is_empty():
		piece.remove_meta("part")
	else:
		piece.set_meta("part", entry.part_before)
	if entry.scribed:
		return false
	var layout = workshop.layout
	piece.set_meta("marks", layout.marks_of(piece).filter(func(m): return m.get("serial", -1) != entry.serial))
	workshop._ui.refresh()
	return true


## A plan's parts laid out: {part id: how many pieces are that part}.
func status(plan_id: String) -> Dictionary:
	var out := {}
	for p in plans.get(plan_id, {}).get("parts", []):
		out[p.id] = 0
	for piece in workshop.pieces:
		if is_instance_valid(piece) and piece.has_meta("part"):
			var tag: Dictionary = piece.get_meta("part")
			if tag.plan == plan_id and out.has(tag.part):
				out[tag.part] += 1
	return out
