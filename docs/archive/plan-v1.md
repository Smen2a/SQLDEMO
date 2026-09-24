> **Archived.** The first plan, kept for its design sections (1–13) and its detailed design
> of the bake path (E4). It is not maintained: the current plan is [../PLAN.md](../PLAN.md),
> and what was built is described in the [README](../../README.md). Milestone designs
> between E3 and W4 are recorded in the commit messages.

# SDF Engine — Design Plan (object builder)

## Context

The game is an **object builder**: players make functional objects (a pickaxe = steel
head + ash handle + wedge) from parts shaped by physically-appropriate processes, then
joined into one working object. The world also has buildings and people. The SDF engine
is the core of all of it and is the subject of this plan.

The repo currently contains only a throwaway Godot 4.7 SQL demo; nothing below is built.

| Decision | Choice |
| --- | --- |
| Engine | Godot 4.7 + C++ GDExtension (godot-cpp); SDF core engine-agnostic |
| Renderer | **Godot hybrid with a measured decision gate** (§4) |
| Look | **Deferred.** Engine built and validated at high resolution first; the eventual semi-realistic PS2-style look is a later presentation layer (§5) |
| Input | Desktop, mouse + keyboard |
| Scale | Small parts at ~0.1mm detail up to large bodies (stone blocks, ~2–3m) |
| Materials | Hardness + malleability drive which process applies (sharp for wood, heat + blunt for metal, percussion for stone) |
| Joining | Parts never blend; interlock / compression / glue joints with computed strength |
| Undo / export | Unlimited undo; manifold mesh export |
| First content | Relief panel, then the pickaxe as the assembly acceptance scene |

## 1. Body model — the source of truth

```
Body
├─ frame        local Transform, units = millimetres, origin at body centre
├─ material     base material + property fields (grain / strata / temperature / hardness zones)
├─ base field   analytic primitive (a blank)  OR  sparse sampled grid (forged, scanned, consolidated)
├─ edit list    ordered analytic ops, each with a blend mode
└─ history      full op log → unlimited undo; consolidation checkpoints for long jobs
```

**Why a sampled base at all:** forging is volume-preserving plastic deformation. Carving,
filing, drilling and stone chipping are subtractive and fit an analytic edit list;
thousands of stacked space-warps do not. Deformation works on a sampled grid;
subtractive work stays analytic on top. The same grid path serves consolidation of long
stone jobs and meshes made carvable on first touch.

| Process | Tool requirement (material table) | Operator | Lives in |
| --- | --- | --- | --- |
| Carving, filing, grinding | edge sharper and harder than material; grain-dependent | analytic subtract of swept tool profile | edit list |
| Percussive stone carving | harder than stone; impact energy | subtract a fracture-shaped chip, larger than the contact | edit list |
| Drilling, boring | as cutting | analytic subtract | edit list |
| Forging | blunt face harder than hot metal; inside forging window | grid advection + redistance + volume correction; temperature diffuses | base grid |
| Adding (putty, filler) | — | analytic add, smooth blend | edit list |
| Breaking | stress > strength | split: A = body ∩ crack, B = body ∖ crack | two bodies |
| Joining | — | assembly edge, never a field op | assembly |

The engine answers **material queries** (material, properties, grain vector at a point)
and **contact queries** (tool vs SDF: penetration, area, normal); gameplay picks the
operator.

## 2. Blend modes (within one body)

**The primitive decides the surface (curves); the blend mode decides the edge.** All
modes have compact support ≤ their radius, so an op's influence region is its bounds
expanded by the radius and per-cell culling stays exact.

| Mode | Edge | Family | Use |
| --- | --- | --- | --- |
| Hard | crisp arris | min / max | chisel cuts, joinery — default |
| Chamfer | flat bevel, width r | hg_sdf chamfer | eased edges |
| Round | circular fillet r, rigid outside it | circular-geometric smin / hg_sdf round | roundovers, sanded edges |
| Smooth | organic C1/C2 | quadratic / cubic polynomial smin | putty, filler |
| Profile | cove, ogee, bead | 2D operator f(dA, dB) from a profile curve | router bits, mouldings |
| Engrave / Groove / Tongue | detail along an intersection | hg_sdf ops | V-lines, tongue-and-groove |
| Asymmetric | fillet ra onto A, rb onto B | scaled 2D operator | fillets biased to one side |

