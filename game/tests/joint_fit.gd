extends Node

## A joint's fit through the extension (SdfBody.joint_pose, SdfBody.fit, core pieces/fit): the
## mallet's head and handle as drawn (the plan's parts, SdfBody.load_part), the handle offered
## to the head's mortise at the joint's mouth and pushed along its axis:
## - the joint: the handle goes in, 58 mm, into the head's face side;
## - as drawn: snug, home, seated on its shoulder;
## - a handle drawn with its tenon 0.2 mm fatter: it drives home (0.1 mm into the mortise's
##   sides); 1.2 mm fatter: it won't go past the mouth.

var _failed := false


func _ready() -> void:
	var plans := preload("res://workshop/plans.gd").new()
	add_child(plans)
	var head: Dictionary = plans.part("mallet", "head")
	var handle: Dictionary = plans.part("mallet", "handle")
	var joint: Dictionary = SdfBody.joint_pose(head, "mortise", handle, "tenon")
	_check(joint.get("kind", "") == "mortise and tenon" and joint.get("moving", "") == "b" and
			absf(joint.get("travel", 0.0) - 58.0) < 1e-3 and joint.get("axis", Vector3.ZERO).is_equal_approx(Vector3(0, 0, 1)),
			"the handle goes 58 mm into the head's face side: %s" % [joint])
	var mortised := _body(head, "oak")
	for grow in [0.0, 0.2, 1.2]:
		var fat: Dictionary = handle.duplicate(true)
		fat.features[0].y = [2.5 - 0.5 * grow, 32.5 + 0.5 * grow]
		var part := _body(fat, "ash")
		var start: Transform3D = joint.home.translated(-joint.axis * joint.travel)
		var fit: Dictionary = part.fit(mortised, start, joint.axis, joint.travel, 0.5, joint.region)
		print("joint fit: a tenon %.1f mm fat: %s, %.1f mm in%s, %.2f mm most, %.2f clearance, %d points, %.0f ms" % [
				grow, fit.get("kind", "?"), fit.get("stops_at", 0.0), " (home, seated)" if fit.get("seated", false) else "",
				fit.get("most", 0.0), fit.get("clearance", 0.0), fit.get("points", 0), fit.get("ms", 0.0)])
		match grow:
			0.0:
				_check(fit.kind == "snug" and fit.home and fit.seated, "as drawn: snug, home")
			0.2:
				_check(fit.kind == "drives" and fit.home and absf(fit.most - 0.1) < 0.03, "0.2 mm fat: it drives home")
			1.2:
				_check(fit.kind == "won't go" and not fit.home and fit.stops_at <= 1.0, "1.2 mm fat: it won't go")
		part.queue_free()
	print("joint fit: %s" % ("the handle fits the head" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A part as drawn, in a wood, in its own part space.
func _body(part: Dictionary, wood: String) -> SdfBody:
	var body := SdfBody.new()
	add_child(body)
	_check(body.load_part(part, wood), "loads %s" % part.get("id", "?"))
	return body


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("joint fit: " + what)
