#!/usr/bin/env bash
# GPU/CPU parity: renders demo bodies through the Live raymarch (Godot, Compatibility
# renderer, software OpenGL) and through the CPU reference renderer (the formula field,
# every edit in order), then compares them pixel by pixel. Normals check geometry, albedo
# checks the material functions and mixing. Needs the native build (SDF_BUILD, default
# native/build) and what tools/run.sh --render needs.
#
#   tools/parity.sh [case ...]     case = <demo>:<normals|albedo>; default: the full set
#
# PARITY_ARGS passes extra arguments to the Godot scene, e.g. --single-pass=off.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${SDF_BUILD:-$ROOT/native/build}"
OUT="$ROOT/out/parity"
mkdir -p "$OUT"

CASES=("$@")
if [[ ${#CASES[@]} -eq 0 ]]; then
	CASES=(carved_panel:normals carved_panel:albedo sphere:normals sphere:albedo session:normals)
	for i in 0 1 2 3 4 5 6 7; do CASES+=("blend_$i:normals"); done
	for i in 0 1 2 3 4 5; do CASES+=("material_$i:normals" "material_$i:albedo"); done
fi

log="$OUT/godot.log"
"$ROOT/tools/run.sh" --render res://tests/parity.tscn -- --cases="$(IFS=,; echo "${CASES[*]}")" --out="$OUT" ${PARITY_ARGS:-} >"$log" 2>&1
if grep -E -q "SHADER ERROR|SCRIPT ERROR|Parse Error|^ERROR:" "$log"; then
	echo "FAIL  Godot reported errors (see ${log#$ROOT/})"
	grep -E -A6 "SHADER ERROR|SCRIPT ERROR|Parse Error|^ERROR:" "$log" | head -40
	exit 1
fi

status=0
for c in "${CASES[@]}"; do
	demo="${c%%:*}" view="${c#*:}"
	gpu="$OUT/${demo}_${view}.png"
	if [[ ! -f "$gpu" ]]; then
		echo "FAIL  $c: Godot wrote no image (see ${log#$ROOT/})"
		status=1
		continue
	fi
	if result=$("$BUILD/sdf_render" "$demo" "$OUT/${demo}_${view}_cpu.png" --view "$view" --compare "$gpu" \
			--diff "$OUT/${demo}_${view}_diff.png" --max-mean 0.5 --max-over 0.002 | tail -1); then
		echo "ok    $c  ${result#*: }"
	else
		echo "FAIL  $c  ${result#*: }"
		status=1
	fi
done
exit $status
