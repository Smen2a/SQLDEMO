# Object Builder SDF Engine: plan (revision 2)

## Context
The first plan grew into a 1,700-line log: the design, then every milestone's design and
as-built notes. Several future steps overlap:
- W3a (a saw cut that goes through separates the board), E5 (a failing assembly breaks
  apart) and E6 (fracture) all turn one body into several.
- W3b (shavings and dust) and the chips of E6 and E8 are all removed material made visible.
- W2b and W3c are both a surface-finish layer.

This revision merges those into shared capabilities and keeps finished work to one table.
- The first plan's lasting design sections and its detailed E4 (bake) design are archived
  verbatim in [archive/plan-v1.md](archive/plan-v1.md).
- As-built details stay in the [README](../README.md) and the commit messages.
- **Tools** (below) is done: the tools behave like their real selves, and a stroke can be
  planned before it is made (T1 to T3).
- **Pieces** (1) is done: P1 (saw through) and P2 (islands left by any cut).
- **Debris** (2) is under way: D1 (shavings and chips) and D2 (dust) are done; D3 (small
  pieces) is next.

## Goals (unchanged)
- An object builder. Players shape parts with processes that fit the material, then join
  them into working objects. The acceptance scene is a pickaxe: a steel head, an ash handle
  and a wedge.
- Processes by material:
  - wood is cut with sharp tools;
  - metal is forged hot with blunt ones;
  - stone is shaped by percussion.
- Parts never blend. They join by interlock, compression or glue, and the joints'
  strength is computed from the SDFs.
- Unlimited undo, manifold export, desktop mouse and keyboard, Godot 4.7 with a C++
  GDExtension. The SDF core is engine-agnostic.

## Principles (how everything is built)
1. **The SDF is the truth; everything else is a cache.** That covers ADF bricks, bakes and
   physics shapes. Caches may lag; they are never the source.
2. **One source for op formulas.** Shared C/GLSL includes (`native/src/core/shared`) serve
   the CPU evaluator, the Live shader and the GPU brick sampler. Parity tests enforce it.
3. **Edits cost nothing while the tool moves.**
   - The shader draws the stroke (the overlay).
   - On release it is committed once, on a worker.
   - Creases land in coarse exact cells within milliseconds and are refined when idle.
   - Bricks are sampled on the GPU where there is one.
4. **Representation by process.**
   - Cuts are analytic edits in the edit list.
   - Smoothing is a sampled layer.
   - Deformation (forging) is a sampled base grid.
   - Separate parts are separate bodies.
5. **Every step ends green, committed and pushed:**
   - native tests (GCC and Clang);
   - `tools/test_godot.sh`: the headless tier (logic, plus a one-frame shader compile check);
   - `tools/test_godot.sh --gpu` where there is a GPU: renders, image checks, GPU/CPU parity,
     Forward+ on Vulkan;
   - benches (`sdf_bench`), with numbers in the README;
   - a zip at each milestone.

