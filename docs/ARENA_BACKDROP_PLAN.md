# Arena Environment Plan

Status: Active
Type: Roadmap
Last updated: 2026-07-28

This roadmap applies the source-first rules in
`LGPE_ENVIRONMENT_FIDELITY_CONTRACT.md` to the PokemonAutochess arena.

## Goal

Use the original LGPE environment as the arena environment while preserving as
much of its geometry, texturing, layering, vegetation, materials, lighting
response, shadows, wind, animation, and other runtime behavior as possible.

Make only the smallest layout adjustment required to place and read the
autochess board. The result should look like the original route was locally
opened or rearranged for the board, not like a newly designed environment
inspired by that route.

## Current Implementation Status

The current projected backdrop implementation lives primarily in:

- `src/game/runtime/session/SessionWorldBackdrop.cpp`;
- `src/game/runtime/session/SessionProjectedWorldView.cpp`;
- `src/game/runtime/session/SessionWorldRenderRuntime.cpp`.

It contains procedural route-shell styles for Route 1, Route 22, Route 2,
Viridian Forest, and Route 3. Those shells are provisional legacy presentation
and useful renderer scaffolding. They are not fidelity authorities and must not
dictate the imported environment's appearance.

The existing `WorldScene` path remains the intended runtime rendering layer.
The source asset pipeline feeding it must be upgraded.

## Source and Review Authority

For Route 1:

- the user's original LGPE dump supplies the source files;
- the sibling environment workspace records extraction evidence, runtime
  captures, reconstruction scripts, validators, promoted Blender checkpoints,
  and restore points;
- the current promoted Blender checkpoint is the editable review authority;
- the original files and native captures remain the higher-priority fidelity
  authority when a Blender interpretation is questioned.

Do not copy ROM-derived source files into this repository. Tools should accept a
local source-root configuration and emit provenance records containing hashes,
not redistribute the source dump.

## Architecture Direction

1. Build a command-line offline LGPE importer owned by the project.
2. Decode original source streams and metadata directly, initially using
   Route 1 as the complete vertical slice.
3. Produce a deterministic canonical representation shared by Blender
   validation and the engine cooker.
4. Extend engine geometry and material semantics where the source requires
   data that the current model cache or `WorldScene` vertex layout omits.
5. Implement recovered Game Freak material families portably across OpenGL,
   Vulkan, and D3D12.
6. Integrate the promoted board-adjusted Route 1 layout.
7. Qualify structure, still-image appearance, motion, lighting, shadows,
   layout delta, and backend parity before replacing the procedural shell.

The final cooked cache encoding remains a downstream decision. Do not design
the importer around the limitations of the current `.pacmdl` version.

## Board Layout Rules

The board integration may:

- translate or locally rearrange route sections;
- clear only the space and visibility margin the board requires;
- reconnect ledges, paths, fences, vegetation, and collision;
- move props that prevent unit or board readability.

It must:

- preserve source scale and elevation language;
- preserve the original asset families and vegetation density around edited
  boundaries;
- preserve source materials and behaviors on moved objects;
- record each changed source transform or replacement transition in a
  layout-delta manifest;
- avoid accidental source omissions outside the declared edit region.

The board surface itself may remain game-owned. Its boundary treatment should
be derived from the source environment so that it feels embedded rather than
overlaid.

## Route 1 Implementation Sequence

Current progress:

- Steps 1 and 2 have a promoted direct-source manifest, a transparent
  canonical scene cook, full geometry and texture-subresource validation, and
  a compiled engine loader.
- Step 3 now has a compiled canonical-to-`WorldScene` adapter. It preserves
  source material/shader identities and bindings, keeps secondary vertex
  channels in a renderer-facing side stream, and honors the source
  `SkipMainRendering` switch when constructing the main-pass frame.
- The generated payload directory remains deliberately provisional; no final
  runtime cache format has been selected.
