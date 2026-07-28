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
  uses a named, isolated value from a Route 1 sibling with byte-identical
  decoded fragment SPIR-V; that value remains explicitly bounded rather than
  claimed as a captured runtime upload. The trunk variant consumes UV0, UV1,
  vertex color, a dual-use bark/highlight texture, toon table, and source rim
  and shadow colors. It is deliberately material-specific because the route's
  other three `FieldObjectShader` materials select different switch variants.
  Authored BNTX mip chains survive the canonical adapter and upload unchanged
  on OpenGL, D3D12, and Vulkan. A fixed-camera `PAC_LgpeQualification` path
  renders the seven ground, nine cliff, 16 ordinary-grass, two small-grass,
  six foliage, and six trunk Route 1 polygon groups together for review. Native
  shared projected cloud/shadow/fog/post remain open and are not replaced with
  generic PBR.
- The live backdrop remains on the promoted Blender checkpoint until this
  family and the remaining families are capture-qualified.

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
- Both authored shadow-only materials set `SkipMainRendering`; their six
  polygon groups remain registered but are excluded from the main-pass frame.
- A single source-role texture remains only a diagnostic preview for
  uninterpreted families. `FieldGroundShader01`, `FieldCliffShader01`,
  `FieldGrassShader01`, `FieldGrassShader02`, `FieldGrassShader04`,
  `FieldGrassShader05`, `FieldTreeShader02`, `FieldTreeShader04`, and
  `FieldTreeShader05`, plus the tree-miki and roadstone `FieldObjectShader`
  variants and `rockmask01_com`, no longer use that fallback: their required
  source roles are bound explicitly. Three material records remain on the
  diagnostic fallback.

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

### 6. Qualification and replacement

- Compare fixed stills and consecutive motion frames with native evidence.
- Run structural and provenance audits.
- Run OpenGL, Vulkan, and D3D12 parity captures.
- Measure load time, frame time, memory, draw count, and shader cost.
- Replace the procedural Route 1 shell only after the imported environment
  passes the agreed fidelity gates.

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
