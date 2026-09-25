# Project context: the object builder's SDF engine

Read this first. It covers what the project is, how it is built, what exists today, what
comes next and how to work on it. Details live elsewhere, and this file points to them:
- [README.md](README.md): as built, with measurements;
- [docs/PLAN.md](docs/PLAN.md): the plan, principles and roadmap;
- [docs/archive/plan-v1.md](docs/archive/plan-v1.md): the original design and the
  detailed Bake design;
- [CLAUDE.md](CLAUDE.md): building and testing in this environment.

## 1. What this is

A crafting game, an **object builder**. Players make working objects from parts shaped
by real processes, then join them. The acceptance scene is a **pickaxe**: a steel head, an
ash handle and a wedge. The world will also have buildings and people, which stay
ordinary meshes.

The processes depend on the material:
- **wood** is cut with sharp tools;
- **metal** is forged hot with blunt ones;
- **stone** is shaped by percussion.

Parts never blend into each other. They join by interlock, compression or glue, and each
joint's strength is computed from the shapes.

The core of it all is an **SDF engine**. Every part is a signed distance field: an
analytic base shape plus an ordered list of edits (tool strokes, fillets, inlays, paint),
each combined with a chosen blend mode. The engine gives:
- **unlimited undo**, since the edit list is the history;
- **exact material queries** (material, grain and hardness at any point);
- **exact contact queries** (tool against part);
- **manifold export**, later, through meshing.

It is built as:
- a C++ core with no dependencies (`native/src/core`), engine-agnostic by rule;
- a Godot 4.7 GDExtension on top of it (`native/src/godot`, the `SdfBody` node);
- the Godot project (`game/`), whose main scene is a **woodworking workshop**: a board on a
  bench and eight kinds of hand tool, every one an SDF body.

The repository's name (SQLDEMO) is historical: it began as a throwaway Godot SQL demo that
the engine replaced.

## 2. Decisions and principles

Decided at the start (docs/archive/plan-v1.md, "Context"):

| Decision | Choice |
| --- | --- |
| Engine | Godot 4.7 plus a C++ GDExtension; the SDF core stays engine-agnostic |
| Renderer | A Godot hybrid: bodies being edited are **raymarched live**, finished ones will be **baked** to meshes. A measured decision gate chooses between this and custom compute passes |
| Look | Deferred. The engine is validated at full resolution first. A semi-realistic, PS2-style look comes later as a presentation layer (settings, not architecture) |
| Input | Desktop, mouse and keyboard |
| Scale | Details of about 0.1 mm on small parts, up to stone blocks of 2–3 m |
| Joining | Parts never blend. Interlock, compression and glue joints with computed strength |
| Undo, export | Unlimited undo; manifold mesh export |

The principles everything is built by (docs/PLAN.md):
1. **The SDF is the truth; everything else is a cache.** ADF bricks, bakes and physics
   shapes may lag behind the SDF, but they are never the source.
2. **One source for op formulas.** Shared C/GLSL includes (`native/src/core/shared/*.glsl`)
   compile both as C++ and as Godot shader code. The CPU evaluator, the Live shader and the
   GPU brick sampler all run the same maths, and parity tests enforce it.
3. **Edits cost nothing while the tool moves.** The shader draws the stroke in progress.
   On release it is applied once, on a worker. Creases land coarse within milliseconds
   and are refined when idle.
4. **Representation by process.**
   - Cuts are analytic edits.
   - Smoothing is a sampled layer.
   - Forging will be a sampled base grid.
   - Separate parts are separate bodies.
5. **Every step ends green, committed and pushed,** with the README and docs/PLAN.md
   updated to match.

## 3. How it is built

### The core (`native/src/core`)

