#!/usr/bin/env bash
# Godot-side checks, run after building native/ (which also builds the GDExtension).
#
#   tools/test_godot.sh         the headless tier: logic, not pixels (under a minute on a CPU)
#   tools/test_godot.sh --gpu   the GPU tier: everything that renders and compares images
#
# The headless tier:
#   - the shared SDF includes and the Live shader compile in Godot's shader pipeline (one
#     frame, software OpenGL: the only check here that renders)
#   - the extension loads and builds a body
#   - the workshop: each tool, driven through the scene's own pointer methods, cuts, and
#     undo takes a stroke back
#   - a stroke previewed draws an overlay and applies nothing; committed, it lands as the
#     expected edits and the overlay empties, for each tool
#   - a stroke planned with the right button held is the cut the left-drag then makes; a
#     left-drag without a plan cuts as the wood lets it; the camera's buttons
#   - a strip sawn off the board comes away as a rigid body resting on the bench
#   - a rebate sawn off the end (two cuts meeting, no plane) comes away as an island, shown
#     once cut out, resting in the rebate on the board's own surface
# The GPU tier (meant for a machine with a GPU; it runs on Mesa's software drivers too, but
# at seconds a frame takes the best part of half an hour):
#   - the Live path renders a shaded scene with shadows and mesh intersections
#   - the workshop drive and the tool planning again, rendered, with screenshots in out/
#   - a stroke previewed by the shader looks the same once committed (image comparison)
#   - GPU/CPU parity over every demo (tools/parity.sh)
#   - with a Vulkan driver (a GPU, or Mesa's lavapipe: mesa-vulkan-drivers), Forward+: ADF
#     bricks sampled by the GPU sampler agree with CPU-sampled ones, and the workshop drive
#     runs with it
# Fails on any shader or script error. Needs xvfb-run and an OpenGL driver; set GODOT as
# for tools/run.sh.
set -uo pipefail

GPU=0
for arg in "$@"; do
	case "$arg" in
		--gpu) GPU=1 ;;
		-h | --help)
			sed -n '2,5p' "$0" | sed 's/^# \{0,1\}//'
			exit 0
			;;
		*)
			echo "unknown argument: $arg (usage: tools/test_godot.sh [--gpu])" >&2
			exit 2
			;;
	esac
done

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

if [[ "$GPU" == 0 ]]; then
	check shader_smoke "Screenshot saved" --render res://tests/shader_smoke.tscn -- --screenshot="$OUT/shader_smoke.png"
	check extension_smoke "extension smoke: carved panel" res://tests/extension_smoke.tscn
	check workshop_drive "workshop drive: chisel" res://tests/workshop_drive.tscn
	check stroke_preview "stroke preview: every tool" res://tests/stroke_preview.tscn
	check tool_planning "tool planning: every step as planned" res://tests/tool_planning.tscn
	check offcut_physics "offcut physics:" res://tests/offcut_physics.tscn
	check island_split "island split: the rebate came away" res://tests/island_split.tscn
	exit $status
fi

check live_sphere "Screenshot saved" --render res://tests/live_view.tscn -- --demo=sphere --floor \
	--screenshot="$OUT/live_sphere.png"
check live_panel "Screenshot saved" --render res://tests/live_view.tscn -- --screenshot="$OUT/live_panel.png"
check workshop_drive_render "workshop drive: chisel" --render res://tests/workshop_drive.tscn -- --out="$OUT"
check stroke_preview_render "stroke preview: every tool" --render res://tests/stroke_preview.tscn -- \
	--out="$OUT/preview"
check tool_planning_render "tool planning: every step as planned" --render res://tests/tool_planning.tscn -- \
	--out="$OUT"
if compgen -G "/usr/share/vulkan/icd.d/*.json" >/dev/null || compgen -G "/etc/vulkan/icd.d/*.json" >/dev/null; then
	check gpu_bricks "gpu bricks: every demo agrees" --vulkan res://tests/gpu_bricks.tscn
	mkdir -p "$OUT/vulkan"
	check workshop_drive_vulkan "workshop drive: chisel" --vulkan res://tests/workshop_drive.tscn -- --out="$OUT/vulkan"
else
	echo "skip  gpu_bricks, workshop_drive_vulkan: no Vulkan driver"
fi
"$ROOT/tools/parity.sh" || status=1
exit $status
