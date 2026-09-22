#!/usr/bin/env bash
# Runs a scene from the Godot project without an editor or a display.
#
#   tools/run.sh SCENE [-- user args]            headless: logic only, nothing is rendered
#   tools/run.sh --render SCENE [-- user args]   renders into a virtual X display with
#                                                software OpenGL (Mesa llvmpipe)
#
# SCENE is a res:// path, e.g. res://tests/shader_smoke.tscn. Anything after `--` reaches
# the scene (see game/tests/harness.gd), e.g. -- --screenshot=out/frame.png
# Set GODOT to a Godot 4.7 binary if it is not on PATH as `godot`.
set -euo pipefail

GODOT="${GODOT:-$(command -v godot || true)}"
if [[ -z "$GODOT" ]]; then
	echo "Godot not found. Install it or set GODOT=/path/to/Godot_v4.7-stable_linux.x86_64" >&2
	exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/game"

RENDER=0
if [[ "${1:-}" == "--render" ]]; then
	RENDER=1
	shift
fi
SCENE="${1:?usage: tools/run.sh [--render] res://path/to/scene.tscn [-- user args]}"
shift
if [[ "${1:-}" == "--" ]]; then
	shift
fi

# Scripts, uids and resources are only registered once the project has been imported.
if [[ ! -d "$PROJECT/.godot" ]]; then
	"$GODOT" --headless --audio-driver Dummy --path "$PROJECT" --import >/dev/null 2>&1
fi

if [[ "$RENDER" == 1 ]]; then
	# --headless swaps in a dummy renderer that never produces a frame, so rendering needs
	# a (virtual) display instead.
	LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1920x1080x24" \
		"$GODOT" --path "$PROJECT" --audio-driver Dummy --rendering-driver opengl3 \
		--resolution 1280x720 "$SCENE" -- "$@"
else
	"$GODOT" --headless --audio-driver Dummy --path "$PROJECT" "$SCENE" -- "$@"
fi