| Area | What it holds |
| --- | --- |
| `shared/*.glsl`, `glsl_compat.h` | The formulas: primitives, tool cross-sections, swept strokes, the blend modes (hard, chamfer, round, smooth, profile, engrave, groove, tongue) with material weights, the materials (wood rings and fibre, metal, stone). Just enough GLSL in C++ to compile them |
| `body/` | `Body` (base shape, edit list, material, grain origin and axis), `Edit`, `Primitive`, bounds and Lipschitz bounds. Smoothing layers (`layer.h`: `Op::Layer`), regions (`region.h`: `Op::Keep`), the material table (colours, density, Janka hardness, split, tear-out) |
| `compile/` | The **octree**: each cell holds the edits that can affect it (its *tape*), pruned by interval bounds. Queries cost the local tape, not the whole edit count. Updated incrementally |
| `adf/` | The **adaptive distance field**, the Live display cache. Bricks of 8³ half-float samples along the surface, refined until trilinear reconstruction is within 10 µm. Crease cells become *exact* cells that evaluate their own short tape. A 64³ lookup grid |
| `edit/` | `EditSession`: a body's edits, the stroke in progress, undo and redo, the octree and ADF kept up to date incrementally |
| `eval/` | The CPU reference renderer (ground truth for every GPU path, golden images) and exact ray queries |
| `tools/` | The hand tools: each one's model, and strokes that turn motion into edits. The cutting model (`cutting.h`), shaping (`shaping.h`: rasps, scraper, spokeshave), the sponge's curvature flow (`smoothing.h`), debris reports (`debris.h`), the variant catalog |
| `pieces/` | Bodies that come apart: `plane_clear` (a cut through), `find_parts`, `find_island`, `cut_out` (islands no plane separates), and volumes, centres and hull points |

### The Godot side

- **`native/src/godot/sdf_body.{h,cpp}`**: `SdfBody`, a `MeshInstance3D`.
  - It holds an `EditSession` and runs the edits on a worker thread. Its jobs are
    STROKE, COMMIT, CHECK (islands, when idle) and REFINE (crease cells, when idle).
  - It uploads only the brick layers that changed, and draws the body with the Live
    shader on a proxy box.
  - Its API covers loading demos, boards and tools, raycasts, planning strokes
    (`plan_stroke`), making them (`begin_stroke`, `move_stroke`, `end_stroke`), undo and
    redo, `split` and `rejoin`, hull points and collision hulls, mass and volume, and
    debris (`take_debris`, `albedo_at`).
- **`native/src/godot/gpu_sampler.cpp`**: samples ADF bricks with a compute shader where
  the renderer has a `RenderingDevice` (Forward+, Mobile).
- **`game/shaders/sdf/`**: byte-identical copies of the shared includes (keep them in step
  with `tools/sync_shaders.sh`) and the Live shader. It sphere-traces in the body's own
  millimetres and writes true depth, normal and position, so Godot lights and composites
  it like any mesh.
- **`game/workshop/`**:
  - `workshop.gd`: the scene, tool driving, offcuts and physics;
  - `workshop_ui.gd`: the panel and status line;
  - `orbit_camera.gd`;
  - `debris.gd`: shavings and chips.
- **`game/tests/`**: scenes driven headless (logic) or rendered (images). **`game/bench/`**:
  the decision-gate benchmark.

**Units.** Bodies are in millimetres with z up. The world is in metres with y up: each
body node is scaled by 0.001 and turned −90° about x.

### The life of a stroke

1. **Pick a tool and point.** An outline shows what it would touch. The tool in hand stays
   hidden until it acts.
2. **Plan it (optional):** hold the right button.
   - `SdfBody.plan_stroke` runs the stroke along its path without making it. For
     chisels, gouges and the spokeshave, the cutting model works it out against the wood
     (`tools/cutting.h`): force against a hand's 200 N, grain, clearance, tear-out.
   - The shader hatches the planned cut. The wheel sets intensity.
   - Or just **left-drag**: after 2 mm the drag gives the direction, and the same plan is
     made there and then, not drawn.
3. **Act.** As the tool moves, the stroke's cut so far, merged into a few edits, goes to
   the shader as the *overlay* (up to 16 edits), which cuts it into the field per pixel.
   There is no CPU work per move. The stroke also reports what it takes off (shavings,
   chips), read from the body as it was before the stroke.
4. **Release.** The merged edits are committed once, on the worker. The octree and ADF
   update incrementally: cuts fold into the existing bricks, and creases land in coarse
   exact cells (3–9 ms). Only the changed brick layers are uploaded, and the overlay
   drops in the same frame.
5. **Afterwards, on the worker:**
   - a saw stroke that went through runs `plane_clear` and measures both sides; any other
     cut, once idle, runs `find_island` round the cut;
   - if a part came away, the body emits `separated`, and `split` makes it a body of its
     own at once (about 3 ms of the frame). It becomes a rigid body.
   - when nothing is queued, creases are refined.
6. **Undo** takes a stroke back, with its shavings and chips. Straight after a split, undo
   rejoins the pieces.

## 4. What is done