- Step 4 now covers `FieldGroundShader01`, `FieldCliffShader01`,
  `FieldGrassShader01`, `FieldGrassShader02`, `FieldGrassShader04`,
  `FieldGrassShader05`, `FieldTreeShader02`, `FieldTreeShader04`,
  `FieldTreeShader05`, and the tree-miki `FieldObjectShader` variant.
  The ground family consumes UV0, promoted GPU UV2, vertex color,
  `Alpha_light`, six authored sRGB roles, and the recovered blend order.
  The cliff family consumes UV0, newly promoted GPU UV1, UV2, vertex color,
  five authored sRGB roles, its recovered cliff/grass/border blend, and source
  rim constants. `FieldTreeShader02` consumes UV0, source vertex RGBA, five
  sampled roles, its two toon tables, cutout, tint, rim, and directional-light
  order. The two ordinary-grass families consume UV0 and UV1, source vertex
  color, six locally bound roles plus retained projected cloud/depth bindings,
  exact decoration/highlight/toon order, authored mip bias, and either rim or
  `OnGameColor`. Their recovered shared vertex program contains no local wind
  or billboard transform. The two small-grass families consume UV0 and UV1,
  source vertex color, their exact BNSH-remapped samplers, authored alpha
  cutout, layered atlas/light-line blends, toon, and `OnGameColor`. Their
  recovered vertex programs likewise contain no local wind or billboard
  transform. `FieldTreeShader04` shares byte-identical decoded
  fragment SPIR-V
  with the implemented tree-05 variant and explicitly supplies its
  `lightColor`. `FieldTreeShader05` consumes UV0 and UV1, six authored roles,
  alpha cutout, toon, rim, secondary-direction, and directional highlight
  operations recovered from decoded BNSH. Its otherwise omitted `lightColor`
  is now recovered directly from the synchronous Route 1 guest frame. The
  canonical 17,556/40,896/17,556 draw sequence proves distinct material
  uploads for `tree001` and `tree002`; the byte-identical `tree006` sibling
  retains its separately serialized source value. The trunk variant consumes
  UV0, UV1, vertex color, a dual-use bark/highlight texture, toon table, and
  source rim and shadow colors. It is deliberately material-specific because
  the route's other three `FieldObjectShader` materials select different
  switch variants.
  Authored BNTX mip chains survive the canonical adapter and upload unchanged
  on OpenGL, D3D12, and Vulkan. A fixed-camera `PAC_LgpeQualification` path
  renders the seven ground, nine cliff, 16 ordinary-grass, two small-grass,
  six foliage, six trunk, one flower, one grass-covered-rock, and one
  signboard Route 1 polygon groups together for review. The flower's exact
  0.85 alpha cutout and
  the rock's five-surface blend, rim, and decoded directional-light lookup are
  represented by modes 15 and 16. The first-ramp sign's exact
  `FieldObjectShader` program, source artwork, two toon tables, directional
  highlight, automatic-shadow window, rim, vertex color, and output order are
  represented by mode 17. Native shared projected
  cloud projection and the decoded native final-color equation are now
  represented without generic PBR substitution. The equation now runs once
  from a shared linear scene-color target on OpenGL, D3D12, and Vulkan.
  Exhaustive shader inventory of the protected Route 1 capture proves that no
  decoded fog variant is dispatched there, so Route 1 fog remains correctly
  disabled. The shared depth-shadow matrix, exact ten-tap projected PCF, and
  exact Route 1 tree global-light uploads are now capture-qualified.
- The source-centimetre scene is now registered in gameplay by
  `config/lgpe/route1_board_layout.json`. The manifest owns the only global
  source-to-world transform: centimetres convert at 0.01, the source anchor
  `[2200, 0, -1700]` maps to gameplay `[0, -0.04, 0]`, yaw remains zero, and
  every game-owned local change is declared against a stable source object ID
  and guarded by its original source transform. Supported target adapters now
  cover canonical source mesh groups, encounter-grass source records, and
  decoded BuildModel vegetation placements. The first layout-editing proof
  suppresses `flowers02` source record 27 where it intersects the board
  clearance boundary. The Route 1 open-road theme loads the canonical road,
  encounter grass, placed shrubs and flowers, and projected shadow atlas
  through indexed world batches. The game-owned board, units, VFX, and UI
  remain separate consumers rather than edits to the source environment.

### 1. Freeze the evidence baseline

