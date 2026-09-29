extends "res://tests/harness.gd"

## The pad (workshop/plan_editor.gd), driven through its own methods and its fields' signals
## (headless, the window is too small to lay the pad out, so nothing goes by screen points):
## - E on the pad on the side table opens it on a new plan;
## - the mallet drawn on it as a drag's release would draw it (feature_from, add_feature):
##   the head (110 x 70 x 55 oak) with its mortise; a stray hole picked and taken off with
##   Delete; the handle (300 x 35 x 28 ash), its tenon's section on the end, its length set in
##   the tenon's own field, its kerf from the end along the face side; the wedge (45 x 12 x 5
##   oak) tapered along the face edge. Each part laid out in the preset's lines;
## - what each tool makes of a drag, and of one it can't use;
## - joined (the tenon in the mortise, the wedge in the kerf) and saved: the file in
##   user://plans, each part naming its stock, in the plan book as the player's;
## - the head's sheet taken and laid on a head blank: 12 pencil lines;
## - edited from the book, put down with P: the file as it was, the pad keeping the change
##   (and going on with it); thrown away;
## - a copy of the preset saved under its own id, the preset untouched; deleted (the second
##   press), and the plain mallet too.

const Workshop := preload("res://workshop/workshop.tscn")
const SheetView := preload("res://workshop/sheet_view.gd")
const IDS := ["plain_mallet", "mallet_copy"]

var workshop
var editor
var _failed := false


