#!/usr/bin/env bash
# Run SQLDEMO without an editor or a display.
#
#   tools/run.sh              print the query result to stdout (headless)
#   tools/run.sh --screenshot render the scene off-screen and save out/sqldemo.png
#
# Set GODOT to the Godot 4.4+ binary if it is not on PATH as `godot`.
set -euo pipefail

GODOT="${GODOT:-$(command -v godot || true)}"
if [[ -z "$GODOT" ]]; then
	echo "Godot not found. Install it or set GODOT=/path/to/Godot_v4.x-stable_linux.x86_64" >&2
	exit 1
fi

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_DIR"

# `class_name` scripts are only registered once the project has been imported,
# so do that first; it is a no-op on later runs.
if [[ ! -f .godot/global_script_class_cache.cfg ]]; then
	"$GODOT" --headless --audio-driver Dummy --path . --import >/dev/null
fi

if [[ "${1:-}" == "--screenshot" ]]; then
	OUT="${2:-$PROJECT_DIR/out/sqldemo.png}"
	mkdir -p "$(dirname "$OUT")"
	# The headless display server does not render, so draw into a virtual X
	# display with software OpenGL and read the frame back from there.
	LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1280x720x24" \
		"$GODOT" --path . --audio-driver Dummy --rendering-driver opengl3 \
		--resolution 960x540 -- --screenshot="$OUT"
else
	"$GODOT" --headless --audio-driver Dummy --path . -- --quit
fi