## Done
| Milestone | Delivered |
| --- | --- |
| E0–E2 | Primitives, swept tool profiles, seven blend modes with material weights, evaluator, octree with interval pruning and incremental updates, CPU reference renderer, golden images |
| E3, E3b–E3e | Godot Live raymarch; ADF display cache (8³ half-float bricks, exact crease cells with their own tapes, 64³ lookup grid, measured error bands); speckle fix; shadow casters; depth early-out |
| W1 | The workshop: a board, chisel, saw and sanding block as SDF bodies; raycast; `EditSession` with undo and redo; edits on a worker |
| W2a | Strokes drawn by the shader while the tool moves (no CPU), applied once on release, merged |
| W2c | Sanding sponge: a curvature-flow smoothing layer (`Op::Layer`) |
| W4 | Precision 10 µm; coarse crease cells refined when idle; cuts folded into old bricks; 512² upload layers; GPU brick sampler. Commits take 3–9 ms (were 57–95), a sponge update 23 ms |
| P1 | Sawn through, the board comes apart: `plane_clear` detects it (about 13 ms) and the worker measures both sides; `SdfBody.split` makes two editable bodies at once (about 3 ms of the frame) (the overlay draws each half-space until it lands); the offcut is a rigid body (a box or a cleaned convex hull) resting on the bench; undo rejoins. Physics tolerances set for millimetres; Jolt stays the default after a side-by-side with Box3D |
| T1 | Plan a stroke first: hold the right button to lock it in and see it hatched on the board, the wheel for its intensity; left-drag makes it along the plan. Or left-drag alone: the tool works the way the drag goes, cutting just as a planned stroke would, without the preview. The tool in hand stays out of sight until it acts, then fades in. Middle-drag orbits |
| T2 | Chisels and gouges cut as the wood lets them (`tools/cutting`): force by hardness and grain against a hand's 200 N, clearance past the bevel (mid-face they skate until tipped, then dive), free entry from an open face, tear-out uphill and breakout at an exit (seeded: the plan and the stroke agree), chopping with mallet blows that pop chips off near an open face. Variants: bench, paring, mortise and skew chisels; #3 and #7 gouges, a veiner, a V-tool. The line by the pointer gives depth, force, grain and warnings |
| P2 | Islands: cuts meeting free a piece no plane separates (a rebate, a corner, a chip). The worker looks round each cut once idle (`find_parts`: ADF samples joined within bricks and across leaf faces, exact tapes where bricks stray), cuts the island out with a region proved apart (`cut_out`: island, rest and free cubes, island and rest never touching across unclear air) and measures both sides; each side keeps its own with `Op::Keep`. The board's collider becomes convex pieces round the hollows; islands are set down on what is under them and let go asleep |
| D2 | Dust: the saw, rasps, scraper, sanding block and sponge report the wood they take (the sponge's own flow measures it); grains fly, land where their arc meets something and pile up, sawdust heaped beyond the kerf's ends, sanding dust on the face; undo takes them back. Every tool's dust within 1.5% of what the board lost |
| D1 | Shavings and chips: a stroke reports what it takes off (`tools/debris.h`), read from the body before it lands; a shaving curls off the edge as it goes, coloured by the wood, breaking by the grain, and comes away as a rigid body; tear-out and pop-offs throw chips; undo takes them back. A shaving is within 1% of the volume the board lost |
| T3 | Shaping and finishing (`tools/shaping`): rasps coarse to fine (never tearing, tilted to chamfer, the round face hollowing), a card scraper taking a whisper, a spokeshave whose 40 mm sole follows convex curves and bridges hollows while its blade takes an even shaving |

**Open measurements (on a real GPU; the reference machine is an RTX 3060 Ti):**
- the E3 gate bench;
- `tools/run.sh --vulkan res://tests/gpu_bricks.tscn` (GPU vs CPU brick times);
- per-pixel cost while crease cells are coarse.

These decide O3 (a 3D brick atlas), the default `update_exact_cell`, and whether the Live
path needs compute passes.

## Roadmap
| # | Capability | Replaces | Builds on |
| --- | --- | --- | --- |
| T | **Tools**: plan, lock, act; cutting by the wood | the fixed-depth tools of W1 | the overlay (W2a) |
| 1 | **Pieces**: bodies that come apart | W3a; E5's and E6's splitting | Live clip, EditSession |
| 2 | **Debris**: removed material made visible | W3b; E6/E8 chip particles | Pieces (small pieces become debris) |
| 3 | **Surface finish**: marks, scratches, sanded edges as shading first | W2b, W3c | the smoothing layer |
| 4 | **Bake**: meshes for distance display, physics and export | E4 | Pieces (shapes), finish (maps) |
| 5 | **Assemblies**: joints, fit and strength, one rigid body | E5 | Pieces, Bake |
| 6 | **Fracture**: breaking by material | E6 | Pieces, Debris |
| 7 | **Forging**: hot metal deformed by blows | E7 | sampled base grid |
| 8 | **Scale**: 2 m stone, 100k edits | E8, O3 | everything |

## T. Tools — plan, lock, act; cutting by the wood: done (T1 to T3)

**Why.** The tools cut like SDF cutters, not like hand tools: the chisel ramps in to
whatever depth is set, anywhere, whatever the wood. A stroke starts the moment the button
goes down, with nothing to see first, and the tool in hand hides what it is aimed at.
Decided with the user:
- Hold the right button to lock a stroke in and see it. Left-drag makes it along the
  locked path.
- Middle-drag orbits the camera.
- The tool is hidden while planning, and a cross-section inset shows the cut side on.
- Afterwards: a left-drag alone must still use any tool, just without the preview; and the
  inset goes (it did not help).
- First round: chisels and gouges, then rasps, a card scraper and a spokeshave. Saw
  variants and planes come later.

### T1 — plan, lock, act; the hidden tool: done
- `SdfBody.plan_stroke` runs the stroke along the path without making it. Its cut goes
  into the overlay flagged as planned, and the shader hatches it. The wheel sets
  intensity, Shift+wheel the chisel's angle.
- **Direct strokes.** A left-drag without a plan waits for 2 mm of drag to give its
  direction, then begins. `SdfBody.begin_stroke` plans a chisel's, gouge's or
  spokeshave's cut there and then (open-ended: 300 mm, as far as the drag goes; about
  3 ms) without drawing it, and keeps its report for the line by the pointer. The sanding
  tools and a chop start on the click.
