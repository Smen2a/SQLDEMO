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
- **Tools** (below): the tools behave like their real selves, and a stroke can be planned
  before it is made (T1 to T3). T4 held them to the rules of the real tools, and T5 gave
  them lines to obey, a guiding hand, planes and steering: all done, to be tried together
  in play.
- **Pieces** (1) is done: P1 (saw through) and P2 (islands left by any cut).
- **Debris** (2) is done: D1 (shavings and chips), D2 (dust) and D3 (small pieces).
- **Making an object** (G) comes next, ahead of Surface finish (3) and Bake (4): plans on
  paper, parts and assembly, built round a first object, a wooden mallet. G0 (stock in
  sizes), G1a (plans, the mallet's preset, laying a part on the wood, the pencil), G1b
  (drawing plans on paper), G2 (checking a part against its drawing) and G3 (a joint's
  fit; offering a part up and tapping it home, one body; glue, the wedge, sawn flush) are
  done, and G4 (the mallet end to end: chopping measured, the brace and bit, the whole
  build from the rack, your mallet): G is done. Surface finish (3) is next.

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
| P1 | Sawn through, the board comes apart: `plane_clear` detects it (about 13 ms) and the worker measures both sides; `SdfBody.split` makes two editable bodies at once (about 3 ms of the frame) (the overlay draws each half-space until it lands); the offcut is a rigid body (a box or a cleaned convex hull) resting on the bench; undo rejoins. Physics tolerances set for millimetres; Jolt stays the default after a side-by-side with Box3D (that side was GodotPhysics3D, found in W5-5; Jolt is now named) |
| T1 | Plan a stroke first: hold the right button to lock it in and see it hatched on the board, the wheel for its intensity; left-drag makes it along the plan. Or left-drag alone: the tool works the way the drag goes, cutting just as a planned stroke would, without the preview. The tool in hand stays out of sight until it acts, then fades in. Middle-drag orbits |
| T2 | Chisels and gouges cut as the wood lets them (`tools/cutting`): force by hardness and grain against a hand's 200 N, clearance past the bevel (mid-face they skate until tipped, then dive), free entry from an open face, tear-out uphill and breakout at an exit (seeded: the plan and the stroke agree), chopping with mallet blows that pop chips off near an open face. Variants: bench, paring, mortise and skew chisels; #3 and #7 gouges, a veiner, a V-tool. The line by the pointer gives depth, force, grain and warnings |
| P2 | Islands: cuts meeting free a piece no plane separates (a rebate, a corner, a chip). The worker looks round each cut once idle (`find_parts`: ADF samples joined within bricks and across leaf faces, exact tapes where bricks stray), cuts the island out with a region proved apart (`cut_out`: island, rest and free cubes, island and rest never touching across unclear air) and measures both sides; each side keeps its own with `Op::Keep`. The board's collider becomes convex pieces round the hollows; islands are set down on what is under them and let go asleep |
| D2 | Dust: the saw, rasps, scraper, sanding block and sponge report the wood they take (the sponge's own flow measures it); grains fly and land where their arc meets something; undo takes them back. Every tool's dust within 1.5% of what the board lost. After play: a puff whose grains go as they land, what reaches the ground heaped in one mound per spot (sawdust beyond the kerf's ends) |
| G3c | Glue (G while offering; sets after a minute: the joint for good), the wedge (a joint driven tighter, opening its kerf: each blow drives less; locks itself and the tenon), T tips a carried piece (the mallet stood handle-down in the vise, set by all its parts), the proud tenon and wedge sawn flush (a joined part splits). `wedge_glue` |
| G3b | Offering up (`assembling.gd`): a part carried to its mate in the vise lines up with the joint (a ghost), E puts it on the joint, the wheel pushes it in as far as a hand can, clicks tap a tight fit home with the mallet, E lets go: one rigid body (its SdfBody in the mate's, its part's metas with it), the tools on whichever part is under the pointer; undo straight after, or F and the wheel back, draws it out. `assembly` |
| G3a | A joint's fit (`plans/joint`: how two parts go together; `pieces/fit`: a part's surface pushed along the joint into its mate, where it binds, loose / snug / drives / won't go; `SdfBody.joint_pose`, `SdfBody.fit`). `test_fit`, `joint_fit` |
| G2 | Checking a part (`plans/check`, `SdfBody.check_part`, `checking.gd`; K): proud and short places against the part as drawn, joined into spots, named, dotted on the wood. `check_part` |
| G1b | Drawing a plan on the pad (`plan_editor.gd`, the sheet's drawing shared with the book in `sheet_view.gd`): parts, blanks, features dragged on the views, picked and sized exactly, joints; saved to `user://plans` and laid out as presets are. `plan_editor` |
| G1a | Plans: a part is a blank with features (`plans/part`: its lines on a piece of stock, its intended solid); the mallet's plan (`game/plans/mallet.json`); the plan book on the side table (P), each part's sheet drawn in three views; a sheet laid on the piece in the vise draws the part on in pencil and makes the piece that part; pencil only guides, the knife and gauge take it; a pencil for drawing on the wood; pencil undone straight after; or scribed on at once, one step. `plan_transfer` |
| G0 | Stock in sizes (`demo::stock`, `SdfBody.load_stock`): the rack's stacks are kinds of stock, boards and the mallet's blanks (ash handles, oak heads, oak strips); the vise's jaws close down to 2 mm; stepping up to the bench, the view takes in a long piece. `stock` |
| D3 | Small pieces: a part under 30 mm³ that came away (crumbs, down to specks) is taken out of the work, not split off. The look for islands refines first, collects every crumb it holds and cuts them out together (`cut_out` over a set of parts), and the work keeps the rest with one `Op::Keep` step; each crumb comes away as a chunk of debris, its convex hull (`pieces/hull.h`) coloured by the wood, with that hull for its collider (`crumbled`). A saw's sliver under 30 mm³ goes the same way. Undo straight after puts it back. On the way: `cut_out` proves air clear beyond the octree's root cube (a crumb at the work's edge). `crumbs` |
| D1 | Shavings and chips: a stroke reports what it takes off (`tools/debris.h`), read from the body before it lands; a shaving curls off the edge as it goes, coloured by the wood, breaking by the grain, and comes away as a rigid body; tear-out and pop-offs throw chips; undo takes them back. A shaving is within 1% of the volume the board lost. After play: shavings and chips fade away 2 s after they come to rest |
| T3 | Shaping and finishing (`tools/shaping`): rasps coarse to fine (never tearing, tilted to chamfer, the round face hollowing), a card scraper taking a whisper, a spokeshave whose 40 mm sole follows convex curves and bridges hollows while its blade takes an even shaving |
| T4-1 | Chisels and gouges held to the real tools' rules: no cut under the work (a step ahead blocks, a steep rise stalls, a gentle one is followed), the blade's body never through it (overhangs, gaps too narrow), force from the chip's own section (a gouge deepens its channel), splinters instead of square pits, 0.2 / 0.3 mm to begin with, tap / firm / heavy blows. In the workshop: a working speed the tool follows at, a pace (¼× to 8×), the blade pushing loose pieces, the reason and place a stroke stops, depths by hundredths. After play: an edge lock (along any edge, flush, level with an earlier cut's floor) and shavings the size of the chip |
| W5 | A workshop to work in: a room built in code (bench and vise, side table, lumber rack), a first-person player, tools on a hotbar (none on the table), every piece of work a rigid body you pick up and carry, the vise squaring what is let go on it, the rack's stacks giving new boards; the old bench view is the work view you step into at the vise |
| W5-5 | Pieces and debris land and lie still: Jolt named (DEFAULT had been GodotPhysics3D), debris resting on the board in the vise, CCD and a plane floor, guarded pushes and vise, old debris fading. `physics_calm` |
| T5-1 | Laying out (the Layout slot, 9): a marking gauge (its fence riding the nearest edge, the wheel for its distance) and a knife with a square, each scribing a real V line 0.3 mm deep and recording a mark on the piece (body space, undone with its cut), drawn while a tool is in hand |
| T5-5 | Steering: a chisel's, gouge's or plane's stroke is a chain of planned segments (`SteeredStroke`), each begun at the depth the last reached. Pivoted mid-stroke, the bevel steers it (dives, runs level, lifts out); dragged round a curve, the edge follows the drag's trail (4 mm lazy, 3° steps, re-fitted to the surface); the segments merged into straight and quadratic sweeps (a quarter circle: 6 edits). `steering` |
| T5-3 | Planes (the Planes slot, the spokeshave's): a block plane (150 mm sole, 35 mm iron) held flat, started at the work's end; with a chamfer fence, held across an arris by edge lock at 45° to the piece's faces or on the plane between two gauge lines, each pass widening the chamfer evenly to the lines; a shoulder plane, its iron flush with its sides, into a rebate's inside corner. `planes` |
| T5-4 | The guiding hand: right-drag pivots the tool on its edge (up / down its angle to the work, left / right its skew or turn, the wheel its lean: rolled about its way), hovering or planned, the pointer held where it was; Space plans (Shift+wheel's angle went); an attitude gauge by the pointer (side on with the bevel riding, biting or digging in, from above, end on, in words). `guiding_hand` |
| T5-2 | The lines stop the tools (`tools/layout.h`: `Stop`, `Limits`, `limit_plan`): flat on a face, a depth line beside is a floor, a line on the face a shoulder (the chisel set flush on it), a knife line across the way an end (stopped square, no lift-out); across the corner, the plane through lines on both faces is a chamfer the tool is laid on and cuts down to, and no further; the saw snaps onto a knife line and stops at a depth line. Rasps, scraper and block keep to the floor and the waste strip. Alt crosses them. `layout_lines` |

**Open measurements (on a real GPU; the reference machine is an RTX 3060 Ti):**
- the E3 gate bench;
- `tools/run.sh --vulkan res://tests/gpu_bricks.tscn` (GPU vs CPU brick times);
- per-pixel cost while crease cells are coarse.

These decide O3 (a 3D brick atlas), the default `update_exact_cell`, and whether the Live
path needs compute passes.

## Roadmap
| # | Capability | Replaces | Builds on |
| --- | --- | --- | --- |
| T | **Tools**: plan, lock, act; cutting by the wood; the real tools' rules (T4, under way) | the fixed-depth tools of W1 | the overlay (W2a) |
| W5 | **A workshop to work in**: walk, carry, clamp, take stock; pieces that lie still (done) | the fixed bench view, tools on the table | Pieces (rigid bodies) |
| 1 | **Pieces**: bodies that come apart | W3a; E5's and E6's splitting | Live clip, EditSession |
| 2 | **Debris**: removed material made visible (done) | W3b; E6/E8 chip particles | Pieces (small pieces become debris) |
| G | **Making an object**: plans on paper (presets or drawn), parts, assembly; first object a mallet (under way) | — | Tools, layout (T5), Pieces |
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

### T4 — the rules of the real tools, one tool at a time (done: to be tried together)
**Why.** From play: the tools went too fast and hit too hard; chisels cut too deep, left
square pits and cut under raised parts of the board and under loose pieces; sanding was
far too fast (about 1,000 times a real rate) and took down the whole rectangle it had
covered. Decided with the user: real rates with a workshop pace (1× by default); every tool
capped at its working speed; fine control, a reason wherever a tool stops, and better aim;
one tool at a time, each tried before the next.

**The rules every tool will obey:**
1. **Access.** Only the working edge or face meets the wood; the tool's body never passes
   through the work.
2. **Chips must escape.** Material comes off only where it can leave: no tunnels, no
   undercuts.
3. **Real rates**, per tool, wood, grit or coarseness and pressure, times the pace. The
   numbers live in one table (README).
4. **Working speed.** The tool follows the pointer at most at its working speed, slower
   as the wood resists, times the pace.
5. **Other bodies.** Loose pieces in the tool's way are pushed by it, never cut under.
6. **Say why.** Every limit carries a reason and a place.

**T4-1, chisels and gouges: done.**
- The plan (`plan_cut`) looks at the chip every millimetre across the edge (nine columns).
  A rise of over a millimetre within two stops the edge at its face (*blocked*, with the
  step's height); a gentler rise into a chip too thick to push is followed at up to 8°,
  steeper it *stalls*. The section reaches through the thickest chip, so no edit leaves a
  ledge or a slot. Chopping still goes into walls.
- The force is the chip's own section over the edge, summed across the columns: in a
  gouge's channel the crescent the next pass takes, so a gouge deepens its own channel.
- Blade access: points on its back and sides 10, 17 and 25 mm behind the edge, at its
  angle (skipping those still down in the stroke's own channel): wood there stops it
  (*the blade meets the work*; walls only at its sides: *too wide for the gap*). Checked at
  the start too, which refuses a stroke the blade cannot reach.
- Tear-out, breakout, buried corners and V-tool wings are splinters: capsule sweeps dipping
  from their root along the grain, at most 1.5 times the cut and 1 mm (breakout 2 mm).
- Defaults 0.2 mm (chisel), 0.3 mm (gouge); limits 1.5 and 2.5 mm; a steep dive levels at
  the asked depth and only warns. Mallet blows 0.3, 1 and 1.6 times a firm one.
- `CutPlan::stop_at`, `stop` and `wall`, reported by `SdfBody.plan_stroke`: the hatch ends
  there, the outline draws a red cross, the line says *stops at N mm: why*.
- Workshop: `drag_screen` sets a target and `_process` moves the stroke towards it at the
  working speed (chisel 40 mm/s, gouge 30, spokeshave 40, down to a fifth at the hand's
  limit, times the pace); a blow every 0.35 s at most; a pace setting; loose pieces the
  blade's box touches (a shape query each physics step) moved on with the edge's advance
  (debris on its own layer, untouched); Ctrl+wheel and the sliders in a fifth of a wheel
  step; the blow chosen on the panel or, chopping, by the wheel.
- The blade as a kinematic solid did not work at millimetre scale: the wedge slid under a
  5 g piece, or tipped it and buried it in the board; a plate at the edge flung pieces off.
  And a velocity as slow as the edge is lost to friction within a physics step.
- Found on the way: the board's convex hull (17 points bunched at its eased corners) let
  loose pieces sink 3 mm into it (under GodotPhysics3D, as it turned out); the board's collider is now a box while it fills
  its bounds, as an offcut's is.
- **After play:**
  - **Edge lock** (`tools/edges`, `SdfBody.find_edge`). A chisel's or gouge's stroke locked
    near an edge and aimed within a set angle of it runs along it, flush. Any edge counts:
    a step of 0.1 mm or more, or a fold sharper than a set angle. It is found on a height
    map of rays and bisected to the edge line exactly. Beside an earlier cut, the depth is
    set level with its floor. At an outside corner (the far side falls away, no floor
    beyond; the face beyond found by a ray back towards the work), the chisel sits across
    the corner on the bisector of its faces and pares a chamfer. Alt places a stroke
    freely.
  - **The shaving is the chip:** nine columns across the edge give its width, thickness
    and place across the edge. Half off the board it is half as wide; it matches the
    board's loss to 0.1–2.4%.
- Tests: `test_cutting` (step, groove, overhang, rising surfaces, a gouge's second pass and
  the chisel blocked at its channel's end, splinters, blows); `tool_planning` (a flick
  follows at the working speed and pushes a loose block aside; the channel's stop and its
  line; Ctrl+wheel).

**T4-2, the sanding block: done** (asked to go on through every tool, then try them together).
- A rubbed face (`tools/rubbing`, shared with the rasp and scraper): a height map of the
  work read at the stroke's start (rays every 2.5 mm, under a millisecond); *patches* of
  the ground the face covered, each lying along the way it moved, cut below the highest
  point under it. A patch grows while the face works along one line (drifting up to half
  its width); turning, or reaching higher ground once it has cut, starts another, resting
  on the ground as the earlier patches left it. At most 12 a stroke (the two most compact
  merge); the preview shows the newest.
- Depth = rate × pace × travel × min(1, face length along the way / range) / contact; the
  contact is the share of the face bearing on the work (at least 0.1): a bump goes about
  ten times as fast, a 6 mm edge five times.
- Rate 1.2e-3 × pressure / grit × 5740 / Janka: 1e-5 mm per mm at 120 grit in ash (0.01 mm
  a metre, 0.2 mm a minute); 80 grit 1.5 times, 240 half. Grits 60–320, a small pad
  (35 × 20) besides the block (70 × 40).
- The cut's feather is at most half what it takes off most of its ground, so ground below
  the floor (round a bump) is left alone.
- The workshop: every direct tool now follows the pointer at a working speed (saw 300,
  rasp 250, scraper 200, block and sponge 300, spokeshave 150 mm/s, times the pace); every
  tool but the saw pushes loose pieces aside; the line by the pointer says what the block
  has taken and on how much of its face.
- Tests: `test_rubbing` (the rate, only where it rubbed, a diagonal band, a bump first, a
  narrow edge faster, dust); `tool_planning` (the block rubs, its line).

**T4-3, the saw: done.**
- Cuts on the push only (towards its toe; its handle is towards the way the drag first
  goes): 0.0038 × pressure × 25 / max(chord, 10) × pitch / 3.2 × 5740 / Janka mm per mm.
  The chord is the wood along its teeth at their depth within the blade, from columns of
  the work read when it is set (a top and a bottom every millimetre). Through 25 mm of oak
  50 mm wide in 68 strokes of 200 mm.
- Its back stops it 59.5 mm below the highest wood under the blade (*its back meets the
  work*). The kerf spans where the teeth have been, not a fixed blade length; once they go
  further along, it is cut again as one edit.
- The last slice down to *through* is always cut (the follower's small steps stopped 0.03 mm
  short). A set feed stays for tests. The panel's feed became pressure.
- Tests: `test_sawing` (push only, the rate, a long chord and walnut, through in 50–80
  strokes, the back, the kerf's span, small steps through); `tool_planning` (pull, then
  push, in the workshop).

**T4-4, the rasp: done.**
- A rubbed face along its line (`tools/rubbing`): 200 × 25 mm, cutting on the push only,
  resting on high spots, only where its face went (the board's whole length, for a face as
  long as the board), faster where it bears on less (over an end, on an arris, a strip).
- 6.7e-4 × coarseness × pressure × 5740 / Janka mm per mm pushed: a cabinet rasp takes
  0.05 mm of oak in a 150 mm push, bearing fully.
- Dust: probed every 5 mm or so once for each cut (was 7 by 7 over the whole pass); the
  block's from its map (the union of its patches' floors). Within 1% of what the board lost.
- Found in the workshop: the pointer's normal (a plane through four hits 4 mm apart) was 2°
  off beside an earlier cut, and a 25 mm face set by it bore on one edge and cut a wedge.
  Rubbed faces now settle: the plane most of 45 points under the face lie on, to 5 µm (a
  0.03 mm band let a long face tilt a hair to take in two levels, and rest on its far end).
- Tests: `test_shaping` (push only, the rate, a raised strip first, an arris chamfered).

**T4-5, the card scraper: done.**
- A rubbed face 1 mm along (its burr) and 60 across, pushed: each point it passes over
  loses 0.01 mm of oak (× pressure, less in harder wood), however long the stroke. Nothing
  on the pull; only where it went; feathered 4 mm at its sides (flexed).
- A patch no longer ends when the face "jumps" more than two face lengths in one move: a
  burr 1 mm long started a new patch every frame. Strokes are continuous.
- Tests: `test_shaping` (a push, ten, the pull, half the way); `tool_planning` (four pushes
  in the workshop).

**T4-6, the spokeshave: done.**
- Held flat on the work (the rubbed faces' settle, which now turns a face only where 40% of
  the points under it lie on one plane: a crown is left as set). Its sole rests on the
  highest across its blade as well as along (three rays a millimetre).
- Chips escape: its mouth passes 0.8 mm at most (*mouth*). Access: its toe, half a sole
  ahead, stops at a rise of over a millimetre within two (*blocked*, with the step's height).
- Force from the chip's own section (the chisel's columns, `chip_columns`, now in
  `cutting.h`): at the start it limits the depth (0.16 mm on oak's face, 0.51 on a 20 mm
  edge); every 2 mm along, a chip over 5% beyond the hands stalls it (*stalls*).
- Working speed 150 mm/s (was 40), slower at the hands' limit; the depth slider to 1 mm.
- Tests: `test_shaping` (a step ahead, a narrow edge, the mouth, set askew).

**T4-7, the sanding sponge: done.**
- Its flow: 0.05 / grit × pressure × 5740 / Janka mm² per mm, from the wood under it (the
  sponge now reads the body at its start, so it waits for an edit landing). Ten passes of
  120 grit round an ash arris to about 0.5 mm radius (0.21 mm along the bisector); 60 grit
  about 1.4 times as far. It follows the pointer at its working speed (T4-2).
- The demo and the flow's own tests rub at 16 times the pace with the reference hardness:
  exactly the old rate, so their images and numbers are unchanged.
- Tests: `test_smoothing` (ten passes at the real rate, 60 against 120 grit).

**Next (after trying them together):** each tool as the user finds it in play.

## W5. A workshop to work in: done

**Why.** The workshop was one view of a board fixed on a bench, the tools lying round it,
and every script assumed a single board. Before trying the tools in play, the user wanted
a place to find out how working with objects feels: a character walking round a real
workshop, tools from a hotbar, objects picked up, carried to the bench and worked on there.
Decided with the user: first person; a bench view you step into to work (today's controls),
Esc to step back; a vise on the bench holding the work; a lumber rack of ash, oak and
walnut.

**Built.**
- **W5-1, the room, the player, the hotbar.** `game/workshop/room.gd` builds the room in
  code: floor, walls (a door gap), ceiling, a window's light and two lamps (the walls are
  out of the daylight, on visual layer 2, which the sun culls), the bench with its vise
  (two jaws, meshes only, and a screw), a side table and the rack. The bench stays at the
  origin, so every earlier test keeps its world coordinates and only steps into work mode
  first (`enter_work(false)`). `game/workshop/player.gd` is a `CharacterBody3D` (a 1.75 m
  capsule, eyes at 1.6 m, 1.5 m/s, 3 m/s hurrying), on physics layer 2 meeting only the
  room's statics (layer 8): pieces never block or get shoved by it. The tools are hidden
  unless in hand: while walking, the one in hand is placed each frame at a hand offset from
  the eyes. Keys 1 to 8 and 0 in both modes, the wheel along the hotbar while walking.
  Modes: WALK (the player's eyes, the mouse captured) and WORK (the orbit view over the
  vise, a 0.3 s glide down, the mouse free).
- **W5-2, pieces.** Every piece of work is a `RigidBody3D` (meta `workpiece`) holding its
  `SdfBody`: the board in the vise, offcuts and islands alike (`_as_piece`). Each keeps its
  own collider, rebuilt on its edits and at a split; the vanishing board collider went.
  `board` is the clamped piece's body, or null (no work mode then). Carrying sets the
  held piece's velocities towards a point in front of the eyes (and its turn, relative to
  the player's facing) every physics step, gravity off, so it meets what it hits. R turns
  it a quarter, the wheel reaches it 0.35 to 1.2 m; let go, its speed is capped at 1 m/s.
  Debris and dust are keyed piece × 100000 + step, so undoing one piece takes back only
  its own.
- **W5-3, the vise.** Let go with the eyes on the vise (the bench between its jaws, or the
  piece in it) and an empty vise takes the piece: the body axis nearest vertical turned
  exactly up, the longer of the other two along the bench (to the nearer end), set on the
  bench top at the vise's middle by its hull, frozen; the jaws close to its width. With
  the vise taken it just drops. F takes the clamped piece out.
- **W5-4, the rack.** A stack of each wood on the middle shelf (plain boxes in the wood's
  colour, labelled on the shelf). E on one makes a new board of that wood
  (`SdfBody.load_demo`) off the top of the stack into the hands, brought round flat and
  across the view. The panel's wood choice and New board went; `set_wood` stays for tests.
- **Tests.** `walk_and_carry` (headless): the hotbar, walking into the bench, stepping up
  and back, F, carrying, turning, letting go on the floor, the vise squaring a board held
  askew, a board from the rack, the taken vise, the side table, a chisel stroke at the
  bench, and the board carried off and dropped with its cut and undo.

- **W5-5, pieces and debris that land and lie still.** From play: shavings (chisel,
  spokeshave) and sawn-off blocks stuck in the bench and floor and shook there, right after
  cutting, when carried or dropped, and when a tool pushed them. `physics_calm` measured
  each case first (how deep a body goes into anything, from the physics space, and when it
  lies still):
  - the chisel's shaving lay 15 mm into the board;
  - the spokeshave's sat 7 mm into the bench top and never came to rest;
  - a chip off the bench went 43 mm into the floor;
  - a block pushed by the sanding block went 62 mm into the bench;
  - a strip let go over the bench went 25 mm into it.

  The causes and fixes:
  - **The engine was not Jolt.** `3d/physics_engine` was left at DEFAULT, which Godot 4.7
    ran as GodotPhysics3D: the Jolt tolerances did nothing, and P1's "Jolt" figures were
    GodotPhysics3D's. On it, small light bodies rock and gain energy (a 4 mm chip spun at
    300 rad/s; Jolt caps at 47). `project.godot` now names Jolt. Every headless test
    passes unchanged; the offcut slides 4.3 mm (was 5.7) and sinks 0.00 mm.
  - **Debris went through the board.** `debris.gd` let a new piece pass through any
    `RigidBody3D` it was born in (meant for a loose piece the blade pushes), checked before
    lifting it clear. Since W5-2 the board in the vise is a frozen `RigidBody3D`. Now: only
    loose (unfrozen) pieces, checked where it is born, and a piece still inside something is
    set down on it from above.
  - **Tunnelling.** Pieces and debris have continuous collision detection, and the floor's
    collider is a plane again (it was before the room).
  - **Teleports.** A tool's push moves a piece only as far as its shapes, swept from just
    above what it rests on, are free to go. The vise won't take a piece where a loose one
    lies, and fades the debris there.
  - **Also:** debris past the newest 24 fades instead of freezing solid (it hung in the
    air once its support was carried off), and a piece's collider is left alone when an
    edit doesn't change it.

  After: every case goes at most 0.2 mm into what it rests on (2.6 mm at worst, landing)
  and is still within 0.7 s. The carry drive was measured too, held out past the bench top
  the eyes are on; it rests on the bench, and was left as it is.

- **T5, lines the tools obey and a guiding hand.** From play: a chisel pass takes one strip,
  so a corner or a whole section never comes out crisp; the tools work on one axis at one
  angle. Real work gets crisp edges from references: a line knifed or gauged, and the tool
  registered to it. The user chose lines the tools obey, and two-handed control with the
  mouse and keyboard. In order: T5-1 layout, T5-2 the lines stop the tools, T5-4 the
  guiding hand (right-drag pivots the tool on its edge, Space plans), T5-3 planes (a block
  plane with a chamfer fence, a shoulder plane), T5-5 steering mid-stroke and curved
  strokes.
  - **T5-1, layout** (done). The Layout slot (9): the marking gauge and the knife and
    square (Tab). A line is scribed with a narrow V edge (`scribe_edge()`, 40°) drawn
    along it, 0.3 mm deep, and recorded as a mark on the piece: kind, face, origin,
    direction, length, the side towards the edge it was gauged from, the distance and the
    step that made it. Undo drops the mark with its cut, redo brings it back.
  - **T5-2, the lines stop the tools** (done). When a stroke is locked or pressed,
    `layout.hold()` reads the marks by how the tool is held and passes `Limits` to the
    core in the stroke's settings (planes in body space, their normals towards the waste):
    - floors: `limit_plan` clamps a chisel's, gouge's or spokeshave's floor under its
      edge's middle and corners; a rubbed face (rasp, scraper, block) holds each pass's
      floor; the saw's depth stops short of one;
    - sides: rubbed patches are clipped to the waste strip (the rasp's pass narrowed to
      it); the chisel, gouge and spokeshave are set flush on one in the workshop;
    - ends: the plan ends at the first one its edge meets, square (`square_end`: no
      lift-out beyond);
    - a new stop, *at the line*.

    A chamfer is found from the marks, not the edge lock: once a pass has begun it, the
    edges the lock finds are the chamfer's own folds, so a tool laid on a slope between
    the two faces, within the corner the lines cut off, is held to it too, set on the
    plane at the corner abreast of where it was pressed, along the corner. Measured
    (`layout_lines`): a rebate to an 8 mm width line and a 1 mm depth line pared to 1.000
    mm, the shoulder untouched; a 3 mm chamfer pared from behind in 5 passes to 4.50 mm²
    (3 × 3 / 2) of section, 1.50 mm down halfway, the last pass taking nothing; a knife line
    stopping a pass square (0.300 mm deep 1.5 mm before it, nothing 1.5 mm past); Alt
    crossing it; the saw stopping at a 5 mm depth line (5.00 mm).

  - **T5-4, the guiding hand** (done). Right press takes the tool (`begin_pivot`: the
    mouse held, the orbit's wheel zoom off); the motion pivots it (`pivot(relative,
    fine)`, 0.25° a pixel, Ctrl a fifth), the wheel leans it (`lean`, 2.5° a notch), and
    right release puts the pointer back (`end_pivot`). Per tool:
    - chisel, gouge: up / down the angle (paring kept below 60°, a chop at or above:
      C switches), left / right the skew (drawn on the edge's outline, with a skew
      chisel's own: the catalog now gives `skew` and `flat`), the wheel the lean;
    - saw, spokeshave: left / right the turn, the wheel the lean (the saw's kerf
      bevelled);
    - rasp: left / right the turn, the wheel its tilt;
    - scraper, sanding block, sponge: left / right the turn (the core scraper has no
      tilt to give).

    The lean is the workshop's: the normal the stroke and its plan are given is rolled
    about the way the tool goes (`_leaned`), so every rule of the core holds for the
    leaned tool (a floor held to a line, too). A chisel's model is held at its angle when
    the hand lets go (or the stroke begins), not at every motion. Planning moved to Space
    (held; `lock()` and `unlock()` are unchanged, so tests drive them as before), and
    Shift+wheel's angle went; `adjust(steps, true)` still sets it. The attitude gauge
    (`workshop_ui.gd`) shows while the hand has the tool and 1.5 s after. Mid-stroke the
    hand does nothing yet: T5-5. `guiding_hand` drives it all through the input handler:
    10° for 40 pixels up, 5° of skew for 20 right, 5° of lean for two notches, a fifth with
    Ctrl, paring kept paring and a chop a chop, a plan pivoted under its bevel's clearance
    skating (and the gauge saying it rides), Space's plan dropped when it comes up, a 5°
    lean cutting 0.65 mm deep 4 mm to one side of a 0.3 mm pass and nothing 4 mm to the
    other, the rasp tilted, the block turned.

  - **T5-3, planes** (done). The spokeshave's slot became Planes, its id kept
    (`spokeshave`, so the tests and settings stand) and a `variant` added. The core's
    `Spokeshave` describes all of them: a kind (for the model), the sole's length and
    width, the iron's width, bed, force, mouth, and a fence (`catalog.h` `plane_catalog`):
    - **Block plane**, block plane with **chamfer fence**, **shoulder plane**, each with
      its own model (cast body, bedded iron and lever cap; the fence's brass plates at
      45° under the sole; a narrow steel body with a walnut wedge).
    - `plan_spokeshave`: a fence skips `settle` (it holds the plane as set); the sole's
      heights are read across the sole's whole width (a block plane rides a wall's top
      beside a rebate); a plane's long sole rests on the highest point under it (held
      flat) rather than following curves as the spokeshave's does; a plane set on within
      20 mm of the near end starts at the end (`kPlaneStart`).
    - `settle` reads its heights a millimetre in from the face's edges: a sole set flush
      against a wall had its outer row on the wall's foot, and with only two rows on the
      wood the plane through both (tipped 8°) outvoted the floor.
    - In the workshop: the fenced plane uses edge lock (corners only, the bisector of the
      piece's own two axes nearest the faces found: `_fence_normal`), looking a little
      ahead along the drag when pressed by the end of the arris; no lean with a fence;
      `_flush` sets a plane by its sole's width; a stroke held to a depth line is squared
      to its face (within 15°: `_square`).
    - Measured (`planes`): the fenced block plane between 3 mm lines, 5 passes of 0.5 mm
      (0.63, 1.17, 1.66, 0.84, then 0.00 mm²), 4.50 mm² of section at x = −50, 0 and
      50 mm, 1.50 mm down halfway, nothing past the lines; the shoulder plane down an
      8 × 1.5 mm rebate in 4 passes, 1.500 mm deep end to end and 0.3 mm from the shoulder,
      nothing past the width line. Natively: the block plane's full-width shaving from the
      end, the fenced chamfer, the shoulder plane into the corner and the block plane's
      3.5 mm strip.

  - **T5-5, steering** (done).
    - **Core** (`tools/cutting.{h,cpp}`):
      - `plan_cut(..., entry)` goes on from a cut `entry` deep. Tilted past the bevel's
        2° clearance it dives at tan(difference) to `max(depth, entry)`; within the
        clearance it runs level; under it, it rises and ends where the floor reaches the
        surface (`kLifts`).
      - `SteeredStroke` chains `PlannedStroke` segments; `going_on()` is the surface point
        over the edge and the edge's depth under it; `steer()` sets the next segment's
        `lead_in` to cover the outer corner of the joint.
      - `edits()` merges the floor points: straight where in line, quadratic where they fit
        within `kSteeredFit` (0.03 mm). Tangents are central differences (neighbour to
        neighbour); a quadratic is taken only if its Lipschitz bound is ≤ `kSteeredBend`
        (1.2).
      - `plan_spokeshave(..., continuing)`: a plane going on is not started again at the
        work's end.
    - **SdfBody:** every chisel, gouge or plane stroke but a chop is a `SteeredStroke`.
      `steer_stroke(normal, along, settings)` plans the next segment (the entry measured in
      the body as it was before the stroke, or from the plan when strokes are applied as
      they go) and reports it through `get_plan()`.
    - **Workshop:**
      - The pushed tool advances half a millimetre at a time, steering after each (`_steer`).
      - The guiding hand works mid-stroke: a changed hold steers on at the new attitude, the
        depth cap raised to the tool's greatest, so the bevel alone decides.
      - Free-hand direct strokes follow the drag's trail: the first point 4 mm ahead of the
        edge, a turn at 3° or more and at most 30°. The surface normal is re-fitted every
        millimetre.
      - Steering stops where a segment ended short (lifted out, blocked).
    - **Found on the way:**
      - An edge that lags a fast drag and heads for the pointer cuts inside the curve
        (4.5 mm); following the trail fixes it.
      - One quadratic bending tighter than its section made the body refuse the whole
        stroke.
      - Steering once a frame faceted curves at a few frames a second.
    - **Measured:**
      - Natively, a stroke steered on its way cuts as its plan (1 sweep); the bevel dives
        0.3 → 0.6 mm at tan 6°, stays level, and lifts out at 2.85 mm; a 90° arc steered
        every 3° is followed within 0.03 mm in 6 sweeps.
      - In the workshop (`steering`), a 40 mm quarter circle is followed within 0.17 mm,
        with at most 5 overlay edits; the hand mid-stroke gives 0.300, then 0.823 mm, held,
        then lifted out.
    - **Left:**
      - A stroke whose merged sweeps exceed the overlay (16 edits: a long, wandering curve)
        shows only its newest until it lands.
      - Its older sweeps could be applied as it goes.

  T5 is done. Rendered: a curve pared across the top and a chamfer planed to its lines on
  the end (`docs/images/t5_curve_and_chamfer.png`, `t5_chamfer_close.png`), and the
  attitude gauge.

**Left for later:** a view model per tool that looks held (it floats at a fixed offset);
putting tools down; the rack's stacks running out; a vise that holds a piece off the bench
(for sawing down, or working an end).

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
- **Engines.** `tools/compare_physics.sh` compares Jolt with GodotPhysics3D and Box3D.
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
  where one fits the gap) and several islands at once (one per check today). Crumbs under
  1 mm³ stayed in the body until D3, which takes every part under 30 mm³ out as debris.

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

## 2. Debris (done)
Built in three steps:
- **D1:** shavings and chips.
- **D2:** dust.
- **D3:** small pieces.

Dust is grains of its own (a MultiMesh), not `CPUParticles3D`, which cannot collide: each
grain's landing is cast against the physics space.

**Changed after play (the user):** debris lingered. Now it is transient. Shavings and
chips lie still 2 s, fade out over 0.5 s and are freed. Dust is a quick puff: a grain
shrinks away as it lands, and what reaches the ground (the bench, the floor) goes into a
pile there, one mound mesh growing with its wood (piles stay until Sweep; undo takes a
stroke's share back). The 1 mm stacking grid, rolling and the 30,000 kept grains are
gone.

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
    the kerf's ends (7 mm high); the block's dust lies on the face (as first built; now
    all of it in the two piles beyond the ends, 5.1 mm high, and the block's goes);
  - throwing costs about 0.6 ms a frame while dust comes (about 1 ms at most), the flights
    at most 0.4 ms (headless).
- **Left for later:** tools pass over piles without pushing them.

### D3 — small pieces (done)
Cuts meeting round a corner or an arris, or a V's crossing, can free bits too small to be
pieces of work. They used to split off as offcuts (a saw's sliver of any size, an island
of 1 mm³ or more) or stay in the body as floating specks (under 1 mm³).
- **One look collects them** (`find_island`'s `crumb`, 30 mm³). The same look round a cut
  as P2's; every part under 30 mm³ the region holds whole, however small, is a crumb, and
  all of them are returned at once, before any island. Islands are now 30 mm³ or more. The
  Keep's commit brings the next look to the same place, which finds any island left
  alongside them.
- **It refines first.** A coarse crease cell's samples, up to a millimetre apart, can miss
  a crumb altogether (a 1 mm sliver along an arris was not a part until refined). The
  look runs on a refine batch anyway, so it refines, then looks.
- **Cut out together** (`cut_out` over a set of parts): one region whose island cubes hold
  every crumb, its box the union of theirs plus the margin. If it cannot be proved, each
  crumb on its own (at most 16), keeping the ones that can.
  - **Fixed on the way:** beyond the octree's root cube (a millimetre round the work)
    `Octree::sample` gives only the distance to the cube, so cubes of the margin round a
    crumb at the work's edge were never clear and the cut failed ("material the samples
    do not show"). `cut_out` takes the body's own field there
    (`a_crumb_at_the_edge_is_cut_out`).
- **Taken out as a step.** The look only reads; when it is done, `SdfBody` queues the
  Keep (the rest of each region) as a stroke and commit of its own, if nothing else is
  queued and no tool is engaged (else the crumbs are looked for again once idle). When
  that step lands, `crumbled(chunks, step)` reports them.
- **A chunk each** (`measure_crumbs`): volume and centre from the part; points on its
  surface from the ADF (the island side's, within its bounds: at most 64, merged within
  0.2 mm); their convex hull (`pieces/hull.{h,cpp}`: incremental, triangles wound
  outwards; a flake too flat for one gets the box round its points). Each corner is
  coloured by the wood 0.2 mm inside it, read before the Keep. Crumbs under 0.2 mm³ are
  taken out with no chunk.
- **Saw slivers** (P1): a cut through whose smaller side is under 30 mm³ is not split;
  the look takes the sliver out as a crumb.
- **In the workshop** (`debris.gd`, `add_chunks`): the hull drawn faceted with its
  corners' colours, a `ConvexPolygonShape3D` of it for its collider, dropped from where it
  was like a chip; it rests and fades like one. It belongs to the Keep's undo step, so
  undo straight after takes the chunk back and puts the crumb back in the work.
- **Measured** (`native/tests/test_crumbs.cpp`, `game/tests/crumbs`):
  - 2 and 3 mm blocks cut free next to a 6 mm one: both crumbs found in one look (13 ms)
    and cut out together (22 ms), their volumes within 2% and their hulls within 10%; the
    board lost 25.5 mm³ (the crumbs 25.6); the next look finds the 6 mm block (162 mm³)
    as the island;
  - in the workshop, three scribed V grooves 2.7 mm deep round the board's front right
    arris free a 2.3 mm³ sliver (and a 0.05 mm³ speck where two grooves cross): looked
    for and cut out in 16 ms on the worker (9 ms of it cutting out and measuring), a
    chunk with a hull of 12 corners.
- **Left for later:**
  - a crumb is looked for only round a cut: undone, it stays in the work until a cut near
    it;
  - where the work's collider is still a box, a chunk born in the hollow it left is set
    down on top of the box;
  - redo does not bring a chunk back (nor any debris).
- Stone's percussion chips (8) will be debris too.

## G. Making an object (done)
The engine cuts wood as the real tools do, but a player can't yet make anything: no goal,
no idea what a piece is meant to become, no way to join parts. G adds the loop, built round
a first object, a **wooden mallet**: an oak head with a through mortise, an ash handle with
a tenon, and an oak wedge. It rehearses the pickaxe (head, handle, wedge) in wood, and
finished, it can be the mallet the chisels are struck with. Decided with the user:
- **Plans:** a part is a rectangular blank with features on its faces (cut to length,
  hole or mortise, notch or rebate, tenon, kerf, taper, chamfer). Presets are the same
  data; the player can draw new ones on paper, or draw on the wood with a pencil.
- **Checking:** the drawing guides, and the real shapes decide whether joints go
  together. An optional check shows where a part is proud of its drawing or short of it.
  No score.
- **Assembly is physical:** a part is offered up to its mate and slides along the joint
  until the shapes bind; a tight fit is driven home with the mallet, glue sets, a wedge
  locks it. The result is one rigid body that can still be worked.

**Order.** Plans come first: assembly needs to know which pieces are which parts and where
they meet, and the plan is what says so. G goes ahead of Surface finish and Bake, neither
of which blocks making an object.

**The player's way through it:**
1. At the side table, a plan book (presets) and a pad of paper: a sheet per part, each in
   three views with dimensions; a parts list with each part's state; joint call-outs.
2. Stock from the rack.
3. Lay out: the part's sheet in hand (the Layout slot), placed on the piece in the vise from
   a reference corner (face side and face edge); a click puts pencil lines on every face
   the part's features touch (an undo step), and the piece is tagged as that part.
4. Cut to the lines: pencil only guides; the knife and gauge snap onto it, and their lines
   stop the tools (T5-2). A quick option scribes the lines as they are transferred.
5. Check (optional).
6. Assemble: offer up, bind or go home, drive, glue, wedge; saw flush in the vise.

**The data** (`game/plans/*.json` presets, `user://plans/` the player's): a plan's parts and
joints. A part: its blank (L × W × T mm, x along the grain; face side +z, face edge +y) and
features. Each feature compiles into pencil lines (knife across the grain, gauge along it)
and into the part's intended solid (the blank less its features, an SDF body) for checks,
ghosts and each joint's pose. A joint pairs two parts' features with an insertion axis.
On a piece: meta `part = {plan, part, placement}` (part space to body space).

**Steps:**
- **G0** — stock in sizes (done, below).
- **G1a** — plans in the core (`plans/part`: part solids and lines), the mallet's preset,
  the plan book and viewer, transfer to pencil marks, the pencil, knife and gauge snapping
  to pencil. Test `plan_transfer`.
- **G1b** — the sheet editor: blanks and features drawn on the views, parts and joints,
  saved plans. Test `plan_editor`.
- **G2** — checking a part: proud and short spots against its solid, shown as dots and
  named. Test `check_part`.
- **G3** — assembly, in three steps (decided with the user: each tried before the next):
  - **G3a** — the fit of a part moving into another along the joint (won't go, drives,
    snug, loose). Tests `test_fit`, `joint_fit` (done, below).
  - **G3b** — offering up (carried to its mate in the vise, it lines up with the joint;
    E seats it at the mouth, the wheel pushes it in until it binds, mallet taps drive it
    home), the assembly as one rigid body (tools on the part hit); apart again until glued
    or wedged. Test `assembly`.
  - **G3c** — glue (G, a step the player chooses; sets after a minute) and the wedge
    (driven into the kerf, spreading it; locks the joint), sawn flush. Test `wedge_glue`.
- **G4** — the mallet end to end (decided with the user: the finished mallet becomes your
  mallet; a brace and bit if chopping is too slow; proven by a scripted build with the
  real tools, in a tier of its own):
  - **G4a** — chopping the mortise, measured (done, below).
  - **G4b** — the brace and bit, since it was too slow (done, below).
  - **G4c** — the whole build from the rack with the real tools (`mallet_build`, in
    `tools/test_godot.sh --long`) (done, below).
  - **G4d** — the finished mallet taken up as yours, its weight in the blows (done, below).

### G0 — stock in sizes (done)
- **`demo::stock(material, size)`:** a blank of a wood, `size` mm (length along x, the
  grain), flat-sawn (the pith 47.5 mm below it), arrises eased by a millimetre (less on
  stock under 5 mm). The board is `stock(…, {160, 100, 25})`, unchanged.
  `SdfBody.load_stock(wood, size)` makes one ("ash", "oak", "walnut").
- **The rack** (`room.gd` `STOCK`): a stack of each kind, its wood, size, name, shelf and
  place. The boards stay on the middle shelf; ash handle blanks (320 × 35 × 28), oak head
  blanks (120 × 70 × 55) and oak strips (150 × 30 × 10) lie on the bottom one. A piece is
  named by its kind ("an oak mallet-head blank").
- **The vise** already held a piece flat, on edge or on end. Its jaws closed no nearer
  than 30 mm, so a board on edge or a handle blank stood loose between them: now down to
  2 mm. Stepping up to the bench, the view backs off to take in a long piece (a handle
  blank on end: 0.52 m); nearer pieces keep the view as it was zoomed.
- **Measured** (`game/tests/stock`): every kind its size to 0.5 mm; its volume 96–99% of
  its box (the eased arrises, and the ADF's volume, which counts about 0.1 mm short all
  round the surface: a known bias of its voxel count); each clamped three ways, squared
  and standing on the bench top between closed jaws. (From G1a the strips are 150 × 12 × 5,
  a wedge's section, which the ADF measures 4% over its box: its volume is good to about
  0.1 mm of the surface either way, which on 5 mm stock is several percent.)

### G1a — plans, the mallet, laying a part on the wood (done)
- **Parts** (core `plans/part.{h,cpp}`). A part is a blank, L × W × T, its reference corner
  at the origin where the reference end (x = 0), the face edge (y = 0) and the face side
  (z = 0) meet. Features: a hole (a face, an x range, a range across, a depth or through), a
  tenon (an end, a length, its section), a kerf (from an end, a depth, square to y or z),
  a rebate or notch, a chamfer, a taper.
  - `part_lines(part, stock)`: the lines that lay it out on a piece of stock flush with it
    at the reference corner. Its size first: knifed to length round the stock where the
    stock is longer, gauged to width and thickness from the stock's far faces where it is
    bigger. Then each feature's, as T5-2 reads them (`as`): a hole knifed round (on the far
    face too if through); a tenon's shoulders gauged across from the end and its cheeks
    gauged from their faces on the faces beside and the end (T5-2's rebate floor and
    strip); a kerf knifed across the end; a rebate's and a chamfer's pair of gauge lines,
    knifed across where they stop; a taper's slope in pencil only (`Guide`). Lines on a
    face the stock does not have yet wait (`later`).
  - `part_solid(part, material)`: the blank eased as stock is, less the features (box
    subtracts, a chamfer's 45° box, a taper's half-space).
- **The mallet** (`game/plans/mallet.json`): an oak head 110 × 70 × 55 with a 30 × 12 mortise
  through it; an ash handle 300 × 35 × 28 with a 58 mm tenon (30 × 12: through the head and
  3 mm proud) and a wedge kerf 38 mm deep down its middle; an oak wedge 45 × 12 × 5 tapering
  to 1 mm. The stock matches the parts' sections (the strips became 150 × 12 × 5), so only
  their lengths are cut to lines.
- **The plan book** (`plan_viewer.gd`; E on it at the side table, or P): the plans, a plan's
  parts (wood, size, how many laid out) and joints, and a part's sheet (face side, face edge
  and end, third-angle; each face's lines, the far face's dashed, what the features take out
  dashed; the sizes; each feature in words). *Take this sheet* puts it in the Layout slot.
- **Laying it on** (`plans.gd`, `layout.gd`): hovered on the piece in the vise, the part
  shows in blue: its face side on the face pointed at (along the grain), its reference end
  the nearer end, its face edge that face's nearer edge (red where the piece is too small).
  A click draws it on as pencil marks (layout.gd's marks, `kind: "pencil"`, with `as`) and
  tags the piece (meta `part`: plan, part, placement). *Scribe a sheet as you lay it on*
  scribes them instead, as one step (`SdfBody.scribe_lines`: every line's V in one
  stroke).
- **Pencil only guides.** `hold()` ignores it. The knife or gauge within 1.5 mm of a pencil
  line on the face takes it (its own extent, knifed or gauged in as its `as` says). The
  pencil (a new Layout variant) draws a line across the face with a click, or along the
  nearest edge or across it with a drag, with its distances shown. Pencil is not an edit:
  undo straight after takes the latest pencil off (a piece's `pencil_log`), before any cut.
- **Models:** a pencil and a sheet of paper (`tools::Pencil`, `tools::Sheet`) for the
  Layout slot in hand.
- **Measured** (`native/tests/test_plans.cpp`, `game/tests/plan_transfer`): the head's
  mortise takes 19,800 mm³ (19,800) out of the solid, its lines on the solid's surface; the
  handle's tenon and kerf 36,328 (36,325); the wedge's taper and a stopped chamfer within
  1–2% once the eased arrises they take are counted; the head laid on its 120 mm blank in 12
  pencil lines where `part_lines` places them, the length line knifed in by a click beside
  it, then holding a chisel.
- **Left for later:** lines only straight and along the blank's axes (a curved part needs
  curved lines, and tools held to them); a part's far faces laid out only once the piece
  is its size that way; one part per piece.

### G1b — drawing a plan on paper (done)
- **The pad** (`game/workshop/plan_editor.gd`): a pad of paper on the side table (E: "draw a
  plan"), or *New plan*, *Edit* (the player's own) and *Edit a copy* (a preset) in the plan
  book. The plan's name and what it is; its parts, each a name, a wood, a count and a blank
  (length along the grain, width, thickness), with the rack's stock it is cut from
  (`plans.stock_for`: of its wood, the least that holds it either way round) or a warning
  that none is big enough; the joints (a part and a feature of it, another and one of its,
  a kind).
- **The sheet** (`sheet_view.gd`, split out of the plan book so both draw it the same way):
  the face side, face edge and end, each with the reference corner at its top left and in
  its own millimetres (the face side's x, y; the face edge's x, z; the end's z, y);
  `to_screen` / `view_at` map points both ways, snapped to 0.5 mm and onto the outline
  within 8 px; `footprints` gives each feature's boxes, for drawing it hidden and for
  picking it.
- **Features dragged on the views** (`feature_from`): a hole as a box on the face side or
  edge (through); a tenon's section as a box on the end (its length a quarter of the part,
  at most 40, to type), or a box from an end on the face side or edge (its length, and
  width or thickness; the other third-middle); a kerf across the end (square to the axis
  it doesn't run along) or from an end along a face (its depth); a rebate as a box in a
  corner of the end, or along an edge of a face (a third deep); a chamfer from a corner of
  the end as wide as the drag, or along an edge (2 mm); a taper as a line along the face
  edge, extended to both ends, off the face it is nearer. Each shows as the drag goes (a
  ghost of the part with it). *Pick* chooses one to name, size in its own fields, or take
  off (Delete).
- **Saving** (`plans.gd`): `user://plans/<id>.json`, the id from the name (never a
  preset's); loaded with the presets, flagged `mine` (not saved). A preset is only edited
  as a copy. Put down (P, Esc, or taking a sheet), the pad keeps what is on it
  (`plans.draft`) until saved or thrown away; *Delete* asks once more.
- **Measured** (`game/tests/plan_editor`, headless: the window is too small there to lay
  the pad out, so it is driven through the editor's own methods and its fields' signals,
  not screen points): the mallet drawn on the pad, each part laid out in the preset's
  lines; each tool's drag, and ones a tool can't use; joined, saved (each part naming its
  stock), its head laid on a blank in 12 pencil lines; edited and put down with P, the file
  unchanged and the pad going on with it; thrown away; a copy of the preset saved and
  deleted, the preset untouched. Found on the way: choosing the second part of a joint
  reset the first's feature.
- **Left for later:** drawing with the real mouse is not in a test (headless has no room
  for the pad's layout); features only along the blank's axes; no undo on the pad.

### G2 — checking a part against its drawing (done)
- **The check** (core `plans/check.{h,cpp}`, `check_part(part, wood, bounds, tolerance,
  spacing)`): the wood (its distance in part space) against the part as drawn
  (`part_solid`, its cuts run well past the blank so that from inside a cut the nearest
  face is its own), on a grid (1 mm) over both. Every grid point within a spacing of the
  wood's surface is moved onto it (the gradient) and measured against the drawing; every
  one within a spacing of the drawing's surface, onto that and measured against the wood.
  Off by more than the tolerance (0.5 mm; 1.25 along the blank's arrises, eased by a
  millimetre in the drawing): proud (wood beyond the drawing) or short (the drawing
  beyond the wood). The grid's cells of wood beyond the drawing, or of the drawing with no
  wood, deeper than the tolerance, give the volumes (each cell weighted by how much of it
  is beyond). Proud cells and places joined with their 26 neighbours into spots, and short
  ones: the worst, the area (each place weighted by its grid point's nearness to the
  surface), the volume, the worst place (the one nearest the middle of those near the
  worst, where the drawing's nearest face is not ambiguous), the drawing's normal there,
  the feature it is on (`part_solid` records each cut's feature), up to 150 dots. 40–60 ms
  for the head.
- **In the workshop** (`checking.gd`, `SdfBody.check_part`: the working body read through
  its octree, exactly, part space to body space by the piece's `placement`): K at the
  bench, or the part's panel (top right, under the work's). The spots named ("The far end:
  10 mm proud, wood to take off (about 36,340 mm³)"; "The mortise, 29 mm from the face
  edge"), dots on the wood (a MultiMesh under the work's SdfBody: orange proud, blue short,
  sized by how far off). It stands until the wood changes, the piece leaves the vise, or K.
- **Found on the way:** `Body::sample` skipped an edit whose box was exactly `|d|` away,
  so at a point on the surface (d = 0) inside a cut's box the cut was skipped. Strictly
  beyond now.
- **Measured** (`native/tests/test_plans.cpp`, `game/tests/check_part`): as drawn, no spots
  (nor on a square-edged head); the head on its 120 mm blank: 10.00 mm proud at the far end,
  36,343 mm³ beyond the tolerance (9.5 × 70 × 55 less the arrises); a mortise a millimetre
  narrow or wide: 1.00 mm proud or short, on the mortise's side 29 mm from the face edge; a
  tenon pared 0.8 mm thin: 0.80 short on the tenon; an uncut mortise: 6.00 proud, 17,545
  mm³. In the workshop, 50–100 ms: the head's blank laid out, proud at the far end and the
  mortise; sawn to length on the waste side of the line: the mortise only.
- **Left for later:** a part checked only where it is laid out (one placement); the
  check on the main thread (a tenth of a second); dots thinned by grid order.

### G3a — a joint's fit (done)
- **The joint** (core `plans/joint.{h,cpp}`, `joint_pose(a, fa, b, fb)`): which part goes
  in, how it lies in its mate once home (`home`: its part space into the mate's), the way
  it goes in and how far (`axis`, `travel`: from the mouth, its leading end on the mate's
  face), and what of it goes in (`region`). A tenon into a hole: along the tenon, into the
  hole's face, the section's longer side along the hole's, home with the shoulder on the
  face (the mallet's handle: 58 mm into the head's face side, 3 mm proud of the back). A
  wedge into a kerf: thin end first, its thickness across the kerf, home at its bottom.
- **The fit** (core `pieces/fit.{h,cpp}`): the moving part's surface (`surface_points`:
  grid points within half a millimetre of it, moved onto it; about 7,000 for the tenon),
  carried along the way in half-millimetre steps and measured in the mate's field
  (`parallel_for`). Each point is a side (its normal across the way: a cheek) or an end
  (along it: a shoulder, the tenon's end). A side point inside the mate: interference, how
  far the mate's wood runs into it across the way (out along the mate's nearest surface
  where that lies across the way, else back along the side's normal: near the mouth the
  nearest surface is the face it is just under). An end point on the mate, facing it end
  on, past the first millimetre (not the tenon's end catching the mouth's rim): seated.
  It stops where the interference passes the drive limit (0.5 mm) or it is seated. Home
  and never over 0.05 mm: snug (by hand), unless over 0.1 mm clear each side (loose);
  over 0.05: drives (the mallet); short of home: won't go. The clearance where it stops is
  its side's nearest approach to the mate's side (not a face it passes over end on).
- **Through the extension:** `SdfBody.joint_pose(a_part, a_feature, b_part, b_feature)`
  (static) and `part.fit(mate, start, axis, travel, step, region)` (one body's surface in
  the other's octree, both flushed first).
- **Measured** (`native/tests/test_fit.cpp`, `game/tests/joint_fit`): the joint poses (the
  tenon's corners on the mortise's, the wedge's thin end at the kerf's bottom, the joint
  named either way round); the tenon as drawn snug, home and seated (8,144 points, 117
  steps, 20–35 ms); 0.2 mm fat, it drives (0.11 mm interference); 1.2 mm fat, it won't go
  past the mouth; 0.4 mm thin all round, loose (0.20 mm clear); a mortise 0.6 mm narrower
  each side below 30 mm stops it at 30. The same through the extension on the plan's
  parts (`load_part`), 19–28 ms.
- **Left for later:** the surface a millimetre apart (a bump narrower than that can slip
  between the points); the wood's spring and the wedge's spreading (G3c: a drive limit per
  joint).

### G3b — offering up, tapping home, one body (done)
- **Offering up** (`game/workshop/assembling.gd`): carrying a part laid out from a plan,
  with the piece in the vise a part of the same plan and a joint between them
  (`plans.joints_between`), and looking at it: the carried part's outline shows in blue
  where the joint would put it, at its mouth (`candidate()`); E puts it there (frozen,
  kinematic, out of the hands). Its pose is the joint's (`SdfBody.joint_pose`) between the
  two parts' placements; where one was laid out mirrored, the joint's own mirror (a
  reflection of the moving part that maps what goes in onto itself: `JointPose::mirror`)
  makes it a proper turn. Either part may be the one carried (the other then is what goes
  in, carried onto it).
- **The fit going in** is measured once (`SdfBody.fit`, the whole way, 35–50 ms). The wheel
  pushes it in (1 mm a notch, Ctrl 0.2) as far as a hand can: to where it is more than
  0.05 mm tight ("It binds: tap it on"), or it stops; a click is a blow with the mallet
  (the chop's strength: 3 mm a firm blow) as far as up to 0.5 mm tight goes; home, seated.
  The wheel back draws it out; past the mouth, it is in the hands again.
- **One body:** E lets go. At least 3 mm in and not loose, the part's SdfBody moves into the
  piece in the vise (its rigid body goes), with its part's metas (`part`, `marks`,
  `pencil_log`, its wood and name): meta `members` on the piece ({sdf, to, joint, t,
  placements, the two bodies' step counts}). Colliders from each body (`_refresh_collider`),
  masses summed; each body's signals wired once to whichever piece holds it (`_wire`),
  a saw through a joined part splitting that part (`_on_separated(..., sdf)`).
- **The tools on the part hit:** with parts joined, hovering finds the body under the
  pointer (`body_hit`: each body's raycast) and the tools work on it (`board`); its
  metas through `holder()` (the piece for its own body, the joined part's SdfBody), which
  layout, plans, checking and the panel now use.
- **Apart again:** undo straight after joining (neither part edited since) takes it out into
  the hands; F on a joined part puts it back on its joint where it was, and the wheel draws
  it out. (Glue and the wedge, G3c, will hold a joint for good.)
- **`workshop.part_as_drawn(plan, part)`**: a plan's part made as drawn (`load_part`), face
  side up, tagged: for tests, and trying a joint without making its parts.
- **Measured** (`game/tests/assembly`, headless and rendered): the handle offered up, its
  tenon's corner on the mortise's to 0.01 mm; pushed home by the wheel (58 mm, snug); one
  body (colliders from both); undo: in the hands; home again; at the bench the pointer on
  the handle works on the handle (K: as drawn), on the head the head; F and the wheel back:
  out into the hands; a tenon 0.2 mm fat binds at the mouth and 21 blows drive it home;
  out of the vise and carried, the two go together.
- **Left for later:** a joined part's marks drawn only while the tools are on it; a
  tenon standing proud of the head in the vise goes into the bench top (the vise holds a
  piece on it).

### G3c — glue, the wedge, sawn flush (done)
- **Glue** (`assembling.gd`): G while a part is offered up spreads glue on the joint (the
  prompt says so). Joined glued, the glue sets `GLUE_SET` (60 s) of game time later (the
  assembling clock: the frames' deltas). Wet, the joint still comes apart (undo, F);
  set, it is there for good (`locked`: F says it is glued in, undo leaves it). The part's
  panel lists each joined part and how it is held ("glued: sets in 42 s", "held for good
  (glued)", "comes out again (F)").
- **The wedge:** a joint that is driven tighter: `SdfBody.fit(..., drive)`, a wedge's
  2.5 mm (it opens its kerf) against 0.5 for a tenon. The wedge goes in with its middle
  line along the kerf (tapered on one face, tipped by half its taper, `joint_pose`), so it
  opens the kerf evenly: 0.1 + 0.044 mm a millimetre in, each side. A blow drives it less
  far the tighter it is (`DRIVE × blow × max(0.2, 1 − tight/drive)`): 2.9 mm the first,
  0.1 the last, 24 blows home. Driven 10 mm or more, it holds itself and the joint it is
  in for good. Offered to the handle already joined in the head: `candidate()` finds the
  joint among the piece's parts (`holders_of`).
- **On end:** T tips a carried piece a quarter about the horizontal (across the view). The
  vise sets a piece of joined parts by all of them (`_vise_pose`: squared by its first
  part, set down by every part's extent) and closes on the narrowest: the mallet stands on
  its handle, the head up, the tenon's end 3 mm proud of it, where the wedge goes.
- **Sawn flush:** a saw through a joined part splits that part (`_on_separated(..., sdf)`,
  from G3b): the wedge's and the tenon's proud ends come away as offcuts, the parts still
  one.
- **F on a joined part** never takes the whole piece out of the vise (it did when the part
  was held for good: found by the test).
- **Offered, the part is kept on its joint** every physics step, its velocities stopped
  (still moving as the hands steered it, it drifted off in the frame after).
- **Measured** (`native/tests/test_fit.cpp`, `game/tests/wedge_glue`, headless and
  rendered): the wedge into the kerf drives home, its interference 0.1 + 0.044 mm a mm
  along the way; in the workshop, the handle glued, undone wet, glued again, set (F and
  undo leave it, the head still in the vise); out, tipped, back in the vise on its
  handle (its foot at the bench top, the tenon 3.0 mm proud); the wedge binding at the
  mouth, driven home in 24 blows, wedged; sawn flush: two offcuts, the wedge's and the
  tenon's tops within 0.01 mm of the head's.
- **Left for later:** the kerf does not really open (nor the tenon spread: the wedge's
  hold is by rule); glue shows only in words; the wedge's tip, eased as stock is, does not
  seat square on the kerf's bottom.

### G4a — chopping the mortise, measured (done)
- **Measured** (`native/tests/test_mortise.cpp`): the mallet's mortise (30 × 12, 55 deep,
  through) in an oak head blank, chopped as a joiner does with the 12 mm bench chisel held
  upright and firm blows: along it from end to end in 3 mm steps, the bevel towards the
  cuts made (each chip pops off into them), pass after pass, half from each face. 110
  blows from each: **220**, at least 77 s at a blow each 0.35 s. Over the ~150 set as too
  slow for play: the brace and bit (G4b).

### G4b — the brace and bit (done)
- **Core `tools/boring.h`:** an auger `Bit` (8, 10, 12 mm; a lead screw drawing it in
  1.6 mm a turn, whatever the wood, its point 4 mm ahead of the cutters) and a `Brace`
  (a steel crank of 200 mm sweep, a chuck, a walnut grip and head: `Brace::model()`).
  `boring_stroke()`: move_to() points go round the bit's axis, the grip's angle; each turn
  clockwise (from outside the work) draws it in by the pitch times the pace; turned back
  it stays (a ratchet). The hole is a cylinder from 5 mm above the surface to the cutters,
  the screw's hole ahead; updates re-cut the newest slice until it is 5 mm deep (as the
  saw's kerf). It stops through (the deepest wood under its disc, `depth_through`) and a
  millimetre beyond, or at a gauge line's floor. `hold_bit()` keeps its rim inside marked
  lines (halfway between two closer than it is wide). Coarse dust at the mouth.
- **SdfBody:** `load_tool("brace", {variant})`, `begin_stroke("brace", ...)` (it reads the
  body: its through depth), the bits in `tool_catalog()` (family "brace"), and
  `distance_at(point)` (the field at a world point, for tests).
- **Workshop:** the brace is a tenth slot, **B**, after Layout. Pressed on the face, it
  is set square to it (the piece's nearest face, from the hit's own normal: the surface
  fitted over a few millimetres leans by a hole beside it, and a brace tilted 22 degrees
  went out of the side) and held to the lines (`layout.hold`: sides and ends both walls).
  Dragging round the bit asks the grip round to the pointer's angle; it follows at 2
  turns a second times the pace, an eighth of a turn at a time. Not planned (Space). Its
  outline is the bit's rim. The line by the pointer: how deep, a turn's feed, *through*.
- **Chops rest on the highest wood under the edge** (`plan_chop`), not under its middle:
  a chisel whose middle stood over a bored hole took the slit's depth as the hole's and
  chopped the whole wall in one blow. **And only the edge over wood severs** (the blow's
  width is the part of the edge over wood): paring the cusps a hole leaves goes deeper
  than chopping solid wood.
- **The check** (`plans::check_part`) takes a point near the wood's surface as on it only
  where the wood's field there is near zero with wood behind it: a hole run out a
  millimetre past a face, and the seams where cuts from two faces barely overlap, dip the
  field towards zero in the air, and it read them as wood 6 mm and 1.2 mm proud.
- **Measured** (`native/tests/test_mortise.cpp`, `game/tests/mortise_chop`): a 12 mm hole
  through 55 mm of oak in 35 turns, its wall where the bit's rim went (air 0.2 mm inside,
  wood 0.2 mm outside, top to bottom), its updates cutting what its merged edit does. The
  mortise bored first, three holes 9 mm apart (**105 turns**: 52 s at 1×, 7 s of turning
  at 8×), then what they leave (cusps along the sides, the corners) chopped to the lines,
  the edge along each side and across each end, half from each face: **44 blows**
  (15 s), none past the lines. In the workshop, through its press and drag at 8× pace:
  each hole through (56 mm), where the lines hold it (set down 3 mm past the end and 3 mm
  off the middle); 44 blows; checked: as drawn to 0.5 mm.

### G4c — the whole build from the rack (done)
- **`game/tests/mallet_build.gd`, the long tier** (`tools/test_godot.sh --long`): each
  part's stock taken from its stack (`take_stock`), let go over the vise, its sheet scribed
  on; the head sawn to length, bored with the brace through the workshop's press and drag,
  chopped to the lines; the handle sawn to length, its tenon's four shoulders sawn across
  and four cheeks sawn in from the end (each down to its line: a floor at the tenon's face,
  or at the shoulder), its kerf sawn; the wedge sawn off its strip and its taper sawn from
  the thin end. Saws at their real rate, straight on the part's body; 8× the pace. Each
  checked (K) as drawn to 0.5 mm. Then put together as `wedge_glue` does, sawn flush,
  recognised as finished and taken up (G4d).
- **Measured:** play at 8× the pace (the saws' travel at their working speed, the brace's
  turns, the blows at 0.35 s): the head 26.3 s (26 strokes, 13.1 turns, 44 blows), the
  handle 9.5 s (57 strokes), the wedge 0.8 s (5), putting together 15.1 s (47 blows: the
  handle *drives* home in 23, the wedge in 24; 6 strokes flush): about 52 s. About 35 s
  to run headless.
- **Found on the way:**
  - **A part goes with its drawing** when a saw cut splits its piece (`_part_follows`):
    the smaller side is still the offcut, but the wedge, sawn off the end of its strip,
    took the strip's part tag with the strip. Now the tag and the lines go with the side
    the part's middle lies on (and back on undo straight after).
  - **A thin island after an earlier one** (`cut_out`): a tenon's edge strip (1.65 mm
    thin), sawn free after the thicker waste had come away, was found but not cut out
    ("material the samples do not show"): where an earlier cut-out dropped material, the
    field never falls below 0.05 mm, a shell of it read as near a surface with no samples.
    Cubes wholly in dropped cubes are now free air (`test_mortise`: the tenon's four
    wastes cut out one after another).
  - **A joined part that was an offcut** (the wedge) kept its record in the workshop's
    offcuts, its rigid body freed: now dropped when it joins.
  - **`SdfBody.is_idle()`:** not busy, and no look for crumbs or an island still to come
    (it looks once idle): a script waiting on it sees each cut's waste come away before
    the next cut.

### G4d — your mallet (done)
- **`assembling.finished(piece)`:** every part of one plan in the piece (as many of each as
  it has), every joint of the plan joined, each held for good: {plan, name}.
- **Taken up** (`workshop.take_up`): E on a finished piece lying about (the line: "E: take
  up the mallet as yours"). It leaves the bench (hidden, frozen, out of the pieces) and is
  kept as `workshop.mallet` ({piece, mass (all its parts', kg), name: "your mallet (oak and
  ash, 468 g)"}). Taking up another puts the first away.
- **Its weight in the blows:** `blow_weight()` = clamp(mass / 0.6 kg, 0.6, 1.6), 0.6 kg being
  the workshop's own mallet (`WORKSHOP_MALLET`); a chop's blow and an assembly's tap are
  struck that much harder or softer. The chisel's and gouge's panels name the mallet in
  use. A new board in the vise leaves it yours.
- **Measured** (`game/tests/your_mallet`, headless): the mallet as drawn, glued but not
  wedged, is not finished; wedged, it is; let go on the bench, taken up with E: 475 g, blows
  0.79 times; a firm chop on the ash board 4.79 mm deep where the workshop's mallet goes
  6.06 (0.79 times). The mallet made from the rack weighs 468 g (0.78 times): an oak head of
  110 × 70 × 55 is lighter than a joiner's beech mallet.

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