Every milestone ended green, committed and pushed. The numbers are from `sdf_bench` on 4
shared cores and from the tests; the README has the tables.

| Milestone | Delivered |
| --- | --- |
| E0–E2 | The shared C++/GLSL op library: primitives, swept tool profiles, the blend modes with material weights, the evaluator with property tests, the CPU reference renderer, golden images, the octree with interval pruning and incremental updates |
| E3, E3b–E3e | The Godot Live raymarch; the ADF display cache (bricks, exact crease cells, measured error bands, lookup grid); a speckle fix; shadow casters; depth early-out. Texel fetches per pixel fell from about 1,070 to 64 on the panel, and parity against the CPU ground truth holds across 27 cases |
| W1 | The workshop: board, chisel, saw and sanding block as SDF bodies, raycasts, `EditSession` with undo and redo, edits on a worker |
| W2a | Strokes drawn by the shader while the tool moves, applied once on release, merged |
| W2c | The sanding sponge: a curvature-flow smoothing layer (`Op::Layer`) |
| W4 | Precision of 10 µm, crease cells refined when idle, cuts folded into old bricks, small uploads, the GPU brick sampler. Commits take 3–9 ms (from 57–95) |
| T1 | Plan a stroke with the right button (hatched preview, the wheel for intensity) or left-drag directly; the tool hidden until it acts |
| T2 | Chisels and gouges cut as the wood lets them: force by Janka hardness and grain, clearance past the bevel, open-face entry, tear-out against the grain, breakout, chopping with mallet blows, pop-off chips. Variants: bench, paring, mortise and skew chisels; #3 and #7 gouges, a veiner, a V-tool |
| T3 | Rasps (coarse to fine, tilted to chamfer, the round face hollowing), a card scraper, and a spokeshave whose sole follows curves and bridges hollows |
| P1 | Sawn through, the board comes apart: `plane_clear` (about 13 ms), both sides measured on the worker, and `split` in about 3 ms of the frame. The offcut is a rigid body; undo rejoins |
| P2 | Islands: cuts meeting free a piece no plane separates. `find_parts` (the whole board in about 35 ms), `cut_out` (a region proved apart), `Op::Keep`. The board's collider becomes convex pieces round the hollows. A rebate: about 600 ms on the worker, 3.5 ms to split |
| Tests | Split into tiers: a headless tier (logic, plus a one-frame shader compile check; about 20 s) and a GPU tier (renders, image comparisons, parity, Vulkan) |
| D1 | Shavings and chips: a stroke reports what it takes off (`tools/debris.h`). A shaving curls off the edge as it goes, coloured by the wood, breaks by the grain and comes away as a rigid body. Tear-out and pop-offs throw chips; undo takes them back. A shaving is within 1% of the volume the board lost |

Test counts today: 90 native tests (GCC and Clang, including golden images) and 8
headless Godot checks.

## 5. Where things stand

### Open measurements (they need a real GPU; the reference machine is an RTX 3060 Ti)

The **decision gate** (`game/bench/live_bench.tscn`) failed on its first run: 19 ms for a
panel filling the screen, against a 6 ms target at 1080p. Since then:
- shadows by raymarching became opt-in;
- the shader draws in a single pass;
- rays end at the scene's depth.

Together those made it 5.5× faster on Mesa. Then the ADF replaced tape interpretation,
for 13 to 45 times fewer texel fetches per pixel. But **the gate has not been re-run on a
GPU**. Also waiting on hardware:
- `tools/run.sh --vulkan res://tests/gpu_bricks.tscn`: GPU against CPU brick times;
- per-pixel cost while crease cells are still coarse.

The results decide three things:
- **O3**, a 3D brick atlas;
- the default for `update_exact_cell`;
- whether Live bodies need custom compute passes. That is "option B" in the gate. Only
  if both performance and lighting integration fail does the own-engine question reopen.

The development environment has no GPU. Mesa's llvmpipe renders at about 2 s a frame,
so correctness can be checked here, but not speed.

### Known limitations (deliberately left for later)

- **Islands:**
  - one island per check;
  - the region is large for long interfaces (a rebate needs about 160,000 cubes);
  - crumbs under 1 mm³ stay in the body (D3).
- **Physics:**
  - debris colliders are boxes;
  - the board's collider is a single hull unless islands have hollowed it (then convex
    pieces). Proper meshes come with the Bake.
