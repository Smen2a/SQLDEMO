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
- the Godot project (`game/`), whose main scene is a **woodworking workshop** you walk
  about in first person: a bench with a vise, a side table, a lumber rack of ash, oak and
  walnut, and eight kinds of hand tool on a hotbar. Every tool and every piece of work is
  an SDF body; pieces are picked up, carried and clamped.

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
| Physics | Jolt, named in `project.godot`, tuned for millimetres (0.2 mm overlap) |

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
| `tools/` | The hand tools: each one's model, and strokes that turn motion into edits. The cutting model (`cutting.h`), shaping (`shaping.h`: rasps, scraper, spokeshave), the sponge's curvature flow (`smoothing.h`), debris reports (`debris.h`), edges to lock onto (`edges.h`), the variant catalog |
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
  - `room.gd`: the room, built in code (light, walls, the bench and its vise, the side
    table, the rack and its stacks);
  - `player.gd`: the first-person player (a `CharacterBody3D`);
  - `workshop.gd`: the two modes (walking, working at the bench), pieces of work as rigid
    bodies, carrying, the vise, the rack, tool driving at the bench, offcuts and physics;
  - `workshop_ui.gd`: the hotbar, the crosshair and its prompt, the panels and status line;
  - `orbit_camera.gd`: the view over the bench;
  - `debris.gd`: shavings and chips (they fade 2 s after landing); `dust.gd`: dust (a puff,
    and piles on the ground).
- **`game/tests/`**: scenes driven headless (logic) or rendered (images). **`game/bench/`**:
  the decision-gate benchmark.

**Units.** Bodies are in millimetres with z up. The world is in metres with y up: each
body node is scaled by 0.001 and turned −90° about x.

### The life of a stroke

1. **At the bench** (E with a piece in the vise), **take a tool from the hotbar and
   point.** An outline shows what it would touch. The tool in hand stays
   hidden until it acts.