Excluded from edit lists: exponential / root / sigmoid smin (infinite support defeats
culling). Radius below the body epsilon collapses to Hard; radius is clamped to the local
feature size of both operands. Every mode returns a **material weight**: smooth blends
mix, hard ones select.

## 3. Materials and joining

1. **Separate parts → separate Bodies, never blended.** Composited by the depth buffer;
   the seam is exact and free.
2. **Material layers inside a body** — stain, char, heat-treatment hardness zones, knots,
   veins — hard or soft transitions, stored as ops so undo covers them.
3. **Procedural fields** — grain, rings, strata — evaluated in body-local space, so cuts
   and breaks reveal the right figure.

**Assembly** = graph of bodies and joints. Strength is computed from the SDFs:

| Joint | Holds by | Strength (sampled over the contact region) |
| --- | --- | --- |
| Interlock — eye + handle, dovetail, mortise-tenon | geometric capture | blocked directions (declared per joint type first, computed later) |
| Compression / interference fit, wedges | friction under pressure | pressure ≈ E_eff · overlap / length; hold = μ · ∫ pressure dA |
| Glue | adhesion | area where gap < glue-line max, weighted by grain (long ≈ 1, end ≈ 0.1–0.2) |

The same sampling yields a **fit heatmap** for hanging a head. An intact assembly is **one
RigidBody3D with a compound shape**; impacts compute load through each joint, partial
overloads **loosen** it, and failure splits the assembly into separate rigid bodies.
Merging joins (weld bead, glue squeeze-out) are reserved as "joint bodies" for later.

## 4. Rendering — Godot hybrid with a decision gate

**The SDF is always the truth; meshes are disposable caches. A body's view is chosen by
mutability first, then screen-space error.**

| State | Display | Physics |
| --- | --- | --- |
| **Live** — being edited, cache stale, fresh fragment, or inspected closer than its bake | raymarched | convex hull from SDF surface samples |
| **Baked** — everything else | decimated mesh + normal / AO maps (§5) | convex decomposition (dynamic) or trimesh (static) |

- **Live → Baked**: debounced async bake when edits stop or on set-down.
- **Baked → Live**: instant — pick up to edit, or zoom past the bake's error.
- **Break**: exact analytic split → fragments Live at once → quick hulls → async bake.
  Fragments below a volume threshold become chip particles, never raymarched.
- **Edit a finished object**: Live instantly; set-down rebakes dirty chunks only.

**Why not full raymarching, and why not our own engine now:** people and foliage are mesh
problems in every option, so every option is a hybrid — the choice is who owns it.
Full raymarching in Godot fights its shadow passes, early-z, GI and culling, and physics
needs meshes regardless. Our own engine means rebuilding renderer, animation, UI, audio,
navigation, tools and export before the game — Dreams took ~4 years and three abandoned
renderers. Godot keeps all of that; the SDF core stays engine-agnostic so a later move
reuses everything but the render glue.

Shadows for Live bodies (updated by E3): **casting needs no proxy mesh** — Godot's shadow
pass runs the raymarch shader and takes its `DEPTH`. Receiving: Forward+/Mobile look
shadows up per fragment at `LIGHT_VERTEX`; the Compatibility renderer computes shadow
coordinates per vertex (on the proxy box), so there Live bodies don't receive shadow-map
shadows. SDF soft self-shadowing in-shader remains a later look item.

**Decision gate (end of E3)**, measured on the user's hardware — this container has no
GPU and no Vulkan ICD, so it can verify correctness only:

| Check | Pass | If it fails |
| --- | --- | --- |
| Live raymarch cost at native 1080p, bench-filling body | ≤ ~6ms GPU; 3 Live bodies ≤ ~8ms (initial targets, calibrate in the spike) | move Live bodies to custom compute passes (option B) |
| Shadow-pass behaviour with raymarched DEPTH | correct, or proxy mesh suffices | proxy mesh (already planned) |
| Depth composition with meshes | exact intersections | B |
| Live vs Baked parity at swap distance | image diff under threshold | fix shading; if structural, B |

Only a failure of both perf and lighting integration reopens the own-engine question.

## 5. Detail — high resolution now, style later

The engine is built and validated at **full resolution first**; the look comes after.

- **Live**: raymarch at native resolution; continuous procedural materials.
- **Baked**: feature-preserving decimation to a generous triangle budget, **normal maps**
  (RG16 / BC5 to avoid banding) and **AO baked from the SDF** — the same AO function the
  Live shader evaluates.