- The tool in hand is hidden until it acts, then fades in (a dither, `sdf_opacity`).
- A section inset (an orthographic camera across the path, the board cut open in
  orthographic views) was tried and removed.
- `game/tests/tool_planning` checks each step, planned and direct.

### T2 — the cutting model, chisels and gouges: done
- **Wood properties** on `Material`: hardness (Janka: ash 1320 lbf, oak 1290, walnut 1010),
  how readily it splits, how readily it tears out, and grain runout.
- **Fibre direction** from the body's grain, tilted by a smooth runout tied to the ring
  field (the figure hints at which way is downhill).
- **Force per mm² of chip** grows with hardness and with the angle to the fibres: least
  along them, about 1.8× across them, about 4.5× into end grain. Add shear for each side
  of the chip still attached, and less when the edge is skewed. Pushed by hand (about
  200 N) or struck (mallet blows); where the force isn't there, the plan stalls or stays
  shallow, and says so.
- **Entry and clearance.** Bevel down, the edge bites only past bevel + 2°, then dives until
  levelled. From an edge or an existing cut the chip is free and the cut can start at
  full depth. In the middle of a face a flat chisel's sides are held: a shaving at most.
- **Tear-out** where the fibres run down into the wood ahead of the edge (seeded chips
  below the cut, the same in plan and result); breakout at an end-grain exit.
- **Chopping:** a mallet blow drives a slit (2–3 mm across the grain in oak for a 12 mm
  bench chisel). Near an open face on the bevel side, the chip pops off along the fibres.
- **The catalog:**
  - chisels: bench (6, 12, 25 mm), paring (25 mm, never struck), mortise (8 mm, heavy
    mallet), skew (18 mm);
  - gouges: #3 and #7 (12 mm), #11 veiner (3 mm), V-tool (60°, 6 mm). With the corners
    out of the wood the chip's sides are free; buried, they tear.
- The line by the pointer shows the force needed against the force available, the grain,
  and warnings: skates, stalls, tear-out, corners buried, breakout, needs a mallet.

### T3 — rasps, card scraper, spokeshave: done
- **Rasp:** removal set by coarseness, pressure and hardness; never tears; flat or
  half-round (which hollows); tilted to chamfer an arris.