func _ready() -> void:
	get_tree().create_timer(300.0).timeout.connect(func():
		push_error("plan editor: timed out")
		get_tree().quit(1))
	for id in IDS:
		DirAccess.remove_absolute("user://plans/%s.json" % id)
	workshop = Workshop.instantiate()
	add_child(workshop)
	await _frames(3)
	var plans = workshop.plans
	var preset_text := FileAccess.get_file_as_string("res://plans/mallet.json")

	# At the side table, E on the pad opens it on a new plan.
	var player = workshop.player
	var table: Vector3 = workshop.room.TABLE_AT
	player.global_position = Vector3(table.x, player.global_position.y, table.z + 0.6)
	player.face(workshop.room.pad.get_child(0).global_position) # (the pad's box: its body lies at the origin)
	await _frames(2)
	_check(workshop.prompt() == "E: draw a plan", "the pad offers to draw a plan: " + workshop.prompt())
	_key(KEY_E)
	await _frames(2)
	editor = workshop._ui._viewer
	if not workshop._ui.editor_open():
		_check(false, "E on the pad opens it")
		get_tree().quit(1)
		return
	_check(editor.plan.parts.size() == 1 and editor.plan_id == "" and not editor.saved, "a new plan, one part")

	# The head: named and sized in its fields.
	_text(editor._name, "Plain mallet")
	_text(editor._part_name, "Head")
	_sizes(Vector3(110, 70, 55))
	var head: Dictionary = editor.part()
	_check(editor.plan.name == "Plain mallet" and head.name == "Head" and head.id == "head" and head.wood == "oak" and
			SheetView.size_of(head) == Vector3(110, 70, 55), "the head: oak, 110 x 70 x 55 (%s %s)" % [head.id, head.size])
	_check(editor._stock.text.contains("mallet-head blank"), "cut from a mallet-head blank: " + editor._stock.text)

	# Its mortise, dragged on the face side 40-70 from the end, 29-41 from the face edge.
	_draw("hole", SheetView.FACE, Vector2(40, 29), Vector2(70, 41))
	var mortise: Dictionary = head.features[0] if head.features.size() == 1 else {}
	_check(mortise.get("kind", "") == "hole" and mortise.get("name", "") == "mortise" and mortise.get("face", "") == "side" and
			mortise.get("along", []) == [40.0, 70.0] and mortise.get("across", []) == [29.0, 41.0] and mortise.get("through", false),
			"the mortise drawn: %s" % mortise)
	# A stray hole: picked by a click in it, taken off with Delete.
	_draw("hole", SheetView.FACE, Vector2(80, 10), Vector2(95, 20))
	editor.set_tool("pick")
	editor.pick({"view": SheetView.FACE, "at": Vector2(88, 15)})
	_check(editor.picked == 1 and head.features.size() == 2, "a click in the stray hole picks it (%d)" % editor.picked)
	_key_to_editor(KEY_DELETE)
	_check(head.features.size() == 1 and head.features[0].name == "mortise" and editor.picked == -1, "Delete takes it off")
	editor.pick({"view": SheetView.FACE, "at": Vector2(5, 60)})
	_check(editor.picked == -1, "a click off the features picks none")
	_check(_same_lines(head, plans.part("mallet", "head")), "the head laid out in the preset head's lines")

	# The handle: the tenon's section on the end, its length in its field; the kerf from the
	# end along the face side.
	editor.add_part()
	_check(editor.plan.parts.size() == 2 and editor.part_index == 1, "a second part")
	_text(editor._part_name, "Handle")
	_choose(editor._wood, "ash")
	_sizes(Vector3(300, 35, 28))
	var handle: Dictionary = editor.part()
	_draw("tenon", SheetView.END, Vector2(8, 2.5), Vector2(20, 32.5))
	var length := _field("long")
	_check(length != null and length.value == 40.0, "the tenon's length field, at 40 as drawn on the end")
	if length != null:
		length.value = 58.0
	var tenon: Dictionary = handle.features[0] if handle.features.size() == 1 else {}
	_check(tenon.get("kind", "") == "tenon" and tenon.get("end", "") == "end" and tenon.get("length", 0.0) == 58.0 and
			tenon.get("y", []) == [2.5, 32.5] and tenon.get("z", []) == [8.0, 20.0], "the tenon drawn: %s" % tenon)
	_draw("kerf", SheetView.FACE, Vector2(0, 17.5), Vector2(38, 17.5))
	var kerf: Dictionary = handle.features[1] if handle.features.size() == 2 else {}
	_check(kerf.get("kind", "") == "kerf" and kerf.get("end", "") == "end" and kerf.get("depth", 0.0) == 38.0 and
			kerf.get("axis", "") == "y" and kerf.get("at", 0.0) == 17.5, "the kerf drawn: %s" % kerf)
	_check(handle.id == "handle" and handle.wood == "ash" and editor._stock.text.contains("handle blank"),
			"the handle: ash, from a handle blank (%s)" % editor._stock.text)
	_check(_same_lines(handle, plans.part("mallet", "handle")), "the handle laid out in the preset handle's lines")

	# The wedge: a line along the face edge, leaving 5 mm at the end and 1 at the far end.
	editor.add_part()
	_text(editor._part_name, "Wedge")
	_sizes(Vector3(45, 12, 5))
	var wedge: Dictionary = editor.part()
	_draw("taper", SheetView.EDGE, Vector2(0, 5), Vector2(45, 1))
	var taper: Dictionary = wedge.features[0] if wedge.features.size() == 1 else {}
	_check(taper.get("kind", "") == "taper" and taper.get("face", "") == "back" and taper.get("from", 0.0) == 5.0 and
			taper.get("to", 0.0) == 1.0, "the taper drawn: %s" % taper)
	_check(editor._stock.text.contains("strip"), "cut from a strip: " + editor._stock.text)
	_check(_same_lines(wedge, plans.part("mallet", "wedge")), "the wedge laid out in the preset wedge's lines")

	# What each tool makes of a drag (on the wedge), and of one it can't use.
	var e = editor
	_check(e.feature_from("hole", SheetView.END, Vector2(1, 1), Vector2(4, 8)).is_empty(), "no hole in the end")
	_check(e.feature_from("tenon", SheetView.FACE, Vector2(10, 2), Vector2(20, 8)).is_empty(), "a tenon starts at an end")
	var rebate: Dictionary = e.feature_from("rebate", SheetView.END, Vector2(0, 0), Vector2(2, 4))
	_check(rebate.get("face", "") == "side" and rebate.get("other", "") == "edge" and rebate.get("width", 0.0) == 4.0 and
			rebate.get("depth", 0.0) == 2.0, "a rebate in the end's corner: %s" % rebate)
	var far: Dictionary = e.feature_from("rebate", SheetView.END, Vector2(5, 12), Vector2(3, 9))
	_check(far.get("face", "") == "back" and far.get("other", "") == "other_edge", "and in the far corner: %s" % far)
	var chamfer: Dictionary = e.feature_from("chamfer", SheetView.END, Vector2(0, 0), Vector2(1.5, 1))
	_check(chamfer.get("face", "") == "side" and chamfer.get("other", "") == "edge" and chamfer.get("width", 0.0) == 1.5,
			"a chamfer from the corner, as wide as the drag: %s" % chamfer)
	var off_side: Dictionary = e.feature_from("taper", SheetView.EDGE, Vector2(0, 0), Vector2(45, 3))
	_check(off_side.get("face", "") == "side" and off_side.get("from", 0.0) == 5.0 and off_side.get("to", 0.0) == 2.0,
			"a taper nearer the side, off the side: %s" % off_side)
	var across: Dictionary = e.feature_from("kerf", SheetView.END, Vector2(0, 6), Vector2(5, 6))
	_check(across.get("axis", "") == "y" and across.get("at", 0.0) == 6.0, "a kerf across the end: %s" % across)
	_check(e.feature_from("taper", SheetView.FACE, Vector2(0, 5), Vector2(45, 1)).is_empty(), "a taper only on the edge")

	# Joined.
	_choose(editor._pick_a, "Head")
	_choose(editor._pick_a_feature, "mortise")
	_choose(editor._pick_b, "Handle")
	_choose(editor._pick_b_feature, "tenon")
	_choose(editor._pick_kind, "through mortise and tenon")
	editor.add_joint()
	_choose(editor._pick_a, "Handle")
	_choose(editor._pick_a_feature, "kerf")
	_choose(editor._pick_b, "Wedge")
	_choose(editor._pick_b_feature, "taper")
	_choose(editor._pick_kind, "wedge")
	editor.add_joint()
	var joints: Array = editor.plan.joints
	_check(joints.size() == 2 and joints[0].a == "head" and joints[0].a_feature == "mortise" and joints[0].b == "handle" and
			joints[0].b_feature == "tenon" and joints[1].a_feature == "kerf" and joints[1].b == "wedge", "two joints: %s" % [joints])

	# Saved: the file, in the book as the player's.
	_check(editor._title.text.contains("not saved"), "not saved yet: " + editor._title.text)
	_check(editor.save(), "saved")
	var path := "user://plans/plain_mallet.json"
	var saved = JSON.parse_string(FileAccess.get_file_as_string(path)) if FileAccess.file_exists(path) else null
	_check(saved is Dictionary and saved.parts.size() == 3 and saved.joints.size() == 2 and not saved.has("mine") and
			saved.name == "Plain mallet", "the file %s" % path)
	_check(plans.plans.has("plain_mallet") and plans.plans.plain_mallet.get("mine", false) and editor.saved and
			editor.plan_id == "plain_mallet" and not editor._title.text.contains("not saved"), "in the plan book, the player's own")
	if saved is Dictionary:
		_check(saved.parts.map(func(p): return p.get("stock", "")) == ["head_oak", "handle_ash", "strip_oak"],
				"each part names its stock")
		_check(_same_lines(saved.parts[0], plans.part("mallet", "head")), "the head as saved, laid out as the preset's")
	print("plan editor: the mallet drawn on the pad and saved (%d bytes)" % FileAccess.get_file_as_string(path).length())

	# The head's sheet taken, laid on a head blank: 12 pencil lines.
	editor.show_part(0)
	editor.take_sheet()
	await _frames(2)
	_check(not workshop._ui.plans_open() and plans.sheet == {"plan": "plain_mallet", "part": "head"} and
			workshop.current == "layout" and workshop.settings.layout.variant == "plan", "the head's sheet in hand")
	workshop.set_wood("head_oak")
	await _frames(2)
	workshop.enter_work(false)
	await _frames(2)
	var piece: RigidBody3D = workshop.clamped
	var screen: Vector2 = workshop.camera.unproject_position(workshop.board.to_global(Vector3(-50, -28, 27.5)))
	workshop.hover_screen(screen)
	workshop.press(screen)
	workshop.release()
	var marks: Array = workshop.layout.marks_of(piece)
	_check(marks.size() == 12 and piece.get_meta("part", {}).get("plan", "") == "plain_mallet",
			"laid on a head blank: %d pencil lines, the piece the plain mallet's head" % marks.size())
	print("plan editor: its head laid on a blank in %d pencil lines" % marks.size())

	# From the book: edited, put down with P, the file as it was; the pad goes on with it.
	var before := FileAccess.get_file_as_string(path)
	workshop._ui.open_plans()
	await _frames(1)
	var viewer = workshop._ui._viewer
	viewer.show_plan("plain_mallet")
	_check(viewer._parts.item_count == 3 and viewer._edit.text == "Edit", "the book lists its 3 parts, to edit")
	viewer.edit()
	await _frames(1)
	editor = workshop._ui._viewer
	_check(workshop._ui.editor_open() and editor.plan_id == "plain_mallet" and editor.saved, "Edit puts it on the pad")
	editor.add_part()
	_check(not editor.saved and editor.plan.parts.size() == 4, "a part added: not saved")
	_key(KEY_P)
	await _frames(1)
	_check(not workshop._ui.plans_open() and FileAccess.get_file_as_string(path) == before and
			plans.draft.get("plan", {}).get("parts", []).size() == 4, "P puts it down: the file as it was, the pad keeping it")
	workshop._ui.open_editor("", false, true)
	await _frames(1)
	editor = workshop._ui._viewer
	_check(editor.plan.parts.size() == 4 and editor.plan_id == "plain_mallet" and not editor.saved, "the pad goes on with it")
	editor.throw_away()
	await _frames(1)
	_check(plans.draft.is_empty() and not workshop._ui.plans_open() and FileAccess.get_file_as_string(path) == before,
			"thrown away: the file as it was")

	# A copy of the preset, saved under its own id; the preset untouched. Deleted.
	workshop._ui.open_plans()
	await _frames(1)
	viewer = workshop._ui._viewer
	viewer.show_plan("mallet")
	_check(viewer._edit.text == "Edit a copy", "a preset is edited as a copy")
	viewer.edit()
	await _frames(1)
	editor = workshop._ui._viewer
	_check(editor.plan.name == "Mallet (copy)" and editor.plan_id == "" and not editor.saved, "the copy: " + editor.plan.name)
	_check(editor.save() and editor.plan_id == "mallet_copy" and FileAccess.file_exists("user://plans/mallet_copy.json") and
			FileAccess.get_file_as_string("res://plans/mallet.json") == preset_text and not plans.plans.mallet.get("mine", false),
			"saved as mallet_copy; the preset untouched")
	editor.delete()
	_check(plans.plans.has("mallet_copy") and editor._delete.text.contains("sure"), "one press on Delete asks")
	editor.delete()
	await _frames(1)
	_check(not plans.plans.has("mallet_copy") and not FileAccess.file_exists("user://plans/mallet_copy.json") and
			not workshop._ui.plans_open(), "the second deletes it")
	_check(plans.delete_plan("plain_mallet") and not FileAccess.file_exists(path) and not plans.delete_plan("mallet"),
			"the plain mallet deleted; a preset can't be")

	print("plan editor: %s" % ("the mallet drawn on the pad, saved, laid out" if not _failed else "FAILED"))
	get_tree().quit(1 if _failed else 0)


