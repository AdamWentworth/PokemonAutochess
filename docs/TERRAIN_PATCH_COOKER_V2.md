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

## Authoring versus render topology

The editor deliberately keeps the fast grid workflow: select cells, change
height/surface/shape, and save. A successful edit reload automatically reruns
the seam resolver, regional planner, ledge resolver, and contour mesher. Map
authors do not place seam patches or choose a corner mesh by hand.

`TerrainContourMesher` is the reusable geometry boundary between an imported
location profile and the renderer. It triangulates sampled contour strips and
the bounded curved pocket behind a convex ledge foot, validates indices,
degenerate faces, and winding, and has no Route 1 or LGPE material dependency.
The Route 1 adapter supplies the recovered LGPE cross-section and material
samples. A later LGPE location can therefore reuse the topology cooker while
providing its own decoded source field; it does not inherit Route 1 cell IDs or
one-off coordinates.

`Route1TerrainContourAssembler` is the LGPE adapter above that generic
triangulator. The ledge resolver supplies semantic edges and join kinds once;
the assembler turns them into one cached sequence of world-space position,
outward, tangent, logical-distance, and material-distance frames. Crown,
cliff, foliage, and low-foot batches now consume those same frames. Compatible
flat-drop spans and convex turns are welded into one renderable contour run;
the crown, cliff wall, and leafy fringe are emitted once per run rather than
once per tile edge and corner. A rebuilt V2 ledge may still sample the source
material fields, but it no longer lets each material batch independently snap
its geometry back to a nearby source triangle. Untouched source terrain
continues to use the exact imported mesh.

A convex turn has one seam contract across all of its carriers:

- adjoining straight crown strips end at a positive inner radius;
- the rounded crown uses those exact two endpoint rows and closes only its
  small interior sector;
- the cliff and foliage use the same turn centre and source-scale radius;
- the low lawn owns one curved triangular foot pocket extending underneath
  the wall, rather than several overlapping square safety meshes.

This prevents the two recurring failure modes from being traded back and
forth: collapsing an inner row into a long green spike, and removing the
low-side owner to expose a backdrop-coloured triangle.

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
- one reusable contour triangulator with deterministic winding/degeneracy
  validation;
- positive-radius straight-to-corner crown handoffs, with no collapsed strip
  vertices;
- contour-owned convex low-foot pockets on both mirrored Route 1 corners;
- one validated frame owner for straight and convex lawn, wall, foliage, and
  foot carriers, including exact endpoint closure at every profile offset;
- contour-level crown, cliff, and fringe objects with welded straight-to-turn
  indices and continuous material distance across the complete run;
- one decoded-source phase offset per edited fringe contour, avoiding the
  stretched alpha-cutout leaves produced by independently blended endpoints;
- material-only source sampling on edited V2 ledges, preventing independent
  source-geometry projections from reopening otherwise matched joins;
- the same recovered LGPE ledge-contact tint and crown normal on the contour
  safety carrier as on the adjoining high lawn, so light-lawn regions no
  longer expose a bright synthetic ribbon beneath alpha-cut leaves;
- mirrored west/east runtime regression coverage, rather than qualifying only
  the first corner that happened to be inspected;
- retirement of the legacy rectangular crown/corner safety meshes and
  source-handoff overlays while the V2 preview is active;
- rejection of projected top-surface slivers that exceed the regional five-
  centimetre lattice, and retirement of tile-local triangles that straddle a
  contour-owned convex rim;
- editor-only V2 preview, enabled by default for Route 1;
- instant A/B fallback through **Terrain Patch V2 Preview** in the project
  commands;
- production runtime remains on the existing cook unless explicitly enabled.

Still to complete before promotion:

- migrate source-specific concave and ramp handoffs onto the shared contour
  frame after their decoded asymmetric profiles receive equivalent fixtures;
- extend the contour validator from its generated carriers to the final
  combined source/patch mesh (intersection and non-manifold checks);
- establish visual golden crops for source corners, authored convex/concave
  corners, light lawn, dark lawn, dirt transitions, and map boundaries;
- remove superseded per-tile repair helpers only after the V2 golden set passes.

## Promotion rule

V2 does not become the shipped Route 1 terrain cook merely because it reduces
one seam. Promotion requires all topology validators to pass and the visual
fixture set to equal or improve the imported source at unchanged boundaries.
Until then the preview switch is the rollback mechanism and authored Route 1
data remains format-compatible with both paths.