- **Card scraper:** about 0.01 mm a pass, no tear-out; cleans up.
- **Spokeshave:** a short sole following the surface at a set depth. An even shaving on a
  flat face, it follows convex curves, and it tears out against the grain at half a
  chisel's rate.

## 1. Pieces — bodies that come apart (current: P1 and P2 done)

**Why one capability.** Sawing through, a failing joint and a fracture all end with one
body becoming several that move on their own. Build it once:
- detect that material separated;
- split without waiting for a rebuild;
- give each piece physics.

Fracture (6) and assembly failure (5) later only supply the cutter.

### P1 — planar separation (saw through): done
As built, it differs from the design below in these ways:
- **Clipping.** There is no separate `sdf_clip` uniform. Each piece's half-space is an
  `Op::Intersect` edit that the stroke overlay draws until it lands.
- **Colliders.** Pieces that fill at least 90% of their bounds get a box. Hulls are
  reduced to the outermost of points within 1 mm: tight clusters made sliver faces that
  rocked and sank.
- **Tolerances.** `project.godot` sets 0.2 mm tolerances.
- **Engines.** `tools/compare_physics.sh` compares Jolt with Box3D.
- **Measured on the worker.** The worker measures both sides (volumes, centres, hull
  points) right after the separation check, and turns the plane so that the smaller side
  is in front. `split()` takes those measurements and waits on nothing: about 3 ms of the
  frame, down from a half-second stall.
- **Half-spaces at the faces.** Each half-space sits 5 µm into the kerf from its piece's
  own face, not in the kerf's middle, so the ADF has no kink in the air to refine.
- **One upload.** Only the first piece to upload makes new textures; the other keeps the
  shared ones and updates them in place.

The details are in the README ("Pieces").

- **Core** (`native/src/core/pieces/pieces.{h,cpp}`, new):
  - `bool plane_clear(body, octree, Plane, Aabb)`: an adaptive quadtree over the plane
    within the body's bounds.
    - A square is clear when F at its centre exceeds L × its half-diagonal (Lipschitz).
    - Otherwise it splits, down to 0.05 mm.
    - Any F ≤ 0 means material still crosses the plane.
    - The pieces are apart when the whole plane is clear, since any path between the sides
      crosses it.
  - `Aabb clip_box(Aabb, Plane)`.
  - `volume(adf, Plane side)`: solid leaves plus the inside samples of brick leaves.
  - Reuse `Octree::sample`, the leaves' Lipschitz bounds (`Octree::Leaf::lipschitz`), and
    `Adf` leaves.
- **Trigger.**
  - A new `Stroke::separation_plane()` (optional): `SawStroke` gives its kerf's centre plane,
    normal = normalize(cross(along, normal)).
  - After the commit that carries it, the worker runs `plane_clear` and SdfBody emits
    `separated(point, normal)`.
- **Split without rebuilding: `SdfBody.split(point, normal) -> SdfBody`.**
  - This body keeps the −normal side; the new body shows the +normal side.
  - Both draw the same textures under a clip half-space uniform (`sdf_clip`):
    - the ray span is clamped to the half-space;
    - d = max(d, plane) in the march, settling, normals and AO.
    The plane lies in the kerf's air, so it adds no surface.
  - Each piece appends an `Op::Intersect` half-space edit as an undo step, then rebuilds
    its octree and ADF on its worker.
  - Textures become copy-on-write: a shared flag makes the next upload create new textures.
  - The new body gets a copy of the EditSession, and its proxy box is `clip_box` of the
    bounds.
  - Both pieces stay editable.
- **Physics.**
  - Each piece gets a `ConvexPolygonShape3D` from surface points on its side: voxel
    centres of brick leaves with |d| < voxel/2, projected onto the surface, decimated to
    256. Godot builds the hull.
  - Mass = volume × the wood's density (a new `Material::density`).
  - In the workshop, the bench top and floor become `StaticBody3D`.
  - The smaller piece goes under a `RigidBody3D`: the SdfBody is its child, carrying the
    0.001 scale. It gets a small push away from the plane.
  - The kept piece stays static.
