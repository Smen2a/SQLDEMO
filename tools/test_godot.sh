#!/usr/bin/env bash
# Godot-side checks: renders each test scene and fails on any shader or script error.
# Needs xvfb-run and a software OpenGL driver; set GODOT as for tools/run.sh.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/out"
mkdir -p "$OUT"

status=0
for scene in shader_smoke; do
	log="$OUT/$scene.log"
	"$ROOT/tools/run.sh" --render "res://tests/$scene.tscn" -- --screenshot="$OUT/$scene.png" >"$log" 2>&1
	if grep -E -q "SHADER ERROR|SCRIPT ERROR|Parse Error|^ERROR:" "$log"; then
		echo "FAIL  $scene (see ${log#$ROOT/})"
		grep -E -A6 "SHADER ERROR|SCRIPT ERROR|Parse Error|^ERROR:" "$log" | head -40
		status=1
	elif ! grep -q "Screenshot saved" "$log"; then
		echo "FAIL  $scene: no frame captured (see ${log#$ROOT/})"
		status=1
	else
		echo "ok    $scene"
	fi
done
exit $status