- **Shadows:** under the Compatibility renderer, Live bodies receive no shadow-map
  shadows (coordinates are per vertex on the proxy box). Forward+ and Mobile look them
  up per fragment, which is still to be confirmed on hardware.
- **Other:** redo doesn't bring back offcuts or debris; there is no mesh export yet.

## 6. What comes next

In order, per docs/PLAN.md's roadmap. Each capability replaces several overlapping items
of the first plan.

### 2. Debris (under way: D1 done)

**D2: dust** from the saw, rasp, scraper, sanding block and sponge.
- **What each tool reports.** Each stroke's `debris()` works out what it removed:
  - **saw:** Δdepth × kerf × the kerf's chord through the material, sampled along the
    blade, puffed at both ends of the chord;
  - **rasp, scraper, block:** Δdepth × footprint × the fraction of it over material;
  - **sponge:** exact, from the samples its flow moves out of the material.
- **Grain size and colour.** Grains are 0.2–0.8 mm by tool, coloured by `albedo_at` and
  lightened (dust scatters light).
- **Grains of its own** (a MultiMesh), not `CPUParticles3D`, which cannot collide:
  - each grain's landing point is found once, by casting its ballistic arc against the
    board and the physics space;
  - grains stack in a 1 mm height grid, so sawdust heaps under the kerf's ends and
    sanding dust lies on the face;
  - grains whose support is cut away fall again;
  - at most 30,000 grains, oldest first out;
  - undo removes a stroke's dust.

**D3: small pieces become debris.**
- One check collects every part that came away. Parts under about 30 mm³ (crumbs under
  1 mm³ included) are cut out together with one region and one `Op::Keep`.
- Each becomes a debris chunk: a hull mesh from a small quickhull (new:
  `native/src/core/pieces/hull.{h,cpp}`), coloured per vertex, with a convex collider. It
  is reported by a new `crumbled` signal. P1 offcuts under the threshold go the same way.
- Undo straight after drops the Keep.

### 3. Surface finish

Tool marks, scratches and sanded edges as shading first:
- **A finish grid:** a coarse RGBA8 grid (1.5–2 mm voxels) over each body. It holds
  mark amplitude, scratch amplitude, their direction, and the bevel radius still to be
  applied.
- **Filled by the tools:** tools splat it on the main thread.
- **Drawn by the Live shader:** it turns the grid into normals and roughness
  (anisotropic scratches, fading marks).
- **The sponge:** it only splats while moving. On release, one worker job builds its
  smoothing layer, handing over like the overlay does.
- Bakes read the grid too.

### 4. Bake

Meshes for distance display, physics and export. The detailed design is plan-v1's "E4
implementation plan" (tasks E4a–E4e):
- dual contouring from the octree field, manifold by construction;
- QEM decimation checked against the SDF;
- xatlas UVs;
- normal, albedo, AO and finish maps;
- a Baked display with Live↔Baked switching by screen-space error;
- OBJ and STL export;
- convex decomposition for physics;
- a Live body's latest bake casts its shadows.

### 5. Assemblies

- **Joints:**
  - interlock (eye and handle, dovetail, mortise and tenon);
  - compression or interference fit (wedges): pressure from the overlap, hold = μ ∫ p dA;
  - glue: area within the glue line, weighted by grain.
- **Strength** is sampled over the contact, which also gives a fit heatmap.
- **One rigid body:** an intact assembly is one `RigidBody3D` with compound shapes.
- **Failure:** overload loosens a joint, and failure splits the assembly (Pieces).
- **Acceptance:** the pickaxe.

### 6. Fracture

Crack surfaces by material:
- wood splits along the grain;
- stone breaks in conchoidal chips;
- metal fails ductile.