- **Undo right after a split rejoins the pieces.**
  - The offcut node is freed.
  - The board drops its Intersect and its clip, through `EditSession::drop_last_step` (new:
    an undo without a redo entry).

### P2 — islands left by any cut: done
- The chisel through a thin bridge, the saw at an angle, many cuts meeting: a split no
  single plane describes.
- **Connectivity** (`pieces/parts`), as designed: union-find over ADF inside samples, solid
  leaves as single nodes. As built:
  - each brick's samples are joined on their own, in parallel, into groups; the groups and
    solid leaves are joined across the faces between leaves (the octree face procedure);
  - plain bricks are trusted as drawn: a gap they would draw has samples in it (their
    reconstruction matches the field near the surface, where a gap would show). In exact
    leaves the brick strays by up to its error bound, so there the exact tape along the
    segment decides (the middle, then the quarters): a 0.1 mm slot parts the board;
  - it runs after commits, once the body is idle (a CHECK job, before refining; strokes
    and plans read the body meanwhile, as it only reads). It looks round the cut first:
    one piece there means nothing came away; a part wholly inside is the island (a chip,
    about 10 ms). Otherwise once more with the region grown round the smaller parts, then
    the whole board: about 35 ms for its 7,300 bricks.
- **Cutting an island out** (`Region`, `Op::Keep`, `cut_out`): not a sampled mask field but
  a labelled octree: island cubes (its material and the air near it), rest cubes, free
  cubes (clear of every surface by 0.05 mm, from the exact field). Cubes split where both
  sides' samples share one, and where an island cube meets a rest cube across a face that
  is not clear, down to 0.05 mm. When none meet, the island is proved apart: a web too thin
  for the samples is refused. `Op::Keep` drops one side as `max(d, 0.1 - d)`; free cubes
  make the switch continuous; the octree takes keep-nothing cells for empty; tapes holding
  a region never make exact leaves (the GPU takes it for the identity), and the island's
  body is hidden until its region lands.
- **Measured** (the rebate: 1,800 mm² between the sides): about 600 ms on the worker,
  most of it the region's 160,000 cubes (they resolve the gap all along it); 3.5 ms of the
  frame to split. The island's own ADF costs 1.7 times the bricks its surface had in the
  board's. Planar saw cuts still take P1's plane, which costs far less.
- **Physics.** The hollowed board's collider is convex pieces: its bounds cut into boxes by
  the faces of each island's box (0.3 mm outside), a hull per box. At millimetre scale the
  engine needed sharp shapes (0.1 mm margins), and the island set down just into what is
  under it and let go asleep (a body's first step finds no contact; on a sampled surface
  it rocks). A surface mesh was tried and dropped: the engine discards 1–2 mm triangles as
  degenerate. Meshes come with the Bake (4).
- **Left for later:** cheaper regions for long interfaces (anisotropic cubes, or a plane
  where one fits the gap), several islands at once (one per check today), and debris for
  crumbs under 1 mm³ (they stay in the body).

### Verification
- **Native** (new `tests/test_pieces.cpp`):
  - `plane_clear` is false with 0.5 mm of wood left and true once through;
  - each piece's field (the body with its half-space) matches the body on its side;
  - the volume estimate is within 2% of a box's;
  - `drop_last_step` restores the edit list bit for bit.
- **Godot** (Compatibility, and Forward+ via `--vulkan`): the workshop drive saws through.
  - A `separated` signal arrives and two bodies exist.
  - The offcut moves at least 5 mm under physics within 1 s.
  - Undo rejoins them.
  - A clip-drawn piece matches its own rebuilt ADF (image diff at the parity thresholds).
- The existing suites stay green; `sdf_bench tools` reports the separation check's time.

