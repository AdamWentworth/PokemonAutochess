# Terrain Patch Cooker V2

## Purpose

The battle board remains a strict integer grid because placement, occupancy,
bench registration, and combat rules depend on cells. The surrounding Route 1
environment does not need to expose those cells as render-mesh boundaries.

Terrain Patch V2 therefore treats cells as authoring and semantic metadata,
then cooks every connected edit into a regional surface. This is the boundary
between gameplay topology and environment presentation:

- gameplay board: persistent, tile-addressable data;
- environment edits: cell-addressed selection input;
- rendered environment: connected regional geometry with shared boundaries;
- untouched environment: exact imported source geometry.

## Regional cook contract

`Route1TerrainPatchCooker` derives a deterministic plan from the active terrain
graph without modifying the authored scene.

1. Every authored or materially rebuilt cell becomes a core edit cell.
2. The core grows through one Chebyshev cell of existing source terrain. This
   source transition ring is where material fields can rejoin the imported
   environment without a hard tile delimiter.
3. Overlapping rings merge before connected components are assigned. Nearby
   edits therefore cannot build competing seam repairs.
4. Each region receives a closed core-to-transition contour and a closed
   patch-to-source contour. Interior holes remain explicit closed contours.
5. Boundary topology is validated before any replacement geometry is allowed
   to render.

The preview replaces source ground only inside a valid region. It does not
persist new scene nodes and does not expand destructive cliff/foliage cleanup
into the transition ring.

## Current implementation status

Implemented:

- deterministic connected-region discovery;
- one-cell source transition rings;
- closed inner/outer boundary chaining, including holes;
- topology validation and runtime statistics;
- regional continuous-material ownership;
- shared vertex indices across compatible same-surface/same-height cell edges;
- exact decoded geometry/material fields on source-transition cells instead of
  moving a synthetic square delimiter one cell away from the authored edit;
- source-profile straight and convex-corner crown gaskets that follow the
  resolved ledge contour and are bounded to a narrow local envelope;
- retirement of the legacy rectangular crown/corner safety meshes while the
  V2 preview is active;
- editor-only V2 preview, enabled by default for Route 1;
- instant A/B fallback through **Terrain Patch V2 Preview** in the project
  commands;
- production runtime remains on the existing cook unless explicitly enabled.

Still to complete before promotion:

- drive the remaining cliff and foliage ownership directly from the regional
  contour instead of the legacy ledge resolver;
- add a geometric intersection/non-manifold validator after final
  triangulation;
- establish visual golden crops for source corners, authored convex/concave
  corners, light lawn, dark lawn, dirt transitions, and map boundaries;
- remove superseded per-tile repair helpers only after the V2 golden set passes.

## Promotion rule

V2 does not become the shipped Route 1 terrain cook merely because it reduces
one seam. Promotion requires all topology validators to pass and the visual
fixture set to equal or improve the imported source at unchanged boundaries.
Until then the preview switch is the rollback mechanism and authored Route 1
data remains format-compatible with both paths.
