# LGPE Environment Fidelity Contract

Status: Active
Type: Contract
Last updated: 2026-07-27

## Plain-Language Goal

Render Pokemon: Let's Go, Pikachu! environments in PokemonAutochess while
preserving as much of their original appearance and behavior as technically
possible.

The only intended environment-content change is the smallest layout adjustment
needed to make room for the autochess board. Terrain, vegetation, materials,
textures, lighting response, shadows, animation, wind, placement language, and
other environmental behavior should otherwise remain as faithful to the
original game files and captured game behavior as possible.

This is a reconstruction and compatibility project, not a visual redesign.

## Scope

This contract applies to LGPE-derived environments, beginning with Route 1. It
governs:

- source-file ingestion and provenance;
- Blender reconstruction and review scenes;
- engine-side asset cooking and loading;
- environment shaders and material families;
- lighting, shadows, wind, animation, and other runtime behavior;
- collision and other environment metadata when it affects presentation or
  gameplay integration;
- layout edits made to accommodate the autochess board;
- OpenGL, Vulkan, and D3D12 output.

PokemonAutochess gameplay, units, the board, UI, and VFX remain game-owned.
They may be visually integrated with the environment, but they do not authorize
unrelated changes to the environment.

## Fidelity Authority

When evidence disagrees or an interpretation is uncertain, use this priority
order:

1. Original files from the user's own LGPE dump, including GFBMDL, BNTX,
   GFBANM, GFBCOL, BNSH, GFPAK relationships, and identified auxiliary data.
2. Native runtime evidence, including RenderDoc captures, consecutive-frame
   captures, resource bindings, constants, render state, and observed motion.
3. Deterministically decoded source streams and metadata with recorded source
   hashes.
4. The current promoted Blender checkpoint and its validation reports.
5. Aligned gameplay images or video when the relevant behavior is not exposed
   by the available files or captures.
6. A bounded hand-authored reconstruction only when stronger evidence is
   unavailable.

Lower-priority evidence must not silently override higher-priority evidence.
Every inferred or hand-authored behavior must be labeled as such and kept
replaceable when better evidence becomes available.

## Permitted Layout Change

The environment may be adjusted only enough to provide a readable and playable
autochess board and its required interaction margin.

Permitted operations include:

- translating or locally rearranging existing environment sections;
- opening a bounded amount of space around the board;
- reconnecting terrain, paths, fences, ledges, vegetation, and collision after
  that displacement;
- adding transition geometry strictly where required to make the edited layout
  continuous;
- moving occluding props when required for board or unit readability.

The layout edit must preserve the source route's scale, asset families,
density, layering, material response, elevation language, and visual rhythm.
It must not become a new Route 1-inspired composition.

Every intentional layout deviation must be recorded in a machine-readable
layout-delta manifest. Anything not present in that manifest is expected to
match the selected source region or an explicitly documented reconstruction.

## Non-Permitted Shortcuts

Do not:

- replace source vegetation, terrain, or props with generic substitutes merely
  because they are easier to render;
- flatten material families into generic PBR when source behavior is known;
- change the palette, lighting, density, silhouettes, or wind as art direction;
- omit a source layer because its mask, ordering, shader, or placement is
  difficult to interpret;
- treat the current procedural route shell as fidelity evidence;
- bake camera-facing or animated behavior into one static view when it can be
  implemented at runtime;
- declare parity from one attractive screenshot;
- make a runtime format choice determine what source data is preserved.

Performance optimization is allowed only when the resulting presentation stays
within the validated fidelity envelope.

## Source-Ingestion Architecture

PokemonAutochess should own a reproducible offline LGPE importer. The importer
is part of the engine toolchain, but proprietary source parsing does not belong
in the shipping frame loop.

The intended flow is:

```text
user-supplied RomFS / unpacked GFPAK
        |
        v
offline LGPE importer
        |
        v
lossless canonical decoded representation
        +-------------------+
        |                   |
        v                   v
Blender reconstruction     engine asset cooker
and authored patch meshes  and runtime resources
        |                   |
        +--------- validation
```

The canonical decoded representation must preserve recognized source data and
retain unknown fields or raw sections when practical. It must not discard data
merely because the first renderer version does not consume it.

The importer must eventually cover:

