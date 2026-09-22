#!/usr/bin/env bash
# Copies the shared SDF sources into the Godot project as shader includes.
# native/src/core/shared/*.glsl is the one place to edit; the copies must stay
# byte-identical, which the native test suite checks.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/native/src/core/shared"
DST="$ROOT/game/shaders/sdf"

mkdir -p "$DST"
for f in "$SRC"/*.glsl; do
	name="$(basename "$f" .glsl)"
	cp "$f" "$DST/$name.gdshaderinc"
done
echo "synced $(ls "$SRC"/*.glsl | wc -l) shared files into ${DST#$ROOT/}"