- **Parity**: shared material include + same Godot lighting path + same AO function +
  swaps gated on < 1px screen-space error. If self-shadow pops at the swap, Baked assets
  gain a small per-object SDF volume for shadows (Unreal's mesh-distance-field idea).
- **Every detail knob is a parameter** — internal render scale, bake triangle budget,
  texture size, material texel quantization. The later semi-realistic PS2-style pass
  (low internal res, low-poly bakes, texel-quantized materials so Live and Baked share a
  texel grid) is then a settings change, not an architecture change.
- Validating at high resolution is the **worst case**: anything the gate passes at 1080p
  only gets cheaper when stylized.
- **Simulation precision is independent of display**: fits, joints and contacts always
  use the exact SDF.
- People and buildings stay ordinary Godot meshes in every case.

## 6. Scale

- **Body-local millimetres, float32** — a 3m body still resolves ~2·10⁻⁴mm.
- **Adaptive octree per body** for edit culling: subdivide where pruned lists exceed a
  budget or edit feature scale is small; flat stone faces stay coarse.
- **View-dependent precision**: hit epsilon = pixel-cone radius at distance t.
- **Consolidation** collapses roughing edits into the sparse base grid at a resolution
  matched to their feature scale; history stays for undo.
- **Precision input**: zoom-dependent mouse gain, snapping 1 → 0.1 → 0.01mm with zoom,
  depth ticks, numeric entry.

## 7. Evaluation core (C++, shared by everything)

- Compile each body into **per-cell tapes** (flattened op lists, libfive / Fidget style)
  over the octree, with **interval pruning**: ops that cannot change the result drop out;
  an op that swallows a cell resets it to constant. Blend operands separated by more than
  the radius prune as Hard.
- Full per-cell membership kept for undo; pruned tapes derived from it.
- Cached per-cell distance bounds for empty-space skipping.
- One evaluator serves tool contact, raycasts, joint/fit sampling, scoring, DC Hermite
  data (forward-mode dual-number gradients) and the CPU reference renderer.
- Incremental: new op → influence AABB → touched cells → re-prune → partial texture upload
  → dirty chunks queued for rebake.
- **Op formulas have one source** in a C/GLSL-compatible subset, shared by the C++
  evaluator and the shader, enforced by a parity test.

## 8. Live path

Proxy box per body → spatial shader raymarches in body-local space, interpreting the
cell's tape from data textures (cell table, tape indices, op parameters, material table).
Empty cells skipped by cached bounds; active cells sphere-traced; normals by tetrahedral
4-tap on the local tape; top-2 material weights shaded; writes DEPTH. Material
evaluation takes an optional texel-quantization parameter, off for now (§5).

## 9. Bake path

Dual Contouring from the octree field with one vertex per surface patch per cell
(manifold by construction) → SDF-checked QEM decimation → UV atlas via **vendored xatlas**
(Godot's `lightmap_unwrap` is editor-only: `modules/xatlas_unwrap/config.py` builds only
with `editor_build`) → bake object-space normal, albedo and SDF AO maps → Baked shader.
V-HACD *is* in runtime builds (`modules/vhacd`), so convex decomposition stays available
for E5. Export writes the mesh (OBJ / binary STL); the Manifold library gate is deferred
until booleans are needed. Details: **E4 implementation plan** below.

## 10. Code layout

```
native/src/core/      no Godot headers — unit-testable standalone
  body/   ops/   compile/   eval/   bake/   deform/   fracture/   assembly/
native/src/godot/     SdfBody, SdfAssembly, texture upload, Live/Baked state machine
game/shaders/         sdf_live.gdshader, sdf_baked.gdshader, shared material include
native/tests/         Godot-free tests
extern/godot-cpp/     pinned to 4.7        extern/manifold/
tools/run.sh          headless run + Xvfb screenshot (golden images)
```

The SQL demo files (`scripts/`, `scenes/main.tscn`, `docs/screenshot.png`) are removed
in E0; `tools/run.sh` is kept and extended.

## 11. Milestones

| | Deliverable | Proves |
| --- | --- | --- |
| E0 | Repo reshape; CPU core: primitives, swept profiles, all blend modes, material weights, evaluator | the maths |
| E1 | CPU reference raymarcher → golden PNGs: blend-mode and material galleries | visual truth without a GPU |
| E2 | Octree + interval pruning + incremental updates + consolidation; 10k / 100k-edit perf | edit scaling |
| E3 | **Spike**: Live path in Godot + measurements → **decision gate** | the hybrid is viable |
| E4 | Bake path (high-res, all budgets parameterized) + Live↔Baked state machine + parity | the cache |
| E5 | Assemblies: joints, strength sampling, fit heatmap, compound body, loosening, failure | the pickaxe |
| E6 | Fracture per material; fragments Live→Baked; chip particles | breakage |
| E7 | Forging: sampled base, temperature, hammer deformation, malleability | metal |
| E8 | Large-body job: 2m stone block, 100k percussive edits | scale |

## 12. Verification

- **Per-op property tests**: reduces to Hard as radius → 0; equals Hard outside support;
  sampled Lipschitz ≤ 1 on random primitive pairs; material weights ∈ [0,1].
- **Pruning soundness**: pruned vs full evaluation agree at random points in every cell.
- **Golden images** from the CPU reference renderer (runs in this container).
- **GPU vs CPU** and **Live vs Baked** parity renders via `tools/run.sh --screenshot`
  under Xvfb (Compatibility renderer).
- **Joint tests**: known interference → sampled overlap and strength match analytic values.
- **Scale tests**: zoom series 1×/10×/100× on a 0.2mm groove — no stepping.
- **Gate perf numbers** printed by a benchmark scene the user runs on real hardware.

## 13. Risks

- Shader tape-interpreter speed on low-end GPUs → subdivide cells to shorten tapes; the
  gate decides.
- Godot shadow-pass behaviour with raymarched DEPTH unverified → proxy mesh fallback.
- Only Compatibility is testable here; Forward+ checks run on the user's machine.
- Forging (advection + redistance) is research-grade → isolated in E7.
- Profile blends are exact only for perpendicular surfaces; documented approximation.

*Research tracks on analytic SDF rendering, blend operators/materials, and Godot 4.7
specifics were still running when this was written. Their findings refine E0 formulas,
E2 pruning and the E3 spike; they do not change the architecture above.*

---

# E4 implementation plan — the bake path (after E3c/E3d)

## Context

E0–E3 are done and pushed: the SDF core, the octree, the CPU reference renderer and the
Live raymarch node `SdfBody` (GPU/CPU parity verified). The user is running the E3 gate
benchmark on their own hardware; whatever it says, every body still needs a **Baked**
form: a cheap mesh for display at a distance, the source for physics shapes (E5), and
the exportable mesh. E4 builds that path and the **Live↔Baked state machine**. The
SDF stays the truth; a bake is a disposable cache that is shown only when it is
indistinguishable from Live (< 1 px of error on screen).

Decisions baked into this plan:
- **Whole-body bakes, chunked internally for parallelism**, run asynchronously. Rebaking
  only dirty chunks (with a tiled atlas) is deferred to E8, where 2 m stone blocks need it;
  bench-scale bodies bake in seconds and stay Live while being edited anyway.
- **Object-space normal maps** read by our own `sdf_baked.gdshader`: exact, and no tangents.
- **xatlas vendored** as a submodule (MIT), because Godot's unwrapper is editor-only.
- High resolution by default (user's instruction); every size is a `BakeParams` field.

## Steps (each ends green, committed and pushed)

### E4a — Dual Contouring mesher (`native/src/core/bake/mesher.{h,cpp}`)
- Grid of cell size `h` aligned to multiples of `h` in body space, covering
  `body.bounds()` plus one cell. Processed in 32³-cell chunks across threads (pattern of
  `eval/reference_renderer.cpp`'s worker pool).
- Corner signs: taken from the octree leaf state where it is Empty/Solid, and evaluated
  with `Octree::sample` only in Surface leaves (`compile/octree.h`). Chunks that touch no
  Surface leaf are skipped.
- Sign-changing edges: the zero crossing by Illinois regula falsi (≤ 8 iterations), and
  the normal by a tetrahedral gradient with step `max(0.01 h, 1e-3 mm)`.
- **Manifold by construction (dual marching cubes):** each cell gets one vertex per
  surface patch. Patches are the connected components of the cell's crossing edges,
  joined along its faces. An ambiguous face (4 crossings) always pairs its edges so that
  inside corners stay separate, so both cells sharing the face agree. This is a
  256-entry table computed at startup.
- Per patch vertex: QEF from its edges' Hermite data (plane equations), solved around
  the mass point with a symmetric eigen-decomposition and truncated small eigenvalues,
  then clamped to the cell.
- One quad per crossing edge from the four cells' patch vertices, wound by the edge's
  sign direction and split along the better diagonal.
- Output `TriMesh {positions, normals, indices}` plus stats.
- `BakeParams`: `cell_size` (default 0.25 mm, raised if the grid would pass 2048 cells per
  axis), `max_error` (default 0.05 mm), `triangle_budget` (default 250k),
  `texel_size` (default 0.1 mm), `max_texture_size` (4096), `padding` (4 texels),
  `threads`.

### E4b — SDF-checked QEM decimation (`bake/decimate.{h,cpp}`)
- Garland–Heckbert edge collapse (area-weighted plane quadrics, optimal placement with an
  endpoint/midpoint fallback) on vertex→face adjacency.
- A collapse is rejected if it:
  - breaks the link condition (keeps the mesh manifold);
  - flips any face normal;
  - leaves a sliver face;
  - puts the new vertex, or a modified face's centroid, further than `max_error` from the
    SDF (octree evaluation). This bounds the geometric error directly, and QEM keeps crisp
    creases.
- Stops at `triangle_budget` or when the queue has no acceptable collapse left.
- `measure_error(mesh, octree)` samples every triangle (centroid, edge midpoints, a few
  barycentric points) and returns max and RMS |d|. It is stored in the bake and used by
  the swap rule.

### E4c — Atlas and texture bake (`bake/atlas.{h,cpp}`, `bake/texture_bake.{h,cpp}`, `bake/bake.{h,cpp}`)
- Add `extern/xatlas` as a submodule (jpcy/xatlas, MIT) and compile `xatlas.cpp` into a
  new `sdf_bake` static library (PIC, warnings off for that file).
- `atlas.cpp` runs xatlas with texels-per-mm = 1/`texel_size` (capped at
  `max_texture_size`) and `padding`, then re-indexes vertices at UV seams.
- Texture bake, rows spread over threads:
  - Rasterize each triangle in UV space. For each covered texel, take the mesh point,
    project it onto the SDF with two or three Newton steps (bounded by 2·`max_error`), and
    evaluate there.
  - Normal: gradient at the projected point.
  - Material: `Octree::sample` plus `MaterialTable::albedo`, mixing exactly like
    `reference_renderer.cpp`'s `surface()`.
  - AO: the same 5-tap SDF occlusion the Live shader uses.
  - Then dilate by `padding`.
- Move the tetrahedral normal and the occlusion taps out of the renderer's private `Tracer`
  into a shared `eval/field_shading.h` (templated on a distance functor), so the reference
  renderer and the baker share one definition.
- Textures:
  - A: RGBA8 = sRGB albedo, specular.
  - B: RGBA16F (half, with a small float→half helper) = octahedral normal xy, AO,
    roughness. The roughness and specular values match `sdf_live.gdshader`.
- `bake(body, octree, materials, params, cancel_flag) → BakedMesh {vertices (pos, uv),
  indices, textures, cell_size, max_error, rms_error, texel_size, timings}`.
  `BakedMesh::resolution() = max(max_error, texel_size)` is what the swap rule compares
  against a pixel.
- **CPU mesh rasterizer** (`eval/mesh_renderer.{h,cpp}`): z-buffered, perspective-correct,
  bilinear texture sampling, the same `Camera`/`RenderSettings` and Normals/Albedo outputs
  as the reference renderer. This gives a Baked-vs-reference parity check with no Godot.
- `sdf_render --baked` renders a demo's bake the same way (for diffs and README images).

### E4d — Godot: Baked display and the state machine (`native/src/godot/sdf_body.*`, `game/shaders/sdf/sdf_baked.gdshader`)
- `sdf_baked.gdshader`:
  - decodes the octahedral normal (object space), then
    `NORMAL = VIEW_MATRIX * MODEL_MATRIX * n`;
  - ALBEDO (texture `source_color`), SPECULAR, ROUGHNESS, AO;
  - debug views for normals and albedo with the same encodings as Live, so `parity.sh`
    applies unchanged.
- The bake is built into an `ArrayMesh` (positions, UVs, indices) plus two
  `ImageTexture`s (`FORMAT_RGBA8`, `FORMAT_RGBAH`).
- The state machine runs in `_process`:
  - Every edit (`load_demo`, `add_random_strokes`, future edit calls) bumps a
    generation and shows Live at once.
  - After `bake_delay` s (default 0.5) with no edits and no bake in flight, snapshot
    `Body` + `Octree` and bake on `std::async`.
  - Poll the future each frame. A result from an older generation is discarded and
    rebaked. The destructor sets the cancel flag and joins.
  - `display_mode` AUTO/LIVE/BAKED. AUTO shows Baked when
    `resolution / (distance to the body's bounds, in body units × pixel angle) < 0.8 px`
    and returns to Live above 1.0 px (hysteresis). The pixel angle comes from the active
    Camera3D's fov and viewport height; orthographic cameras use their size instead.
- API:
  - properties `display_mode`, `bake_delay`, `bake_cell_size`, `bake_max_error`,
    `bake_triangle_budget`, `bake_texel_size`;
  - `bake_now()` (synchronous, for tests), `is_displaying_baked()`, `get_bake_stats()`;
  - signals `bake_finished(stats)` and `display_changed(baked)`.
- Known Compatibility-only pop: Baked meshes receive shadow-map shadows there and Live
  bodies don't (E3 finding). Documented; Forward+ is unaffected.
- **Shadow caster for Live bodies (from E3b):** Live bodies no longer raymarch in shadow
  passes. While Live, the latest bake is drawn as a `SHADOWS_ONLY` child instance, so the
  shadow is at most one rebake stale. Before the first bake there is no shadow;
  `live_shadows` restores exact raymarched casting at its cost.

### E4e — Verification in Godot, bench, export, docs
- `parity.gd`/`parity.sh` gain a display dimension (`<demo>:<view>:<live|baked>`):
  - Baked cases are diffed against the CPU reference.
  - A new `live_vs_baked` mode renders each demo shaded as Live and as Baked, from a camera
    at the computed swap distance, and diffs them. This is the "pop" metric.
- `tests/display_switch.tscn` drives AUTO mode:
  1. load the panel, far camera → the bake completes and shows Baked;
  2. move the camera close → Live;
  3. add strokes → Live plus a rebake, then Baked again.

  It prints each transition; `test_godot.sh` checks the expected sequence.
- `live_bench.gd` adds `panel_fill_baked` and `three_bodies_baked`, so the gate numbers
  include what Baked saves.
- Export: `bake/export.{h,cpp}` writes OBJ and binary STL. `SdfBody.export_mesh(path,
  cell_size)` re-meshes at export resolution without decimation and checks the result is
  closed, manifold and consistently oriented.
- README section and images (a Baked render plus its wireframe); plan §9 already updated.

## Files
- New:
  - `native/src/core/bake/{mesher,decimate,atlas,texture_bake,bake,export}.{h,cpp}`
  - `native/src/core/eval/{field_shading.h,mesh_renderer.{h,cpp}}`
  - `native/tests/test_bake.cpp`
  - `extern/xatlas` (submodule)
  - `game/shaders/sdf/sdf_baked.gdshader`
  - `game/tests/display_switch.{gd,tscn}`
- Changed:
  - `native/CMakeLists.txt` (`sdf_bake` library; link into tests, tools and the extension)
  - `native/src/core/eval/reference_renderer.cpp` (use `field_shading.h`)
  - `native/tools/sdf_render.cpp` (`--baked`)
  - `native/src/godot/sdf_body.{h,cpp}`
  - `game/tests/parity.gd`, `tools/parity.sh`, `tools/test_godot.sh`
  - `game/bench/live_bench.gd`, `README.md`
- Reused as is:
  - `Octree::sample/step`, `Body`, `MaterialTable::albedo`, `demo::named_demo`
  - `Image`/`compare`/`write_png` (`eval/image.h`, `io/png.h`)
  - the worker-pool pattern in `reference_renderer.cpp`
  - `tools/run.sh`

## Verification
- `native/build/sdf_tests` gains `test_bake.cpp`, run on every demo:
  - closed, edge-manifold, consistently oriented, no degenerate faces;
  - every vertex within 0.1·h of the surface before decimation, and the measured error
    ≤ `max_error` after;
  - sphere and box volumes within 0.5% of analytic;
  - a plain box decimates to ≤ 50 triangles with sharp corners intact;
  - byte-identical results across thread counts;
  - baked texels' normals and AO match the field at their projected points;
  - CPU Baked-vs-reference normals/albedo diff under threshold at a camera where
    `resolution` < 0.5 px.
- Keep all 34 existing tests and the goldens green, under GCC and Clang.
- `tools/test_godot.sh`: the existing checks, plus Baked parity, Live-vs-Baked parity at
  swap distance, and the display-switch transition sequence.
- Report bake timings, triangle counts and errors for the panel, the sphere and the
  300-/2000-stroke sessions, and send screenshots (Live vs Baked side by side, and the
  wireframe).
