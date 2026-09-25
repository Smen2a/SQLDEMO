# Working on this repo

A real-time-editable SDF engine: a C++ core in `native/` (no dependencies), a Godot 4.7
GDExtension built from it, and the Godot project in `game/`. See README.md for the
design and docs/PLAN.md for the plan and what is done.

## Build

```sh
git submodule update --init
cmake -S native -B native/build -G Ninja
cmake --build native/build          # also writes game/bin/sdf_godot.<os>.<ext>
native/build/sdf_tests              # property tests and golden images
```

Also build and test with Clang, in a separate build directory with the extension off:
`-DCMAKE_CXX_COMPILER=clang++ -DSDF_BUILD_GODOT=OFF`. After editing
`native/src/core/shared/`, run `tools/sync_shaders.sh`.

## Godot tests

Set `GODOT` to a Godot 4.7 binary (e.g. `Godot_v4.7.2-stable_linux.x86_64`), then:

- `tools/test_godot.sh`: the **headless tier**. Everything runs headless apart from one
  software-rendered frame that checks the Live shader compiles. It takes under a minute.
- `tools/test_godot.sh --gpu`: the **GPU tier**, which covers live renders, screenshots,
  preview-against-commit image comparison, GPU/CPU parity (`tools/parity.sh`) and Vulkan.

The cloud environment has no GPU: Mesa's llvmpipe takes about 2 s a frame, so the GPU
tier takes around half an hour there. **Run the headless tier here.** Leave `--gpu` for a
machine with a GPU. When a change is to the shaders or to what is drawn, run only the
affected rendered scene (`tools/run.sh --render res://tests/<scene>.tscn`), not the whole tier.

Tests should not depend on visual output. Put new logic checks in scenes that run headless
(`tools/run.sh res://tests/<scene>.tscn`). Skip screenshots and image comparisons when
`DisplayServer.get_name() == "headless"`, as `stroke_preview.gd` and `workshop_drive.gd`
do, and put the rendered run in the `--gpu` tier.
