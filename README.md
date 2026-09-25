# Object Builder — SDF engine

The engine for a crafting game where players make working objects — a pickaxe is a
steel head, an ash handle and a wedge — from parts shaped by real processes and then
joined. Every part is a **signed distance field**: an analytic base shape plus an ordered
list of edits (tool strokes, fillets, inlays, paint), each combined with a chosen blend
mode. The full design is in the approved plan; this README covers what exists today.

## The workshop

Open `game/` in Godot 4.7 and press F5 (or run `godot --path game`). A board lies on a
bench with four hand tools beside it: a chisel, a back saw, a sanding block and a sanding
sponge. Every one is an SDF body, built by the engine in steel, brass, ash, walnut, cork
and abrasive.

1. **Pick up a tool.** Click it on the bench, or press 1 to 4.
2. **Point at the board.** The tool floats where it will engage, and an outline marks what
   it will touch: the chisel's edge and its push direction, the saw's line, the block's
   face, the sponge's reach.
3. **Hold the left button and drag.** The cut follows live:
   - **Chisel:** pares along the drag, at the width and depth set in the panel. It ramps
     in at its approach angle and lifts out when you let go. It cannot un-cut: pulling
     back does nothing.
   - **Saw:** stroke it back and forth along its line. Every millimetre of travel deepens
     the kerf by the feed, until it is through the board.
   - **Sanding block:** takes the surface down wherever it rubs, faster at coarser grits.
     Being a flat block, it flattens: high spots and edges go first, and the edges of the
     patch feather out.
   - **Sanding sponge:** soft, so it wraps over whatever it is rubbed on. It rounds over
     the arrises and ridges within its reach (10 mm) and leaves faces and hollows alone.
     Rub along an edge to ease it; the longer you rub, the rounder it gets.

Other controls:
- Q / E turn the tool; Esc drops the stroke in progress.
- Ctrl+Z / Ctrl+Shift+Z undo and redo, without limit.
- Right-drag orbits, middle-drag pans, the wheel zooms.
- The panel switches the wood (ash, oak, walnut), starts a new board, and lets the board
  cast shadows.
- *Preview strokes on the GPU* (on by default): the shader draws the cut while you drag,
  and the board applies it once, when you let go. Turned off, every move is applied as it
  happens, for comparison.
- The status line shows how long the last edit took to apply and upload, how many edits
  the shader is previewing, and the GPU frame time.