The crack is the cutter for Pieces (P2's region and `Op::Keep`); fragments under the
threshold are Debris.

### 7. Forging

- A sampled base grid under the edit list.
- A temperature field.
- Hammer blows as deformation: advection, redistancing, volume correction.
- Malleability comes from the material's forging window.

### 8. Scale

A 2 m stone block with 100,000 percussive edits:
- consolidation of long edit runs into sampled bases;
- a second level of lookup grid;
- dirty-chunk rebakes;
- O3 if the GPU numbers ask for it.

### Later: the look

A semi-realistic, PS2-style pass: low internal resolution, low-poly bakes, texel-quantized
materials. Every detail knob is already a parameter, so it is a settings change.

## 7. Working on it

**Build and test** (see [CLAUDE.md](CLAUDE.md)):

```sh
git submodule update --init
cmake -S native -B native/build -G Ninja && cmake --build native/build   # also builds game/bin/sdf_godot.*
native/build/sdf_tests                                                   # 90 tests, golden images
GODOT=/path/to/godot tools/test_godot.sh                                 # headless tier (about 20 s)
GODOT=/path/to/godot tools/test_godot.sh --gpu                           # GPU tier: renders, images, parity
```

Also build with Clang (`-DCMAKE_CXX_COMPILER=clang++ -DSDF_BUILD_GODOT=OFF`, in a separate
build directory).

**Conventions.**
- Work in steps. Each step ends with the native tests (GCC and Clang) and the headless
  tier green. The README section and docs/PLAN.md are updated, then it is committed and
  pushed.
- Tests don't depend on visual output: new checks run headless, and anything that
  compares images goes in the `--gpu` tier.
- On a machine without a GPU, run only the rendered scene a change affects
  (`tools/run.sh --render res://tests/<scene>.tscn`), not the whole GPU tier.
- After editing `native/src/core/shared/`, run `tools/sync_shaders.sh`.
- Rewrite golden images only for an intended visual change
  (`SDF_UPDATE_GOLDEN=1 native/build/sdf_tests golden`), and look at them first.
- Code and docs are written plainly, in the style of the surrounding code: comments say
  why, and units are stated (mm, m, mm³, N).

## 8. Lessons worth not relearning

- **Physics at millimetre scale (Jolt, Godot's default):**
  - Godot's default shape margin of 4 cm rounds millimetre shapes away. Use 0.1 mm.
    `project.godot` sets 0.2 mm tolerances.
  - A body's first physics step finds no contacts. So a piece is set just into what it
    rests on, and let go asleep when supported.
  - Jolt discards triangles of 1–2 mm as degenerate, so there are no trimesh colliders
    until the Bake.
  - Hull points closer than about 1 mm make sliver faces that rock. A hull round a curled
    shaving rocked from facet to facet and never settled; a box rests.
  - Bodies lighter than a few grams are shaken about: debris weighs at least 5 g to the
    solver.
- **Threads.** Strokes and plans read the body on the main thread, so they wait for
  `body_settled()`. REFINE and CHECK jobs only read, so they don't block. A previewed
  stroke leaves the body alone until it commits, so its debris is read from the body as
  it was before the stroke.
- **GDScript.**
  - `:=` needs a type it can infer.
  - A parse error in a test scene makes a headless run hang until its timeout.
  - A relative `--out` path resolves against `game/`.
  - A variable whose type was inferred (`var shape := ConvexPolygonShape3D.new()`) can't
    later hold a `BoxShape3D`: declare it `Shape3D`.
- **The board's arrises are rounded.** A stroke pressed within a few millimetres of an
  edge takes the rounded surface's tangent plane and cuts little. Tests start strokes
  well inside the faces.
- **Rendering here** is software: about 2 s a frame on llvmpipe, and the rendered tiers
  take minutes. Lavapipe gives Vulkan (Forward+) for correctness only.
- **Volumes.** The ADF's voxel volume can't resolve a half-millimetre cut. Measure what a
  cut removed with columns of raycasts before and after.

## 9. Where to read more

- **The workshop and how each part works** (README.md):
  - "The workshop";
  - "Edits in milliseconds";
  - "Sanding as smoothing";
  - "Chisels and gouges cut as the wood lets them";
  - "Shaping and finishing";
  - "Pieces";
  - "Islands";
  - "Shavings and chips".
- **The engine** (README.md): "Blend modes", "Materials", "Scaling to many edits: the
  octree", "In Godot: the Live path" (the ADF, parity, the decision gate).
- **The plan:** [docs/PLAN.md](docs/PLAN.md) (roadmap and as-built notes per milestone)
  and [docs/archive/plan-v1.md](docs/archive/plan-v1.md) (the body model, joining, the
  rendering decision, detail and scale, and the full Bake design).
- **Key headers:**
  - `native/src/core/body/body.h`;
  - `native/src/core/compile/octree.h`;
  - `native/src/core/adf/adf.h`;
  - `native/src/core/edit/session.h`;
  - `native/src/core/tools/cutting.h`;
  - `native/src/core/tools/debris.h`;
  - `native/src/core/pieces/parts.h`;
  - `native/src/core/body/region.h`;
  - `native/src/godot/sdf_body.h`.