## A drag on the sheet with a tool, as its release draws it.
func _draw(tool: String, view: int, a: Vector2, b: Vector2) -> void:
	editor.set_tool(tool)
	var f: Dictionary = editor.feature_from(tool, view, a, b)
	_check(not f.is_empty(), "a %s drawn from %s to %s" % [tool, a, b])
	if not f.is_empty():
		editor.add_feature(f)


## Text typed into a field (its text_changed, as typing sends it).
func _text(line: LineEdit, text: String) -> void:
	line.text = text
	line.text_changed.emit(text)


## The part's blank, set in its size fields.
func _sizes(s: Vector3) -> void:
	for i in 3:
		editor._size[i].value = s[i]


## An OptionButton set to an item by its text (as its popup would).
func _choose(option: OptionButton, text: String) -> void:
	for i in option.item_count:
		if option.get_item_text(i) == text:
			option.select(i)
			option.item_selected.emit(i)
			return
	_check(false, "no %s to choose" % text)


## The SpinBox after a label in the picked feature's panel, or null.
func _field(label: String) -> SpinBox:
	for box in editor._props.get_children():
		if box.is_queued_for_deletion() or not box is HBoxContainer or box.get_child_count() != 2:
			continue
		if box.get_child(0) is Label and box.get_child(0).text == label and box.get_child(1) is SpinBox:
			return box.get_child(1)
	return null


## Whether two parts lay out in the same lines (SdfBody.part_lines on their own sizes).
func _same_lines(a: Dictionary, b: Dictionary) -> bool:
	var s := SheetView.size_of(b)
	if SheetView.size_of(a) != s:
		return false
	var la: Array = SdfBody.part_lines(a, s).lines
	var lb: Array = SdfBody.part_lines(b, s).lines
	if la.size() != lb.size():
		print("plan editor: %d lines, %d in the preset" % [la.size(), lb.size()])
		return false
	for line in la:
		var found := false
		for other in lb:
			if (line.origin - other.origin).length() < 1e-3 and (line.dir - other.dir).length() < 1e-3 and \
					absf(line.length - other.length) < 1e-3 and line.as == other.as:
				found = true
				break
		if not found:
			print("plan editor: a line not in the preset's: %s" % [line])
			return false
	return true


func _key_to_editor(code: Key) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	editor._unhandled_key_input(key)


## A key as the workshop takes it (nothing focused).
func _key(code: Key) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.pressed = true
	workshop._unhandled_input(key)


func _frames(n: int) -> void:
	for i in n:
		await get_tree().process_frame


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failed = true
		push_error("plan editor: " + what)