2. **Plan it (optional):** hold Space. Right-drag (the guiding hand) pivots the tool on
   its edge, hovering or planned: its angle, skew or turn, and lean (the wheel).
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
| Tests | Split into tiers: a headless tier (logic, plus a one-frame shader compile check; about a minute) and a GPU tier (renders, image comparisons, parity, Vulkan) |
| D1 | Shavings and chips: a stroke reports what it takes off (`tools/debris.h`). A shaving curls off the edge as it goes, coloured by the wood, breaks by the grain and comes away as a rigid body. Tear-out and pop-offs throw chips; undo takes them back. A shaving is within 1% of the volume the board lost. They fade away 2 s after coming to rest |
| D2 | Dust: the saw, rasps, scraper, sanding block and sponge report the wood they take (the sponge's flow measures its own). A quick puff: grains fly, land where their arc meets something and go. What reaches the ground piles up as one mound per spot (sawdust beyond the kerf's ends), until Sweep. Every tool's dust is within 1.5% of what the board lost |
| W5-5 | Pieces and debris land and lie still. The project runs Jolt, named (left at DEFAULT it had been GodotPhysics3D all along, where small light bodies never came to rest). Shavings and chips rest on the board in the vise (a birth rule meant for loose pieces had them falling through it). Continuous collision detection on pieces and debris, a plane for the floor, tool pushes that never move a piece into anything, old debris fading rather than freezing in mid-air, a vise that won't clamp over a loose piece. `physics_calm` measures it all |
| G3b | Putting parts together: a part carried to its mate in the vise lines up with the joint (a ghost), E puts it on, the wheel pushes it in until it binds, clicks tap a tight fit home with the mallet, E lets go: one rigid body, the tools working on whichever part is under the pointer. Undo straight after, or F and the wheel back, takes it out again |
| G3a | A joint's fit: how a part goes into its mate (a tenon into its mortise, a wedge into its kerf) and, pushed along the joint, where it binds: loose, snug (home by hand), drives (home with the mallet) or won't go. Measured on the part's surface against the mate's exact field, 20–35 ms for the handle into the head |
| G2 | Checking a part against its drawing (K at the bench): the wood compared with the part as drawn on a millimetre grid, each surface measured against the other; proud (wood to take off) and short (wood gone) places over 0.5 mm joined into spots, named by feature or face, with dots on the wood. Unfinished work shows as proud: what is left to do. On the way, `Body::sample` stopped skipping an edit at a point on the surface inside its box |
| G1b | Drawing a plan: a pad of paper on the side table (and New / Edit / Edit a copy in the plan book); a plan's parts (wood, count, blank size, the stock it comes from) and joints; features drawn with the mouse on the part's three views (hole, tenon, kerf, rebate, chamfer, taper), shown as the drag goes, picked to set their sizes exactly; saved to `user://plans`, in the book as the player's, laid out as a preset is |
| G1a | Plans: a part is a blank with features; the mallet's plan (head, handle, wedge); the plan book on the side table (P), each part's sheet drawn in three views; a sheet laid on the piece in the vise draws the part on in pencil and makes the piece that part; the knife and gauge take pencil lines; a pencil to draw on the wood; undo takes pencil off straight after; or a sheet scribed on at once |
| G0 | Stock in sizes: the rack holds kinds of stock (ash, oak and walnut boards; the mallet's blanks: ash handles, oak heads, oak strips), each a blank of its wood made to its size (`SdfBody.load_stock`). The vise's jaws close on thin pieces, and the view at the bench takes in a long one stood on end |
| D3 | Small pieces become debris: a part under 30 mm³ that came away (down to specks) is taken out of the work rather than split off. The look for islands refines, collects every crumb and cuts them out together; the work keeps the rest as an undo step of its own, and each crumb comes away as a chunk of debris (its convex hull, coloured by the wood, that hull its collider). A saw's sliver under 30 mm³ goes the same way; undo straight after puts it back. On the way, `cut_out` learned to prove air clear beyond the octree's root cube (a crumb at the work's edge) |
| T5-5 | Steering: pivoted mid-stroke, a chisel's bevel steers its depth (tipped past it, it dives; on it, level; under it, it lifts out); dragged round a curve, a chisel, gouge or plane follows the drag's trail, set afresh to the surface as it goes; the chain of segments swept as a few straight and quadratic sweeps |
| T5-3 | Planes: the spokeshave's slot holds a block plane (a long sole held flat, started at the work's end), the block plane with a chamfer fence (held across an arris at 45°, or on the plane between two gauge lines: an even chamfer to the lines) and a shoulder plane (its iron flush with its sides: into a rebate's inside corner) |
| T5-4 | The guiding hand: right-drag pivots the tool on its edge (up / down its angle to the work, left / right its skew or turn, the wheel its lean), hovering or while planned, the pointer held where it was. Space plans. An attitude gauge by the pointer shows the angle (with the bevel riding, biting or digging in), the skew and the lean |
| T5-2 | The lines stop the tools. Flat on a face: a gauge line on the face beside is a floor (a rebate's depth), one on the face a shoulder (the chisel set flush on it), a knife line across the way an end (stopped square). Across the corner: the plane through gauge lines on both faces is a chamfer; the tool is laid on it and each pass takes the corner down to it, then nothing. The saw snaps onto a knife line and stops at a depth line; rasps, scraper and block keep to the floor and the waste. Alt crosses them. In the core: `Limits` (`tools/layout.h`) and a stop *at the line* |
| T5-1 | Laying out: a marking gauge and a knife with a square (the Layout slot, 9) scribe real V lines 0.3 mm deep and record them as marks on the piece, undone with their cuts |
| W5 | A workshop to work in: a room built in code, a first-person player, tools on a hotbar, every piece of work a rigid body you pick up and carry (R turns it, the wheel reaches it), a vise that squares what is let go on it (flat, along the bench, on its top) and holds the piece the tools work on, a rack whose stacks give new boards. The old bench view is the work view you step into (E) and out of (Esc) |
| T4-7 | The sanding sponge's flow at a real rate by grit, pressure and wood (ten passes of 120 grit round an ash arris to a 0.5 mm radius), at its working speed |
| T4-6 | The spokeshave: held flat, its sole riding the highest across its blade; as deep as two hands push the chip's own section (deeper on a narrow edge), its mouth passes (0.8 mm) and its toe rides (a step blocks it) |
| T4-5 | The card scraper: a burr line pushed over the work, 0.01 mm from each point it passes, only where it went, on the push |
| T4-4 | The rasp: a rubbed face along its line, cutting on the push at a calibrated rate (a cabinet rasp 0.05 mm of oak a 150 mm push), resting on high spots, only where its face went |
| T4-3 | The saw: cuts on the push at a tenon saw's rate, by the chord under its teeth and the wood's hardness (through 25 mm of oak in about 70 strokes); its back stops it; the kerf runs where the teeth have been |
| T4-2 | The sanding block held to the real tools' rules (`tools/rubbing`): it rests on the high spots and takes them down first, only where it rubbed (patches lying along its way, adding up over the same ground), at a real rate (0.01 mm a metre at 120 grit in ash), faster where it bears on less. Grits 60–320, a small pad. Every direct tool follows the pointer at a working speed; every tool but the saw pushes loose pieces aside |
| T4-1 | Chisels and gouges held to the real tools' rules: nothing cut under the work (a step ahead blocks, a steep rise stalls, a gentle one is followed), the blade never through it, force from the chip's own section, splinters instead of square pits, shallow defaults, tap / firm / heavy blows. The workshop: a working speed the tool follows at, a pace, the blade pushing loose pieces aside, where and why a stroke stops, depths by hundredths, an edge lock (along any edge, flush, level with an earlier cut's floor; Alt: free). Shavings are the chip's own width and section |

Test counts today: 151 native tests (GCC and Clang, including golden images) and 22
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
  - crumbs (parts under 30 mm³) are looked for only round a cut: an undone one stays in
    the work until a cut near it; one whose region can't be proved (a web to the rest
    thinner than the samples) stays too.
- **Physics:**
  - shavings' and chips' colliders are boxes (a chunk's is its hull);
  - where the work's collider is a box, a chunk born in the hollow it left is set down on
    top of the box;
  - a piece's collider is a box while it fills its bounds, unless islands have hollowed
    it (then convex pieces). Proper meshes come with the Bake;
  - under Jolt a hull rests less well than a box (a sawn strip as a hull sinks 0.6 mm and
    tilts 2°), so irregular offcuts, which get hulls, are the ones to watch.
- **The workshop:** the tool in hand floats at a fixed offset from the eyes (no hands);
  tools can't be put down; the rack's stacks never run out; the vise holds a piece on the
  bench top only (not raised for sawing down or working an end).
- **Shadows:** under the Compatibility renderer, Live bodies receive no shadow-map
  shadows (coordinates are per vertex on the proxy box). Forward+ and Mobile look them
  up per fragment, which is still to be confirmed on hardware.
- **Steering:** a stroke whose merged sweeps outgrow the overlay (16 edits: a long,
  wandering curve) shows only its newest until it lands; its older sweeps could be applied
  as it goes.
- **Dust:** it doesn't ride on the pieces it lands on (it falls once a sawn piece has slid
  away), and tools pass over it without pushing it.
- **Other:** redo doesn't bring back offcuts or debris; there is no mesh export yet.

## 6. What comes next

In order, per docs/PLAN.md's roadmap. Each capability replaces several overlapping items
of the first plan.

### T5. Lines the tools obey, and a guiding hand (done)

From play: a chisel pass takes one strip, so corners and whole sections never come out
crisp, and the tools work on one axis at one angle. Real work gets crisp edges from
references, a line knifed or gauged that the tool registers to, and control from the
guiding hand on the blade. The user chose lines the tools obey (they stop the tool; Alt
crosses them), scribed and drawn, and two-handed control with the mouse and keyboard
(right-drag pivots the tool on its edge, Space plans). The plan (docs/PLAN.md, T5): T5-1
layout, T5-2 the lines stop the tools, then T5-4 the guiding hand, T5-3 planes (a block
plane with a chamfer fence, a shoulder plane), T5-5 steering mid-stroke and curved
strokes. All five are done. Next: try them together in play.

### W5. A workshop to work in (done)

Before trying the tools in play, the user wanted to find out how working with objects
feels: walk round a workshop, take tools from a hotbar, pick pieces up, carry them to the
bench and work on them there. Built in four steps (docs/PLAN.md, W5): the room and player
with the hotbar; pieces as rigid bodies you carry; the vise; the rack. From play, shavings
and sawn-off blocks got stuck in the bench and floor and shook there: W5-5 fixed it
(docs/PLAN.md, W5-5). Next: try the workshop and the tools together in play.

### T4. The rules of the real tools (done: T4-1 to T4-7; to be tried together)

From play, the tools went too fast and hit too hard. One tool at a time, each tried by the
user before the next, they are held to six rules (docs/PLAN.md, T4): access (only the edge
or face meets the wood), chips must escape, real rates times a workshop pace, a working
speed, loose pieces pushed not cut under, and a reason wherever a tool stops. T4-1 (chisels
and gouges), T4-2 (the sanding block), T4-3 (the saw), T4-4 (the rasp), T4-5 (the scraper),
T4-6 (the spokeshave) and T4-7 (the sponge) are done: the user asked for them in one go,
to try them together and then work on each. Next: what play turns up, tool by tool.

### 2. Debris (done: D1, D2 and D3)

Shavings and chips (D1), dust (D2), and small pieces (D3): parts under 30 mm³ that cuts
free are taken out of the work and come away as chunks of debris, not pieces
(docs/PLAN.md, D3).

### G. Making an object (under way: G0, G1a, G1b, G2, G3a and G3b done)

The engine cuts like the real tools, but a player can't make anything yet: no goal, no
idea what a piece is meant to become, no joining. The user chose (docs/PLAN.md, G):
- a first object, a **wooden mallet** (an oak head with a through mortise, an ash handle
  with a tenon, an oak wedge): the pickaxe's shape in wood, and once made, the mallet the
  chisels are struck with;
- **plans** as rectangular blanks with features on their faces, from a plan book of
  presets or drawn on paper; laid on the piece in the vise as pencil lines, which the knife
  and gauge snap onto; or a pencil on the wood;
- **checking** as a guide (proud and short spots shown), with no score: the real shapes
  decide whether joints go together;
- **physical assembly**: offer a part up, it slides until the shapes bind, drive it with
  the mallet, glue, wedge; one rigid body that can still be worked.

Steps: G0 stock in sizes (done), G1a plans and laying a part on the wood (done), G1b the
sheet editor (done), G2 checking (done), G3 assembly (G3a the fit and G3b offering up and
the assembly as one body done; G3c glue and the wedge next), G4 the mallet end to end. They go ahead of
Surface finish and Bake.

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
native/build/sdf_tests                                                   # 151 tests, golden images
GODOT=/path/to/godot tools/test_godot.sh                                 # headless tier (about a minute)
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
    shaving rocked from facet to facet and never settled; a box rests. The board's own hull
    (17 points, bunched at its eased corners) let a loose block jump and then sink 3 mm into
    it; a box of its bounds holds it.
  - Bodies lighter than a few grams are shaken about: debris weighs at least 5 g to the
    solver.
  - A kinematic wedge (a chisel's blade) slides under a light piece, or tips it and buries
    it in the board; a plate at the edge flings it off. A velocity kick as slow as a hand's
    tool (13 mm/s) is below what friction takes off in one physics step (μ·g·dt ≈ 80 mm/s),
    so it moves nothing. Pieces in a tool's way are moved with the edge (a shape query and a
    step each physics frame).
  - A test that locks a stroke by a ray should wait for the board to go idle: refining
    changes the cache the rays read, and the plane fitted across a channel's rim with it.
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
- **Chips are what stands over the edge.** Taken as a block from the floor up to the
  highest surface nearby, a chip in a gouge's channel counted the channel's sides, and the
  gouge could not deepen its own channel. Sum the section across the edge.
- **A cut's end is not a step.** It slopes back at the blade's angle, rising under a
  millimetre per millimetre, so a rise tested sample by sample never sees a wall there.
  Look over two millimetres.
- **Rubbing adds up; rectangles don't.** A sanding pass as one rectangle round the path
  sands a diagonal sweep's empty corners; independent patches over the same ground take
  only the deepest one's depth. Patches lie along the way the face moved, and each new one
  rests on the ground as the ones before it left it (a height map lowered as they close).
- **A flat face must be set flat.** A normal from the pointer can be a degree or two off
  (a plane through four hits beside an earlier cut). Over a 25 mm face that is a millimetre:
  the face bears on one edge and cuts a wedge. Settle the face on the plane most points under
  it lie on, to micrometres: a looser band lets a 200 mm face tilt a hair to take in two
  levels.
- **A smooth blend reaches below its floor.** A pass feathered by r lowers any surface
  within r under its box's floor: resting on a bump, a 2 mm feather sanded the flat round
  it. Keep the feather to half what the pass takes off most of its ground.
- **Name the physics engine, and check which one runs.** Left at DEFAULT, this project ran
  GodotPhysics3D, not Jolt: the Jolt settings in `project.godot` did nothing, and every
  physics figure measured "under Jolt" was GodotPhysics3D's. A chip spinning at 300 rad/s
  gave it away (Jolt caps a body at 47 rad/s). Identical numbers when a setting changes
  are the other tell.
- **A rule for loose pieces must say "loose".** Debris goes through a `RigidBody3D` it is
  born inside (a loose piece the blade pushes). Once the board in the vise became a frozen
  `RigidBody3D` too, every shaving went through the board, and the debris test's "rests
  above the bench" still passed. Check it rests where it should (on the board).
- **Fast and small at 60 Hz.** A piece let go at carry height moves 5 to 9 cm a physics
  step, more than a bench top is thick: give pieces and debris continuous collision
  detection, and the floor a plane. Teleporting a piece (a tool's push, the vise) must look
  where it lands.
- **A piece's collider follows its split at once.** The offcut is made where it was, inside
  the old piece's collider until the half-space lands; rebuilt only on the next `edited`,
  that box threw it 21 mm off the kerf (it should slide about 6). Rebuild it at the split.
- **The eyes see through what they carry.** A ray from the crosshair hits the held piece
  first unless it is excluded, and letting go over the vise would never find the vise.
- **Daylight through a window lights the walls from inside.** The sun still reaches every
  wall that faces it and blows them out; keep the walls on their own visual layer, culled
  from the sun, and let the lamps and ambient light them.
- **Register to what the lines say, not to what the lock found.** A chamfer was first held
  only on edge lock's outside-corner hold. The first pass works; the second starts on the
  chamfer begun, where edge lock finds the chamfer's own folds (running off at an angle
  where the pass ramped in) and snaps flush off one: no chamfer, and a path climbing out of
  it. Find the chamfer from the marks and where the stroke was pressed, and lay the tool
  along the lines' corner.
- **Two rows of points always make a plane.** `settle` takes the plane most of its sampled
  points lie on. A shoulder plane flush against a rebate's wall, most of its sole over
  the edge, had two rows on the wood, the outer one on the wall's foot: the plane through
  both rows (tipped 8°) outvoted the floor. Sample a little in from the edges, and look
  at which rows actually hit.
- **A long sole is held flat, and must go end to end.** Resting a plane's sole on the
  upper hull (as the spokeshave's, to follow curves) let it dip over a board's rounded
  end; resting it on the highest point under it is right, but then anything a pass left
  standing (half a millimetre at the far end) holds it off the wood for half a sole's
  length. Start planes at the near end and run them off the far one.
- **Follow the way the pointer went, not where it is.** An edge that lags a fast drag (it
  goes at a hand's speed) and heads for the pointer cuts the corner: 4.5 mm inside a
  40 mm curve. Following the drag's trail keeps it within 0.2 mm.
- **One bad edit refuses the whole batch.** A merged quadratic bending tighter than its
  section follows has a Lipschitz bound over 2; the session rejects the stroke's whole
  commit, and nothing is cut. Check each candidate's bound, not just its fit.
- **Step the tool finer than a frame.** Steering once a frame faceted curves when frames
  took half a second; the pushed tool now advances and steers every half millimetre.
- **Wait for the board to be idle between strokes in a test.** A ray right after a commit
  can read the body as it was: lock the next stroke only once `is_busy()` is false and no
  refine is pending.
- **Tests wait in game time, and for the tool.** Every tool now follows the pointer at a
  working speed: drag one leg at a time and wait for `lagging()` to clear (the harness's
  `catch_up`). Waits counted in frames break when headless frames take a millisecond.

## 9. Where to read more

- **The workshop and how each part works** (README.md):
  - "The workshop";
  - "Edits in milliseconds";
  - "Sanding as smoothing";
  - "Chisels and gouges cut as the wood lets them";
  - "Sanding block" and "Rates and working speeds";
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
  - `native/src/core/tools/rubbing.h`;
  - `native/src/core/tools/debris.h`;
  - `native/src/core/pieces/parts.h`;
  - `native/src/core/body/region.h`;
  - `native/src/godot/sdf_body.h`.
