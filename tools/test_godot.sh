#!/usr/bin/env bash
# Godot-side checks, run after building native/ (which also builds the GDExtension):
#   - the shared SDF includes and the Live shader compile in Godot's shader pipeline
#   - the extension loads and builds a body
#   - the Live path renders a shaded scene with shadows and mesh intersections
#   - the workshop: each tool, driven through the scene's own pointer methods, cuts, and
#     undo takes a stroke back
#   - a stroke previewed by the shader looks the same once committed, for each tool
#   - a stroke planned with the right button held (hatched, the tool out of view, the
#     section inset showing) is the cut the left-drag then makes; the camera's buttons
#   - a strip sawn off the board comes away as a rigid body resting on the bench
#   - GPU/CPU parity over every demo (tools/parity.sh)
#   - with a Vulkan driver (a GPU, or Mesa's lavapipe: mesa-vulkan-drivers), Forward+: ADF
#     bricks sampled by the GPU sampler agree with CPU-sampled ones, and the workshop drive
#     runs with it
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
check stroke_preview "stroke preview: every tool" --render res://tests/stroke_preview.tscn -- --out="$OUT/preview"
check tool_planning "tool planning: every step as planned" --render res://tests/tool_planning.tscn -- --out="$OUT"
check offcut_physics "offcut physics:" --render res://tests/offcut_physics.tscn
if compgen -G "/usr/share/vulkan/icd.d/*.json" >/dev/null || compgen -G "/etc/vulkan/icd.d/*.json" >/dev/null; then
	check gpu_bricks "gpu bricks: every demo agrees" --vulkan res://tests/gpu_bricks.tscn
	mkdir -p "$OUT/vulkan"
	check workshop_drive_vulkan "workshop drive: chisel" --vulkan res://tests/workshop_drive.tscn -- --out="$OUT/vulkan"
else
	echo "skip  gpu_bricks, workshop_drive_vulkan: no Vulkan driver"
fi
"$ROOT/tools/parity.sh" || status=1
exit $status