| | |
| --- | --- |
| ![The chisel hovering over an ash board where it will engage, with the saw and the sanding block on the bench](docs/images/workshop_chisel.png) | ![The chisel at work, its edge riding in the groove it has pared](docs/images/workshop_chisel_working.png) |
| ![The back saw stroked across the board, its teeth in the kerf](docs/images/workshop_saw_working.png) | ![The board afterwards: a paring cut with ramped ends, a kerf the length of the board, and a sanded patch](docs/images/workshop_result.png) |
| ![The sanding sponge rubbed along the board's front top arris](docs/images/workshop_sponge_working.png) | |

(Rendered here on software OpenGL, at about a frame a second; the status line reads the
GPU time on real hardware.)

**How it works:**
- A tool in use is a *stroke* (`native/src/core/tools/`). It turns the tool's motion into
  edits of the same shapes the tool's model is built from, and gives the cut so far merged
  into as few edits as it takes: the chisel's ramp and flat run, the saw's kerf, the
  block's pass.
- **While the tool moves, the shader draws the stroke.** `SdfBody` hands the merged edits
  to the Live shader as uniforms (the *overlay*, up to 16 edits), and the shader cuts them
  into the field wherever it samples it: the march, the settling steps, the normals and
  the ambient occlusion. Moving a tool costs no CPU work at all, and the cut keeps up with
  the frame rate.
  - Tools only cut, and a cut only ever raises the field, so empty cells stay empty and are
    still skipped whole.
  - A cut applied on top of the field is exactly the body with that edit appended. Where it
    opens up a solid cell, which has no brick, the shader uses the cell's stored material.
  - Normals difference the cut as closely as an exact cell's tape, so its edges stay crisp.
    Bricks keep their taps half a voxel apart, because hardware filtering resolves only
    1/256 of a voxel.
- **When the tool lifts off, the stroke is applied once:** the merged edits, plus the
  chisel's lift-out, go to the body's `EditSession` (`native/src/core/edit/`). That session
  keeps unlimited undo and updates the octree and ADF incrementally; cells that pruning
  shows no changed edit reaches keep their bricks.
  - `SdfBody` applies it on a worker thread, then uploads only the brick layers that
    changed.
  - The stroke leaves the overlay in the same frame as that upload, so nothing pops.
  - Commands that arrive while an update runs are folded together, so a slow update never
    builds a backlog.
  - `game/tests/stroke_preview` renders each tool's preview and its commit and diffs them.
    The mean difference is under 0.1 / 255, and 0.013% of pixels or fewer differ
    noticeably.
- **With the preview off,** each move is applied as it happens. Cuts that only grow keep
  their newest piece open and re-cut just that as it grows. The chisel's 10 mm lengths of
  run and the saw's 1 mm slices of kerf are then left behind, so an update touches only
  what moved. Those pieces overlap generously: inside a union of cuts the field is only a
  bound, and a thin overlap leaves a seam whose value is smaller than the hit epsilon,
  which the raymarcher would draw as a wall. The merged edits have no such seams.

`sdf_bench tools` times both ways on the workshop board (4 shared cores), and the
refinement that follows once the tool is idle (see below):

| Tool at work | Applied as it moves | Previewed, applied on release | Refined when idle |
| --- | --- | --- | --- |
| chisel, 12 mm wide, 1.5 mm deep, pushed 60 mm | 29 updates of 1.9 ms (55 ms in all), 8 edits | 3.0 ms once, 3 edits | 55 ms |
| saw, 12 mm deep in 30 strokes | 30 updates of 3.4 ms (0.10 s), 10 edits | 4.8 ms once, 1 edit | 63 ms |
| sanding block, rubbed over the board | 25 updates of 8.2 ms (0.21 s), 1 edit | 8.8 ms once, 1 edit | 93 ms |
| sanding sponge along an arris, 3 passes of 60 mm | 30 updates of 23 ms (flow 12, octree and ADF 11), 1 layer | (not previewed) | |

Before the changes below, the same commits took 57, 70 and 95 ms, and a sponge update
48 ms. Applied as it moves, the cut lags the tool by an update. Previewed, the cut is there
in the same frame, and the CPU works once per stroke.

### Edits in milliseconds

Applying a cut costs little in itself. What cost the time was refining the creases a cut
makes (its rim, and its floor meeting its walls) down to 1 mm exact cells, eight children
at a time, re-sampling a thousand bricks or more.
- **Coarse first, refined when idle.** An edit stops refining creases at the largest bricks
  (`AdfParams::update_exact_cell`, 7 mm): they become exact cells at once. Exact cells
  evaluate their own tapes, so the surface is the same. Only drawing costs more there
  until `Adf::refine()` takes them down to 1 mm. `SdfBody` refines on its worker once the
  body is idle (no tool engaged, nothing queued: `refine_when_idle`); a new stroke simply
  waits for it. An edit that reaches creases refined earlier redoes them from their big
  cube, so cutting across old work costs no more than cutting a face.
- **Cuts fold into the old bricks.** When an edit only appends cuts (Subtract or
  Intersect), each brick it reaches applies them to the samples it already holds: one
  primitive per sample instead of the cell's whole tape. The usual checks at voxel centres
  catch whatever the cut does between samples (a new crease, a kerf thinner than a voxel)
  and send that cell down the full path. Bricks the cut leaves as they were keep their
  slots and are not uploaded again; the rest are rewritten in place.
- **Half the precision.** Bricks are refined to 10 µm (was 5 µm), and never finer than
  40 µm between samples (was 20 µm). The board takes 7,288 bricks instead of 13,320.
- **Smaller uploads.** Brick layers are 512² (512 bricks), so an edit re-sends a quarter
  as much per layer it touches.
- **Bricks on the GPU.** Where the renderer has a `RenderingDevice` (Forward+, Mobile),
  bricks are sampled by a compute shader (`native/src/godot/gpu_sampler.cpp`). It is built
  from the same primitive and blend includes as the Live shader, with a tape interpreter
  reading storage buffers. The ADF refines a level at a time, so each level's bricks are
  one dispatch: 512 corners and 343 voxel centres each. The CPU keeps only the decisions
  (split, exact cell, brick). Bricks whose tapes hold a smoothing layer (a grid only the
  CPU has) are sampled on the CPU alongside. `SdfBody.gpu_bricks` (on by default) turns it
  off; under Compatibility the CPU samples everything, as before.
  - `game/tests/gpu_bricks` (Forward+) checks that GPU-sampled bricks agree with
    CPU-sampled ones within 0.02 mm near the surface, after a build, after edits and once
    refined; on Mesa's lavapipe they agree within 0.0065 mm. It also prints build and
    refinement times, CPU against GPU. Only a real GPU makes those meaningful: lavapipe is
    the CPU again.

### Sanding as smoothing: a layer, not cuts

A cut is a shape; what a sponge or a sheet of sandpaper in the hand does is closer to
smoothing the surface it is rubbed over. So the sponge does not make edits of shapes at
all. It makes one *smoothing layer* (`native/src/core/body/layer.h`,
`native/src/core/tools/smoothing.h`): the field near the surface, re-sampled on a sparse
grid (0.25 mm) and evolved by curvature flow.
- **Curvature flow.** Where the sponge bears, every point moves inward at a speed set by
  the surface's curvature there, convex parts only: dphi/dt = |grad phi| max(kappa, 0).
  - An arris rounds over, with a radius growing as the square root of the rubbing.
  - Ridges left by other tools soften, while faces and hollows stay put.
  - Each update runs a few explicit steps on the grid near the sponge, in parallel.
- **The layer replaces the field instead of offsetting it.** Inside a sharp arris the field
  itself has a crease (along the bisector: F = -min(x, y)). Adding any smooth offset to F
  would keep that crease, as a ridge along the middle of every sanded edge.
  - So the layer stores the smoothed field phi and a weight w, both reconstructed as
    quadratic B-splines (C1, so normals stay smooth), and gives (1 - w) F + w phi.
  - Where w is 1, none of F's creases remain.
  - B-splines reproduce planes exactly, so flat faces under the sponge do not move by a
    micron.
- **It is one edit, `Op::Layer`, in the edit list like any other:**
  - undo takes it away;
  - later cuts apply over it;
  - a later sponge stroke smooths those.
  - Octree pruning drops it wherever its weight is 0, and bounds it by its samples
    elsewhere.
  - The ADF samples it into bricks like everything else. It never gives it an exact cell,
    because only the CPU holds the grid (the `EXACT` live source draws bodies without their
    layers).
- **Its gradient stays bounded (1.41 plus about 0.15), so rays keep taking safe steps.**
  - Level sets outside the surface move nearly with the surface beneath them (velocity
    extension, capped so the explicit steps stay stable).
  - Inside, each level moves by its own curvature, so levels only spread apart.
  - The bound is measured only down to 0.9 mm below the surface: rays approach from
    outside, and deeper only the field's sign matters.
- **The sponge is applied as it goes.** The shader cannot draw a layer, so there is no
  preview. The worker runs the flow and re-samples only the region whose samples changed
  (`EditSession::revise_stroke` with a changed box). Moving the sponge only records its
  path.

| | |
| --- | --- |
| ![A walnut block with a chisel groove: its front top arris sanded round in the middle and still sharp at both ends, and the groove's edges softened](docs/images/sanded.png) | ![The same in the normals view: the sanded arris turns smoothly from the top face to the front, the ends stay sharp](docs/images/sanded_normals.png) |

The "sanded" demo (`sdf_render sanded`, `load_demo("sanded")`): a sharp walnut block
with a chisel groove, sanded by two sponge strokes, one layer each. It is part of the
GPU/CPU parity set.

### Pieces: sawn through, the board comes apart

Saw right through the board and the strip you cut off comes away. It is a body of its own,
resting on the bench, and still editable. Undo straight after puts the pieces back together.
Fracture and failing joints will split bodies the same way later
([docs/PLAN.md](docs/PLAN.md), "Pieces"); only what cuts them will differ.
- **Detecting it.** A saw stroke that has gone through reports its kerf's middle plane
  (`Stroke::separation`). After its commit, the worker checks the plane with
  `plane_clear` (`native/src/core/pieces/`). This is an adaptive quadtree over the plane
  within the body's bounds. A square is proven free of material when the field at its
  centre exceeds the Lipschitz bound times its half-diagonal; otherwise it splits, down to
  0.05 mm. If no material crosses the plane, the two sides cannot be joined: any path
  between them crosses it. On the board the check takes about 13 ms. Then `SdfBody` emits
  `separated(point, normal)`.
- **Splitting without waiting.** `SdfBody.split(point, normal)` returns a new body for the
  part in front of the plane and keeps the part behind it.
  - Each piece appends an `Op::Intersect` with its half-space, as an undo step. Until
    that lands, the shader's overlay draws the half-space: the same mechanism that
    previews strokes (W2a) cuts the other side away.
  - Both pieces draw the same textures until either uploads anew; the next upload
    creates new ones.
  - Nothing waits on a rebuild.
- **Physics.**
  - Each piece's mass (from the ADF's volume and the wood's density) and its centre of
    mass are computed before its rebuild starts.
  - Its rigid body sits at its centre of mass, because Godot's automatic one follows shape
    origins.
  - Its collider is a box when the piece fills at least 90% of its bounds, as a sawn
    strip does, and a convex hull otherwise. The hull's points are brick zero crossings,
    reduced to the extremes along 256 directions, then to the outermost of any within
    1 mm. Left as tight clusters round corners and edges, they made sliver faces the
    offcut rocked and sank on.
  - The offcut gets a 0.25 m/s nudge off the kerf; the kept board gets a static hull.
- **Millimetre tolerances.** Godot's physics engines are tuned for metre-sized objects:
  Jolt, the default, lets contacts overlap by 2 cm. `project.godot` sets the overlap to
  0.2 mm (`[physics]`), so a 25 mm offcut rests on the bench instead of sinking into it.
- **Measured** (`game/tests/offcut_physics`, 1 s after the saw goes through; Jolt's figures
  match its 0.2 mm slop). `tools/compare_physics.sh` repeats it under Box3D, Erin Catto's
  new engine, through the experimental
  [godot-box3d](https://github.com/bearlikelion/godot-box3d) extension (built from source,
  never committed):

  | | Jolt (default, tuned), box | Jolt, hull | Box3D, box | Box3D, hull |
  | --- | --- | --- | --- | --- |
  | sinks into the bench at rest | 0.20 mm | 0.20 mm | 0.09 mm | 0.16 mm (4.2 at worst) |
  | tilts | 0° | 0.5° | 0° | 0.7° |
  | slides off the kerf | 5.7 mm | 5.4 mm | 0.9 mm | 2.2 mm |
  | per physics step | 0.25 ms | 0.30 ms | 0.24 ms | 0.27 ms |

  Both handle it. Box3D's friction combines differently, hence the shorter slide. It is
  still alpha, as is its Godot extension, so the default stays Jolt.

## Layout

| Path | What it is |
| --- | --- |
| `native/src/core/shared/*.glsl` | The SDF formulas — primitives, tool cross-sections, swept strokes, blend modes, materials. Written in a subset that compiles **both as C++ and as Godot shader code**, so the CPU evaluator and the GPU raymarcher run the same maths. Edit these only. |
| `native/src/core/glsl_compat.h` | Just enough GLSL (`vec3`, `mix`, `clamp`, …) in C++ to compile the shared files. |
| `native/src/core/body/` | `Body`, `Edit`, `Primitive`: the per-part source of truth, bounds and Lipschitz bounds; smoothing layers (`layer.h`); the material table. |
| `native/src/core/compile/` | The octree: per-cell pruned edit lists ("tapes") that keep per-query cost independent of edit count. |
| `native/src/core/adf/` | The adaptive distance field: the Live display cache (sampled bricks, exact cells at creases). |
| `native/src/core/eval/` | The CPU reference renderer (ground truth for every later GPU path) and exact ray queries. |
| `native/src/core/tools/` | The hand tools: each one's model and the cuts it makes, strokes that turn a tool's motion into edits, and the curvature flow that builds a sanding sponge's smoothing layer (`smoothing.h`). |
| `native/src/core/pieces/` | Bodies that come apart: whether a cut left two parts (`plane_clear`), and each piece's bounds, volume, centre of mass and hull points. |
| `native/src/core/edit/` | `EditSession`: a body's edits with the stroke in progress and undo / redo, its octree and ADF kept up to date incrementally. |
| `native/src/demo/`, `native/tools/` | Demo scenes; `sdf_gallery` (renders the galleries), `sdf_render` (renders any demo, diffs against another image) and `sdf_bench` (octree scaling). |
| `native/src/godot/` | The GDExtension: `SdfBody`, a node that raymarches a body live, and the GPU brick sampler. |
| `native/tests/` | Property tests for the core and golden-image tests. No Godot needed. |
| `game/` | The Godot 4.7 project. `game/workshop/` is the workshop (the main scene); `game/shaders/sdf/` holds byte-identical copies of the shared files plus `sdf_live.gdshader`; `game/bench/` the decision-gate benchmark. |
| `extern/godot-cpp/` | godot-cpp 10.0.0 (submodule), built against the Godot 4.7 API. |
| `tools/` | `sync_shaders.sh`, `run.sh` (headless / Xvfb scene runner, OpenGL or Vulkan), `test_godot.sh`, `parity.sh`, `compare_physics.sh` (Jolt vs Box3D on a sawn offcut). |
| `docs/PLAN.md` | The plan: principles, what is done, and the roadmap from here. The first plan is archived in `docs/archive/plan-v1.md`. |

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
ambient occlusion. It is slow on purpose — it is the ground truth the GPU path is
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

## In Godot: the Live path

`SdfBody` (a `MeshInstance3D`) compiles its body into the octree and an **adaptive distance
field** (ADF, below), and flattens both into data textures. A proxy box around the body
runs the Live shader (`sdf_live.gdshaderinc`), which sphere-traces each pixel in the body's
own millimetres. It writes the true hit's depth, normal and position (`DEPTH`, `NORMAL`,
`LIGHT_VERTEX`), so Godot lights and composites it like any mesh. Adding a stroke updates
the touched cells and bricks and uploads only what changed; nothing is meshed.

**The ADF.** Interpreting edit tapes per pixel costs the tape's length in texel fetches on
every frame, for edits that never change. So each edit is evaluated once per affected
voxel instead, when it is made:
- The octree continues into bricks of 8³ half-float distance samples along the surface.
- Each cell is refined until trilinear reconstruction is within 10 µm of the exact field
  near the surface. Planes are exact under trilinear filtering, so flat and gently curved
  faces get coarse bricks (up to 1 mm between samples).
- Creases would need ever finer bricks, so a crease cell ≤ 1 mm becomes an *exact* cell
  (≤ 7 mm right after an edit, until it is refined: see "Edits in milliseconds"). Rays
  march on its brick while they are further from the surface than the brick's error band,
  then evaluate its own tape: the octree tape pruned again to that small cell, which leaves
  only the 2–3 edits forming the crease. Hits, normals and materials there are exact. The
  band is measured when the cell is built (twice the worst brick-vs-tape difference near
  the surface), some 30× narrower than the worst case L · voxel · √3, so rays reach the
  tape later and need fewer tape steps.
- Lookups start from a 64³ grid over the root cube that names each block's node, a few
  levels above its leaves, instead of descending a dozen levels from the root.
- Normals come from the hit's own leaf: four taps on its brick, or one pass over its tape
  for all four taps in an exact cell. The tape interpreter is inlined wherever the shader
  calls it, so it is called from three places only.
- Bricks live in a `Texture2DArray` (512² layers of 512 bricks) and are read with hardware
  filtering. An edit
  re-samples the cells it reaches, reuses the rest (slots and all) and uploads only the
  layers that changed. "Reaches" is decided by pruning, not by the edit's bounding box: a
  long curved stroke's box holds several times more cells than its groove touches.
- The exact field stays the truth: bricks are a display cache, like a baked mesh.
  `live_source = EXACT` raymarches the tapes alone, for comparison.

`sdf_bench adf` (4 shared cores; per stroke before W4, at 5 µm and refined in full at
once, in parentheses):

| Body | Edits | Build | Memory | Exact cells | Per stroke |
| --- | --- | --- | --- | --- | --- |
| carved panel | 52 | 0.55 s | 16 MB | 4,616 | 6.9 ms (28) |
| fluted ball | 6 | 0.05 s | 3.0 MB | 0 | 2.2 ms (6) |
| random session | 227 | 2.5 s | 29 MB | 14,025 | 23 ms (51) |
| random session | 1,495 | 15 s | 50 MB | 22,516 | 60 ms (182) |

Nearly all of that is evaluating primitives at brick samples (callgrind: 80%), a quarter
of it in the Bézier strokes' cubic solve.

The shader is bound by texture fetches, as Claybook's SDF tracer was. `sdf_bench count`
replays it on the CPU for the benchmark's views and counts texel fetches per pixel:

| View | Exact tapes (E3b) | ADF (E3d) | ADF, measured bands and grid (E3e) |
| --- | --- | --- | --- |
| panel filling the screen | ~1070 | 129 | 64 |
| close-up of the rosette | ~1670 | 262 | 128 |
| 2000 random strokes | ~15,000 | 765 | 335 |

![A fluted walnut ball, raymarched live in Godot: it casts its shadow on the floor, and a mesh bar pushed into it is cut exactly where it enters the surface](docs/images/live_sphere.png)

**Parity.** `tools/parity.sh` renders every demo through Godot and diffs it pixel by pixel,
from the same camera, against two CPU renders:
- the formula field (every edit, in order, no octree): the ground truth;
- the CPU tracing the same source (the ADF), the same algorithm, so any mismatch is a
  shader bug.

Normals check geometry and unlit albedo checks materials. The 27 cases are the 8 blend
modes, 6 material scenes, the carved panel, the fluted ball, a 300-stroke session and the
sanded block (smoothing layers), at 1280x720.
- **Against the ground truth:** the mean channel difference is at most 0.34 / 255, and at
  most 0.16% of pixels differ by more than 24 / 255. Those are single pixels on silhouettes
  and creases, where a pixel centre falls on one side of the edge or the other.
- **Against the CPU ADF:** at most 0.007% of pixels differ.

The exact Live path (`PARITY_ARGS=--live-source=exact`) matched the ground truth within
0.23 / 255 and 0.05%. It skips the sanded block: it draws bodies without their layers. Results are identical with the node scaled to a metre world
(scale 0.001).

What was checked in Godot (Compatibility renderer, the only one without a GPU):

| Check | Result |
| --- | --- |
| Depth composition with meshes | exact: meshes pushed into a body are cut at its surface |
| Casting shadows | works: the shadow pass runs the raymarch and takes its depth (opt-in, see below) |
| Receiving shadows | Forward+ / Mobile look shadows up per fragment at `LIGHT_VERTEX` (from Godot's source, to confirm on hardware); Compatibility computes shadow coordinates per vertex, on the proxy box, so there Live bodies receive no shadow-map shadows |
| Orthographic cameras (directional shadow passes are orthographic) | works |
| Metre-scaled world | works, parity unchanged |

**The decision gate** (plan §4) runs `game/bench/live_bench.tscn` on real hardware. The
first run (RTX 3060 Ti, Forward+, 1280x720) failed it clearly:

| Scenario | GPU median | Target (at 1080p) |
| --- | --- | --- |
| panel filling the screen | 19.1 ms | 6 ms |
| close-up of the rosette | 40.7 ms | 6 ms |
| 2000 random strokes | 283.9 ms | 6 ms |
| three bodies | 21.6 ms | 8 ms |

Where the time went (counted by replaying the shader on the CPU, and timed with Mesa):
- raymarching again in every shadow pass: about two thirds of the frame;
- Forward+ running the shader twice (depth prepass and colour pass);
- evaluations after the hit (refine, normal, AO): about 75% of the rest;
- about 8 texel fetches per edit in each evaluation.

The measured times track the fetch count (close-up ≈ 1.6× the panel's, 2000 strokes ≈ 14×).

What changed:
- Live bodies no longer cast shadows by raymarching unless `live_shadows` is on; the
  baked mesh (E4) will cast them. When they do, the caster marches bricks only: a shadow
  map cannot resolve an exact cell's error band.
- They draw in one pass (`sdf_live_single.gdshader`: the transparent pipeline, which skips
  the depth prepass but still writes depth). This also fixed speckle on Forward+: after its
  prepass, Forward+ redraws opaque materials with an exact-equality depth test, and a
  raymarched depth recomputed by a separately compiled shader variant misses it on some
  pixels. With `live_shadows` on, an internal shadows-only child runs the opaque variant
  in shadow passes, where no such test applies.
- The ray and the post-hit taps stay in their octree cell.
- Rays end at the opaque scene's depth (read in the single pass), so the parts of a body
  hidden behind meshes cost nothing.
- Edit, node and tape records are packed tighter.

Parity is unchanged. On Mesa that is 5.5× faster, and rendering 3D at half resolution
another 3.4×. The ADF (above) then removed the tape-length term from the per-pixel cost.

Run it after building (below):

```sh
godot --path game res://bench/live_bench.tscn                                   # Forward+
godot --path game --rendering-method gl_compatibility res://bench/live_bench.tscn
```

Use a 1920x1080 window if the screen allows. The switches after `--` show where time goes:
`--shadows=off`, `--live-shadows=on`, `--live-source=exact`, `--exact=off` (the ADF's crease
cells shade from their bricks, to measure what their tapes cost), `--ao=off`, and
`--scale3d=0.5` for half-resolution 3D. `--view=steps` shows step-count heat maps, and `--shots=<dir>`
saves each scenario.

## Building and testing

The core has no dependencies; the tests and the image tools need zlib (for PNGs), and the
Godot extension needs the godot-cpp submodule. The first build compiles godot-cpp's
bindings, which takes a few minutes; `-DSDF_BUILD_GODOT=OFF` skips the extension.

```sh
git submodule update --init
cmake -S native -B native/build -G Ninja
cmake --build native/build        # also writes game/bin/sdf_godot.<os>.<ext>
native/build/sdf_tests            # property tests (exactness, compact support, Lipschitz
                                  # bounds, no phantom surfaces, material weights, culling)
                                  # and golden images of the demo scenes
native/build/sdf_gallery out 2 2  # re-render the gallery images at 2x, 2x2 supersampled

GODOT=/path/to/godot tools/test_godot.sh   # shader compile checks, the extension, live
                                           # renders, the workshop, stroke previews and
                                           # GPU/CPU parity (needs xvfb-run and Mesa;
                                           # several minutes on llvmpipe); with a Vulkan
                                           # driver, also GPU brick sampling (Forward+)
GODOT=/path/to/godot tools/run.sh --vulkan res://tests/gpu_bricks.tscn   # GPU vs CPU bricks,
                                           # with build and refinement times
native/build/sdf_render carved_panel out/panel.png --view normals   # any demo, any view
```

After editing anything in `native/src/core/shared/`, run `tools/sync_shaders.sh`; the
test suite fails if the Godot copies drift. After an intended visual change, rewrite the
golden images with `SDF_UPDATE_GOLDEN=1 native/build/sdf_tests golden` — and look at them
before committing. The goldens are bit-identical across GCC and Clang, optimised or not,
on x86-64; the tolerances are there for MSVC and ARM.
