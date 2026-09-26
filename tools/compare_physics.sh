#!/usr/bin/env bash
# Saws a strip off the workshop board under two physics engines and prints how each
# handles the offcut (tests/offcut_physics: how far it sinks into the bench, tilts and
# slides):
#   - Godot's default (Jolt since 4.6), with this project's tolerances for millimetre-sized
#     pieces (project.godot [physics]);
#   - Box3D, through the experimental bearlikelion/godot-box3d GDExtension, a drop-in
#     PhysicsServer3D. Build it from https://github.com/bearlikelion/godot-box3d
#     (cmake -S . -B build -G Ninja && cmake --build build) and point BOX3D_LIB at the
#     library it writes (bin/libgodot-box3d.so).
# The addon and the engine switch exist only for the run: game/addons/godot-box3d and
# game/override.cfg are removed afterwards (both are gitignored). Set GODOT as for run.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB="${BOX3D_LIB:?set BOX3D_LIB to a built libgodot-box3d (see the comment at the top)}"
ADDON="$ROOT/game/addons/godot-box3d"
LIST="$ROOT/game/.godot/extension_list.cfg"
OVERRIDE="$ROOT/game/override.cfg"

# Both colliders an offcut can get: a box (sawn strips) and a convex hull (anything else).
run() {
	for collider in box hull; do
		"$ROOT/tools/run.sh" --render res://tests/offcut_physics.tscn -- --report-only=1 --collider=$collider 2>&1 |
			grep -E "offcut physics|ERROR" || true
	done
}

echo "== Godot's default engine (Jolt), tuned for millimetres"
run

# The project must have been imported once (run.sh does it) for the extension list to exist.
cp "$LIST" "$LIST.bak"
cleanup() {
	rm -rf "$ADDON" "$OVERRIDE"
	mv -f "$LIST.bak" "$LIST"
}
trap cleanup EXIT
mkdir -p "$ADDON/bin"
cp "$LIB" "$ADDON/bin/"
cat >"$ADDON/godot-box3d.gdextension" <<'GDEXT'
[configuration]

entry_symbol = "godot_box3d_main"
compatibility_minimum = "4.3"

[libraries]

linux.x86_64 = "res://addons/godot-box3d/bin/libgodot-box3d.so"
GDEXT
echo "res://addons/godot-box3d/godot-box3d.gdextension" >>"$LIST"
printf '[physics]\n\n3d/physics_engine="Box3D Physics"\n' >"$OVERRIDE"

echo "== Box3D (godot-box3d, experimental), its default tolerances"
run