- Record the promoted Blender checkpoint and validation reports.
- Record the original Route 1 GFBMDL, BNTX, auxiliary-file, and capture hashes.
- Define fixed review cameras, motion frames, and representative problem
  regions.

### 2. Direct source importer

- Resolve the Route 1 model and texture dependencies from the local unpack.
- Decode GFBMDL hierarchy, transforms, meshes, polygon groups, vertex streams,
  skeleton data, material metadata, and render flags.
- Decode BNTX images, formats, mip chains, swizzles, sampler state, and
  colorspace.
- Preserve unknown fields and unresolved auxiliary references.
- Validate against the existing extraction reports without using DAE as the
  canonical bridge.

### 3. Engine semantic expansion

- Add missing vertex channels such as UV1, additional color data, and `_WIND`.
- Add explicit environment material-family and render-state contracts.
- Represent projected lighting, alpha behavior, culling, shadow policy,
  animation, and wind without baking them to one camera.
- Keep the semantic layer independent of OpenGL, Vulkan, and D3D12.

Implemented evidence boundary:

- Route 1 declares UV1 on 27 meshes and UV2 on 12 meshes. It declares no UV3,
  secondary color, or `_WIND` vertex semantic in the decoded model, so the
  adapter does not invent one.
- Canonical UV1-UV3, color1-color3, normal-W, and bitangent values remain in a
  validated `WorldScene` source side stream. Source UV1 and UV2 are now also
  promoted to the compact cross-backend GPU vertex. `FieldCliffShader01`
  proves UV1's authored use for the cliff texture; both implemented families
  prove UV2's authored use for their grass/border transition atlas.
- Exact shader-group strings and all 127 source texture/sampler bindings are
  retained. The 21 materials classify into ground, grass, cliff, object, rock,
  tree, and shadow-only families.
- The two canonical road shadow-only materials remain registered but are
  excluded from the main-pass frame. A placed `grass02` companion material
  named `pasted__shadow` exposes contradictory source metadata
  (`FieldShadowOnlyShader`, `SkipMainRendering=false`, `CastShadow=true`).
  Shader family is authoritative for this case: it is retained as a caster and
  never submitted as an untextured color surface.
- No Route 1 material record remains on diagnostic preview rendering.
  `FieldGroundShader01`, `FieldCliffShader01`,
  `FieldGrassShader01`, `FieldGrassShader02`, `FieldGrassShader04`,
  `FieldGrassShader05`, `FieldTreeShader02`, `FieldTreeShader04`, and
  `FieldTreeShader05`, plus the tree-miki, roadstone, and flower
  `FieldObjectShader` variants, including `bm_signboard01_01`,
  `rockmask01_com`, and `rock01_com_grass01_com`, bind their required source
  roles explicitly. Every `FieldShadowOnlyShader` record remains registered
  for shadow work and is excluded from main color rendering.

### 4. Runtime material parity

- Recover BNSH bindings, constants, variants, and operations where practical.
- Use native captures to determine actual runtime resource values and state.
- Implement and validate one material family at a time.
- Start with a representative set covering lawn, ledge/fringe, tree foliage,
  ordinary ground grass, encounter grass, cliff, and a textured prop.

### 5. Board-adjusted scene integration

- Import the promoted Route 1 layout delta separately from source-fidelity
  corrections.
- Validate that undeclared areas remain source-equivalent.
- Test units, VFX, UI, camera framing, collision, and board interactions
  without using readability as a reason for unrelated environment redesign.

Implemented first-pass boundary:

- The committed board-layout manifest retains global registration and now
  supports four explicit imported-source target adapters:
  `buildmodel_vegetation_placement`, `encounter_grass_record`, and
  `canonical_mesh_group`, plus individual `canonical_tree_instance` targets.
  Each override records its stable logical name and
  record index plus the expected source transform, and is rejected if an
  incompatible recook changes the source record. The first proof suppresses
  `flowers02` record 27 for `autochess_board_clearance`; no canonical geometry
  or source placement manifest is modified.
- Phlosion Editor exposes 139 primary transformable Route 1 units in semantic,
  collapsible hierarchy folders: 32 non-tree canonical source mesh groups,
  all 47 topology-derived tree instances, six encounter-grass source records,
  and all 54 decoded vegetation placements. Terrain, ledge/platform, tree,
  encounter-grass, flower, ground-cover, prop, and source-layer groups are
  therefore reachable without a hard-coded `grass02`/flower filter.