## 2. Debris (current: D1 and D2 done)
Built in three steps:
- **D1:** shavings and chips.
- **D2:** dust.
- **D3:** small pieces.

Dust is grains of its own (a MultiMesh), not `CPUParticles3D`, which cannot collide: it
settles and piles rather than falling through the bench.

### D1 — shavings and chips: done
- **The report** (`tools/debris.h`). A stroke says what it took off since it last said
  (`Stroke::debris`), from the body as it was before the stroke. A previewed stroke
  leaves the body alone until it lands, so `SdfBody` gathers debris only for previewed
  strokes, and only while nothing else is applied to the body.
  - A planned stroke samples its shaving every 0.5 mm: from the floor up to the surface,
    as wide as the edge's chip.
  - Each chip is reported once, when the edge reaches it: the material its cut takes
    beyond the floor's, sampled over its box (`measure_chip`).
  - `take_debris()` hands it over in world space, coloured by `albedo_at` (the shared
    `MaterialTable::albedo` mix the reference renderer uses too).
- **Breaking.** Every (1.5 + 3 (1 − split)) / grain mm (`CutPlan::shaving_piece`): along
  the fibres a long ribbon, across them about 10 mm, into end grain 2.4 mm. Also wherever
  the edge passes over air.
- **The workshop** (`debris.gd`):
  - the live shaving curls off the edge (radius 1.5 mm + 12 × thickness, 0.7 as long and
    1/0.7 as thick as the cut) and comes away as a rigid body where it breaks and when the
    stroke ends;
  - chips are boxes thrown off the face;
  - each piece is tagged with the board's undo step, so undo takes it back;
  - the newest 24 move, older ones freeze, past 300 the oldest go;
  - *Sweep* clears the bench.
- **Physics as built.** Shavings rest on a box round the curl in its own frame: a hull of
  the curl rocked from facet to facet and never settled. Debris weighs at least 5 g to the
  solver (lighter, it is shaken about) and is damped. The board keeps a collider while
  debris lies about, with the islands' 0.1 mm margin (its single hull had Godot's 4 cm).
- **Measured.** A shaving is within 1% of the volume the board lost; with an uphill cut's
  tear-out chips, within 1%. Taking it in costs at most about 0.5 ms a frame.

### D2 — dust: done
- **What each tool reports** (its `Stroke::debris`, from the body before the stroke):
  - **saw:** each slice of kerf it goes down through is the kerf's width times the
    material along the blade's line at that depth (the chord: sampled every millimetre,
    kept per half millimetre of depth). It leaves at the chord's two ends, thrown outward;
  - **rasp, scraper:** each bit deeper is the pass's width at that depth (a round face's
    chord grows as it goes in) times its length times the fraction of it over material (a
    13 × 5 probe). It leaves where the tool is, the way it was going;
  - **sanding block:** the pass's depth over the rectangle it has covered, times the
    fraction over material (a 7 × 7 probe), beyond what was reported; from under the block,
    over its face;
  - **sponge:** its flow measures it: each sample's share of material (phi's zero set
    smeared over a sample) before and after each step. Its work runs on the worker, so it
    reports from its own numbers and reads nothing; the last of it lands after the stroke
    ends (`SdfBody` keeps the stroke until its work is done).
  - Less than 0.05 mm³ is held back until more comes. Grains are 0.2–0.9 mm by tool.
- **Colour.** `albedo_at` where the body stands; while it is being changed (the saw and
  sponge do not wait), the base material's, which edits never change.
- **The workshop** (`dust.gd`):
  - a puff throws up to 16 grains, bigger when it carries more wood (dust takes 2.5 times
    the room) up to 2.5 mm across (then the pile grows taller instead), 15% paler than the
    wood;
  - each grain flies the way it was thrown with a little scatter, under gravity. Where it
    lands is found once, when it is thrown: its arc cast in three pieces against the
    physics space. So the board keeps its collider from the first stroke on;
  - grains stack on a 1 mm grid. One landing on a pile rolls to the first neighbour lower
    by more than a cell's width (a 45° slope, or an edge);
  - one MultiMesh, 30,000 grains at most, the oldest going first; at most 20 thrown a frame
    (the rest wait);
  - undo takes a stroke's dust back; a sawn piece's dust falls once the piece has slid off.
