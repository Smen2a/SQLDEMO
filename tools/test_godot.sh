#!/usr/bin/env bash
# Godot-side checks, run after building native/ (which also builds the GDExtension):
#   - the shared SDF includes and the Live shader compile in Godot's shader pipeline
#   - the extension loads and builds a body
#   - the Live path renders a shaded scene with shadows and mesh intersections
#   - the workshop: each tool, driven through the scene's own pointer methods, cuts, and
#     undo takes a stroke back
#   - GPU/CPU parity over every demo (tools/parity.sh)
# Fails on any shader or script error. Needs xvfb-run and a software OpenGL driver; set
# GODOT as for tools/run.sh.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out"
mkdir -p "$OUT"
ERRORS="SHADER ERROR|SCRIPT ERROR|Parse Error|^ERROR:"

status=0
# check NAME EXPECTED_LINE tools/run.sh args...
check() {
	local name="$1" expect="$2"
	shift 2
	local log="$OUT/$name.log"
	"$ROOT/tools/run.sh" "$@" >"$log" 2>&1
	if grep -E -q "$ERRORS" "$log"; then
		echo "FAIL  $name (see ${log#$ROOT/})"
		grep -E -A6 "$ERRORS" "$log" | head -40
		status=1
	elif ! grep -q "$expect" "$log"; then
		echo "FAIL  $name: no '$expect' (see ${log#$ROOT/})"
		status=1
	else
		echo "ok    $name"
	fi
}

check shader_smoke "Screenshot saved" --render res://tests/shader_smoke.tscn -- --screenshot="$OUT/shader_smoke.png"
check extension_smoke "extension smoke: carved panel" res://tests/extension_smoke.tscn
check live_sphere "Screenshot saved" --render res://tests/live_view.tscn -- --demo=sphere --floor \
	--screenshot="$OUT/live_sphere.png"
check live_panel "Screenshot saved" --render res://tests/live_view.tscn -- --screenshot="$OUT/live_panel.png"
check workshop_drive "workshop drive: chisel" --render res://tests/workshop_drive.tscn -- --out="$OUT"
"$ROOT/tools/parity.sh" || status=1
exit $status