- In paused `EDIT` mode, projected markers can be selected directly in the
  Scene viewport and manipulated with source-local Move, Rotate, and Scale
  gizmos. Pointer movement uses a lightweight instance/frame preview;
  projected shadows, material tables, statistics, manifest validation, and
  saving run once on release. Escape restores the pre-drag layout. Inspector
  fields use the same preview/commit path. The authoritative 8x8 board
  footprint and clearance boundary remain available as an overlay.
- Canonical groups remain an honest intermediate authoring boundary for route
  meshes that have not yet been decomposed. The six tree groups are no longer
  part of that limitation: connected trunk topology and matching contiguous
  material-stream blocks prove the exact 11/11/12/2/2/9 instance split. Each
  of the 47 trees retains its exact source canopy, trunk, shadow geometry,
  material assignment, stable source pivot, and source-transform guard. A
  legacy whole-family override remains compatible and appears separately only
  when a saved layout uses one.
- The generic Phlosion authored-scene schema version 1 owns project-created
  prefab instances, imported-source transform/suppression overrides, and
  persistent hierarchy metadata without modifying the imported source scene.
  Duplicate/Create Copy, Delete, Rename, and Hierarchy Folder operations now
  autosave atomically and share the same bounded Undo/Redo history as
  transforms. Deleting an imported object records suppression; deleting a
  created object removes its authored record. Hierarchy labels use natural
  numeric ordering (`1, 2, ... 10, 11`). Route 1 declares
  `scenes/route1.scene.json` through its project descriptor; its LGPE board
  manifest now owns global board registration only. Parametric ramp, ledge,
  and raised-platform tools are the next editing milestone.
- Route 1 gameplay loads the canonical base scene, both source encounter-grass
  models, all 164 accepted encounter modules, and all 54 source-decoded placed
  vegetation records. Declared suppression affects runtime visibility only;
  the source placement inventory remains intact. It preserves source draw
  order where alpha-composited environmental layers require it.
- Projected-cloud rows and the projected-depth shadow basis are transformed
  through the exact inverse board registration, preserving source-space
  lighting after the scene moves into gameplay world space.
- The old Route 1 environment model is not mixed with the canonical scene.
  When local decoded caches are unavailable, the game-owned board and route
  fill remain available, but the runtime does not invent replacement canonical
  vegetation.

### 6. Qualification and replacement

- Compare fixed stills and consecutive motion frames with native evidence.
- Run structural and provenance audits.
- Run OpenGL, Vulkan, and D3D12 parity captures.
- Measure load time, frame time, memory, draw count, and shader cost.
- Replace the procedural Route 1 shell only after the imported environment
  passes the agreed fidelity gates.

Current qualification:

- The complete test suite passes, including board-transform round trips,
  projected-cloud invariance, shadow-only main-pass exclusion, indexed
  submission ordering, and the compressed D3D12 world-material constant
  contract.
- OpenGL, Vulkan, and native D3D12 gameplay captures render the same canonical
  environment. All three renderer contract probes report signature
  `2d637fef00f62903`.
- `docs/lgpe/evidence/route1_gameplay_integration_report.json` records the
  manifest, source hashes, runtime boundary, and local visual proof.

## Later Routes

After Route 1 proves the full pipeline, reuse the importer, semantic material
families, validators, and board-layout process for:

1. Route 22;
2. Route 2;
3. Viridian Forest;
4. Route 3;
5. other selected LGPE environments.

Do not begin route-wide approximation work for later environments in place of
finishing the reusable direct-import and fidelity-validation path.

## Completion Criteria

An environment is complete when:

- original source relationships and hashes are recorded;
- decoded structure and source attributes pass validation;
- material, lighting, shadow, wind, and animation behavior are qualified;
- the only intentional content difference is the documented board-layout
  delta;
- all active rendering backends produce equivalent results;
- no procedural fallback is silently contributing to the qualified scene;
- regressions are guarded by reproducible tests and promoted captures.