- **Measured** (`native/tests/test_debris.cpp`, `game/tests/dust`):
  - against what the board lost: saw 0.1%, sanding block 1.4%, flat and half-round rasp
    0.0%, sponge 1.0%;
  - in the workshop, a kerf 5.7 mm deep across the board gives 455 mm³ of sawdust (the
    kerf's width × depth × the board's width: 456), 85% of it heaped on the bench beyond
    the kerf's ends (7 mm high); the block's dust lies on the face;
  - throwing costs about 0.6 ms a frame while dust comes (about 1 ms at most), the flights
    at most 0.4 ms (headless).
- **Left for later:** dust does not ride on pieces it lands on (it falls once a sawn piece
  has slid away); tools pass over dust without pushing it.

### D3 — small pieces (next)
- One check collects every part that came away. Parts under about 30 mm³ (crumbs under
  1 mm³ included) are cut out together: one region labels them all Island, and the board
  keeps the Rest with one `Op::Keep` step. Bigger islands still split into bodies.
- Each becomes a debris chunk: a hull mesh from a small quickhull
  (`native/src/core/pieces/hull.{h,cpp}`), coloured per vertex, with a convex collider,
  reported by a `crumbled` signal. P1 offcuts under the threshold go the same way.
- Undo straight after drops the Keep.
- Stone's percussion chips (8) will be debris too.

## 3. Surface finish
- **A coarse finish grid** (RGBA8, 1.5–2 mm voxels over the body) holds:
  - tool-mark amplitude;
  - scratch amplitude;
  - their direction;
  - the pending bevel radius.

  Tools splat it on the main thread, and it is uploaded whole.
- **The Live shader** turns it into normals and roughness (anisotropic scratches, fading
  marks), and a bevel look where a radius is pending.
- **The sponge** only splats the grid while moving (about 1 ms). On release, one worker
  job builds the smoothing layer from the recorded path. The pending bevel clears in the
  frame the geometry lands, the way the overlay hands over (W2a).
- Bakes (4) read the grid too.

## 4. Bake
- **Pipeline:**
  - dual contouring from the octree field, manifold by construction;
  - QEM decimation checked against the SDF;
  - xatlas UVs;
  - normal, albedo, AO and finish maps.
- **Display:** a Baked display with Live↔Baked switching by screen-space error.
- **Export:** OBJ and STL.
- **Physics shapes** move from hulls (1) to convex decomposition.
- **Shadows:** while a body is Live, its latest bake casts its shadows (a shadows-only
  child).
- Detailed design: archived plan v1, "E4 implementation plan".

## 5. Assemblies
- **Joints:** interlock, compression or wedge, and glue. Strength is sampled over the
  contact.
- **Also:**
  - a fit heatmap;
  - one `RigidBody3D` with compound shapes per assembly;
  - overload loosens a joint, and failure splits the assembly into pieces (1).
- Acceptance: the pickaxe.

## 6. Fracture
- Crack surfaces per material:
  - wood splits along the grain;
  - stone breaks in conchoidal chips;
  - metal fails ductile.
- The crack is the cutter for Pieces (P2's region, `Op::Keep`); fragments below the
  threshold are Debris.

## 7. Forging
- A sampled base grid under the edit list, a temperature field, and hammer blows as
  deformation: advection, redistancing and volume correction.
- Malleability comes from the material's forging window.

## 8. Scale
- A 2 m stone block with 100k percussive edits.
- Consolidation of long edit runs into sampled bases, and a second lookup-grid level.
- Dirty-chunk rebakes.
- O3 (a 3D brick atlas) if the GPU numbers ask for it.
