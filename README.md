# Object Builder — SDF engine

The engine for a crafting game where players make working objects — a pickaxe is a
steel head, an ash handle and a wedge — from parts shaped by real processes and then
joined. Every part is a **signed distance field**: an analytic base shape plus an ordered
list of edits (tool strokes, fillets, inlays, paint), each combined with a chosen blend
mode. The full design is in the approved plan; this README covers what exists today.

## Layout

| Path | What it is |
| --- | --- |
| `native/src/core/shared/*.glsl` | The SDF formulas — primitives, tool cross-sections, swept strokes, blend modes, materials. Written in a subset that compiles **both as C++ and as Godot shader code**, so the CPU evaluator and the GPU raymarcher run the same maths. Edit these only. |
| `native/src/core/glsl_compat.h` | Just enough GLSL (`vec3`, `mix`, `clamp`, …) in C++ to compile the shared files. |
| `native/src/core/body/` | `Body`, `Edit`, `Primitive`: the per-part source of truth, bounds and Lipschitz bounds; the material table. |
| `native/src/core/compile/` | The octree: per-cell pruned edit lists ("tapes") that keep per-query cost independent of edit count. |
| `native/src/core/eval/` | The CPU reference renderer: ground truth for every later GPU path. |
| `native/src/demo/`, `native/tools/` | Demo scenes, `sdf_gallery` (renders them) and `sdf_bench` (octree scaling). |
| `native/tests/` | Property tests for the core and golden-image tests. No Godot needed. |
| `game/` | The Godot 4.7 project. `game/shaders/sdf/` holds byte-identical copies of the shared files. |
| `tools/` | `sync_shaders.sh`, `run.sh` (headless / Xvfb scene runner), `test_godot.sh`. |

![Every blend mode applied to the same union (a boss rising from a block) and subtract (a chiselled channel)](docs/images/blend_gallery.png)

## Blend modes

The primitive decides the surface; the blend mode decides the edge where two surfaces meet.
Every mode has compact support — beyond its radius it is exactly the hard result — so an
edit only reshapes the field near its own primitive.

| Mode | Edge | Lipschitz |
| --- | --- | --- |
| `Hard` | crisp arris | 1 |
| `Chamfer` | flat bevel, optionally asymmetric (`r` onto one surface, `r2` onto the other) | √2 |
| `Round` | circular fillet, unchanged outside it | √2 |
| `Smooth`, `SmoothC2` | organic polynomial blend (C1 / C2); mixes materials | 1 |
| `Profile` | router-style edge profiles: arc concave, arc convex, ogee | √2 |

Operators: union, subtract, intersect, plus engrave / groove / tongue along a guide
surface, and paint (material only). Tool strokes sweep a flat, V or gouge cross-section
along a segment or a quadratic Bezier.

## Materials

Materials are evaluated in each part's own space, relative to where it sat in the log or
block, so a cut reveals the figure that was always inside it. Smooth blends mix materials;
hard ones keep a crisp boundary on one continuous surface.

![Flat- and quarter-sawn wood, putty mixing across a smooth seam, a flush brass inlay, granite and steel](docs/images/material_gallery.png)

![A relief-carved ash panel: V-tool outlines, gouged petals, border groove](docs/images/carved_panel.png)

These are rendered by the CPU reference renderer (`native/src/core/eval`): sphere tracing
with Lipschitz-scaled steps and a pixel-footprint hit epsilon, SDF soft shadows and SDF
ambient occlusion. It is slow on purpose — it is the ground truth the GPU path will be
diffed against. `Body::sample` already skips, per point, every edit whose bounding box
is at least `|d| + influence` away, which provably never changes the result's sign.

## Scaling to many edits: the octree

A carving session is thousands of strokes; a stone job, tens of thousands of chips.
Evaluating every edit at every point would make each query cost O(edits). Each body is
therefore compiled into an adaptive octree whose cells hold a pruned **tape**: only the
edits that can shape the surface inside that cell, in order.

**The contract:** inside every cell, a tape's field has exactly the body's sign, and its
exact values within `value_margin` (1 mm) of the surface — enough for hits, normals and
shadows. Further out it is a valid distance bound for that cell, so tracing clamps every
step at the cell's exit and skips cells proven empty.

Edits are dropped from a cell by rules that each preserve that contract:

- **Reach.** Its bounds, grown by its own blend reach, by the widest reach of any *later*
  edit that reads values in the cell, and by the value margin, miss the cell. (Hard min /
  max only depend on their operands' signs, but a later blend reads actual values, which a
  dropped neighbour could otherwise have fed.)
- **Interval arithmetic.** The field's range over the cell is tracked through every op's
  own rules. An edit that provably changes nothing is dropped; one whose primitive alone
  defines the whole cell *resets* the tape — unless an earlier union or paint put a
  different material there, which a cut must still expose.
- **Supersession.** A hard cut provably contained, throughout the cell, by a later hard
  cut is dead: finishing cuts supersede roughing cuts. Monotone blends in between widen the
  margin the later cut must win by; profiles, guide ops and material changes stop the search.

Cells split while their tape exceeds a budget, but never below half the smallest feature
in the tape: chipped stone does not need the sub-millimetre cells a fine V-tool line does.
Curved strokes tighter than their tool can follow are refused by `Body::add` — they are
not cuts a real tool makes, and their distance bound degrades too far.

`sdf_bench`, a granite block roughed out by percussive chips (4 cores):

| Chips | Build | Add one chip | Surface cells | Mean tape | Memory | vs full edit list |
| --- | --- | --- | --- | --- | --- | --- |
| 1,000 | 0.003 s | 7 µs | 820 | 4.3 | < 1 MB | 75× faster |
| 10,000 | 0.16 s | 30 µs | 55 k | 7.6 | 3.5 MB | 380× faster |
| 100,000 | 10.7 s | 1.0 ms | 1.0 M | 14.7 | 183 MB | 965× faster |

The 100k case is heavy mostly because a 0.4 m² worked face at millimetre detail simply has
that many cells; consolidating old chips into a sampled base field (planned alongside
forging) is what will bound it for very long jobs.

## Building and testing

The core has no dependencies; the tests and the gallery tool need zlib (for PNGs).

```sh
cmake -S native -B native/build -G Ninja
cmake --build native/build
native/build/sdf_tests            # property tests (exactness, compact support, Lipschitz
                                  # bounds, no phantom surfaces, material weights, culling)
                                  # and golden images of the demo scenes
native/build/sdf_gallery out 2 2  # re-render the gallery images at 2x, 2x2 supersampled

GODOT=/path/to/godot tools/test_godot.sh   # compiles the shared files through Godot's
                                           # shader pipeline (needs xvfb-run + Mesa)
```

After editing anything in `native/src/core/shared/`, run `tools/sync_shaders.sh`; the
test suite fails if the Godot copies drift. After an intended visual change, rewrite the
golden images with `SDF_UPDATE_GOLDEN=1 native/build/sdf_tests golden` — and look at them
before committing. The goldens are bit-identical across GCC and Clang, optimised or not,
on x86-64; the tolerances are there for MSVC and ARM.
