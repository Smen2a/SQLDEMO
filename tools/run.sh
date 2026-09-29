#!/usr/bin/env bash
# Runs a scene from the Godot project without an editor or a display.
#
#   tools/run.sh SCENE [-- user args]            headless: logic only, nothing is rendered
#   tools/run.sh --render SCENE [-- user args]   renders into a virtual X display with
#                                                software OpenGL (Mesa llvmpipe)
#   tools/run.sh --vulkan SCENE [-- user args]   the same with Forward+ on Vulkan (without a
#                                                GPU, Mesa's lavapipe: mesa-vulkan-drivers),
#                                                which has a RenderingDevice (compute)
#
# SCENE is a res:// path, e.g. res://tests/shader_smoke.tscn. Anything after `--` reaches
# the scene (see game/tests/harness.gd), e.g. -- --screenshot=out/frame.png
# Set GODOT to a Godot 4.7 binary if it is not on PATH as `godot`, and RESOLUTION (WxH,
# default 1280x720) to change the rendered window size.
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
elif [[ "${1:-}" == "--vulkan" ]]; then
	RENDER=2
	shift
fi
SCENE="${1:?usage: tools/run.sh [--render] res://path/to/scene.tscn [-- user args]}"
shift
if [[ "${1:-}" == "--" ]]; then
	shift
fi

# Scripts, uids and resources are only registered once the project has been imported.
# Godot 4.7.2's headless --import crashes while shutting down whenever a godot-cpp v10
# extension is loaded (godot-cpp's own test extension reproduces it); the import itself has
# completed by then, so check its output rather than its exit status.
if [[ ! -f "$PROJECT/.godot/uid_cache.bin" ]]; then
	( "$GODOT" --headless --audio-driver Dummy --path "$PROJECT" --import || true ) >/dev/null 2>&1
	if [[ ! -f "$PROJECT/.godot/uid_cache.bin" ]]; then
		echo "Project import failed (no .godot/uid_cache.bin)" >&2
		exit 1
	fi
fi

if [[ "$RENDER" == 1 ]]; then
	# --headless swaps in a dummy renderer that never produces a frame, so rendering needs
	# a (virtual) display instead. Without a GPU only the Compatibility renderer (OpenGL,
	# here Mesa's llvmpipe) is available; the project's own default is Forward+.
	LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 1920x1080x24" \
		"$GODOT" --path "$PROJECT" --audio-driver Dummy --rendering-method gl_compatibility --rendering-driver opengl3 \
		--resolution "${RESOLUTION:-1280x720}" "$SCENE" -- "$@"
elif [[ "$RENDER" == 2 ]]; then
	xvfb-run -a -s "-screen 0 1920x1080x24" \
		"$GODOT" --path "$PROJECT" --audio-driver Dummy --rendering-method forward_plus --rendering-driver vulkan \
		--resolution "${RESOLUTION:-1280x720}" "$SCENE" -- "$@"
else
	"$GODOT" --headless --audio-driver Dummy --path "$PROJECT" "$SCENE" -- "$@"
fi