- GFPAK archive relationships and hash resolution;
- GFBMDL scene hierarchy, geometry, polygon groups, all vertex streams,
  skeletons, material assignments, parameters, and render metadata;
- BNTX textures, formats, swizzles, mip chains, sampler state, and colorspace;
- GFBANM animation data;
- GFBCOL collision and footprint data;
- BNSH program metadata, resource bindings, variants, and recoverable
  semantics;
- identification and preservation of relevant auxiliary binary files.

The original source dump remains user-supplied local input and must not be
committed to or distributed with this repository.

## Runtime Interpretation

Direct source interpretation means that the asset pipeline reads Game Freak's
files without relying on a lossy DAE or screenshot reconstruction. It does not
mean that the shipping renderer must parse GFPAK, GFBMDL, or BNTX every time the
game starts.

The offline importer should translate source data into explicit engine
semantics. The runtime should consume validated cooked resources through the
existing asset-store and `WorldScene` architecture.

BNSH programs require special treatment. They target the Nintendo Switch GPU
environment and cannot be executed unchanged across OpenGL, Vulkan, and D3D12.
Their bindings, constants, variants, and observed operations should inform
portable engine material-family implementations. Native captures remain the
behavioral oracle for those implementations.

The final cache or package encoding is deliberately not fixed by this
contract. Source fidelity, deterministic conversion, and renderer behavior
must be proven before a durable runtime schema is frozen.

## Blender's Role

Blender remains the editable reconstruction, project-owned patch-mesh, and
visual-review environment. Source-backed placement transforms and visibility
overrides are authored in Phlosion Editor's non-destructive layout layer so
they retain stable source IDs and transform guards. Blender is not allowed to
become an undocumented source of replacements for data already available in
the original files.

The promoted Blender checkpoint must:

- record its exact parent and source hashes;
- preserve decoded source attributes required at runtime;
- distinguish preview-only animation or shader proxies from runtime contracts;
- keep board-driven layout edits separate from fidelity corrections;
- remain reproducible through versioned scripts and validators;
- retain accepted restore points before experimental interpretation changes.

## Validation Gates

An environment is not fidelity-qualified until it passes all applicable gates:

1. **Provenance:** every imported resource records source path, type, hash,
   decoder version, and any unresolved dependency.
2. **Structural:** scene counts, hierarchy, polygon groups, transforms, UVs,
   colors, bones, weights, materials, textures, and animations match decoded
   source data.
3. **Material:** texture roles, colorspace, alpha behavior, culling, layering,
   projected effects, and material parameters are validated per source family.
4. **Motion:** wind and animation are compared over consecutive frames, not
   inferred from one still.
5. **Lighting and shadow:** fixed-camera comparisons cover representative
   times, depths, vegetation densities, and ledge conditions.
6. **Layout delta:** every board-driven displacement is intentional, bounded,
   recorded, and free of accidental omissions or overlaps.
7. **Backend parity:** OpenGL, Vulkan, and D3D12 render the same qualified
   environment semantics within an explicit tolerance.
8. **Regression:** promoted fixed-camera and motion-sequence checks prevent
   accepted behavior from being silently lost.

Validation reports must distinguish:

- exact source-backed behavior;
- capture-backed runtime reconstruction;
- evidence-constrained approximation;
- unresolved behavior;
- intentional board-layout deviation.

## First Implementation Slice

Route 1 is the proving ground. The first direct-import slice must decode the
original Route 1 GFBMDL and its BNTX dependencies into a canonical
representation without using DAE as the data bridge.

The slice must prove preservation of:

- scene hierarchy and transforms;
- every mesh and polygon group;
- positions, normals, tangents where present, both UV streams, both color
  streams where present, bone IDs, and bone weights;
- material names, shader-family names, parameters, samplers, and render flags;
- original texture format, dimensions, mip count, colorspace, and source
  container identity;
- source hashes and unresolved auxiliary references.

Its output must be compared against the existing Route 1 extraction reports
and promoted Blender checkpoint before engine rendering work builds on it.

## Decision Rule

When fidelity, convenience, and implementation speed conflict, preserve the
source evidence first. A slower or incomplete importer can be improved; data
discarded early in the pipeline cannot be recovered reliably downstream.
