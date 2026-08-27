# LGPE Terrain Piece Workflow

Status: Active
Type: Architecture
Last updated: 2026-08-27

## Why the workflow changed

Route 1 proved that stretching one recovered straight ledge profile through
every straight, convex, concave, terminal, ramp, and map-boundary case is not a
production-safe authoring model. Runtime contour reconstruction remains useful
for genuinely new topology, but it cannot be the only source of truth when the
LGPE scene already contains a fitting authored carrier.

The durable workflow has two layers:

1. author with a complete catalog of exact imported source pieces;
2. freeze an approved region into one validated production mesh after the
   layout is stable.

The battle board remains tile-addressable. Environment presentation does not
become a collection of independently rendered square tiles.

## Complete source-piece catalog

`phlosion.tile-tools` derives the catalog from every occupied source terrain
cell exposed by the mounted environment. It does not contain a hardcoded
"straight plus one corner" kit.

Each catalog entry records:

- its immutable source coordinate;
- source elevation, surface, and flat/ramp profile;
- four edge sockets in south/east/north/west order;
- both endpoint heights on the owning and neighboring sides of each socket;
- neighbor occupancy and surface family;
- a topology classification such as straight ledge, corner, corridor,
  junction, island, tapered handoff, ramp, or map boundary;
- a deterministic socket signature.

Socket heights are normalized to the donor cell's base. A correct source piece
can therefore move to another elevation without changing its compatibility
contract.

The Inspector's **Exact Source Piece Catalog** ranks the complete donor set
against the selected cell. An exact match agrees on surface, shape, all four
neighbor occupancy states, all owner/neighbor endpoint heights, and adjoining
surface families. If no exact match exists, the carousel exposes every donor
best-first and says explicitly that it is a fallback; it never silently labels
the nearest approximation as exact.

**Apply Exact Donor To Selected Cell** persists a normal project-owned
`source_reference`. The Route adapter then clips and reuses the donor's
original LGPE positions, normals, tangents, UV sets, vertex colors, material
indices, and triangle winding. It does not copy a screenshot, manufacture a
flat green patch, or resample one generic ledge profile.

Adjacent donor references with one translation continue to union before
clipping. Multi-cell source structures can therefore be assembled by applying
the relevant donors and then copied as one existing terrain stamp.

## Authoring loop

1. Make the semantic terrain edit: board clearance, level, surface, or ramp.
2. Select a visibly difficult cell at a ledge turn, handoff, or boundary.
3. Open **Exact Source Piece Catalog**.
4. Prefer an exact socket match and apply it.
5. For a multi-cell source structure, apply its neighboring donor cells with a
   consistent source-to-target offset; copy/paste retains those references.
6. Use ordinary undo if a visually ranked fallback is not suitable.
7. Keep procedural contour output only where the source catalog contains no
   compatible authored topology.

This is a reusable location workflow. A future LGPE map supplies its own
occupied source cells and exact scene geometry; tile-tools supplies the same
catalog/ranking UI without inheriting Route 1 coordinates.

## Production bake boundary

The editor preview may compose immutable source geometry, exact donor
references, and generated transition geometry. A shipped location must not
depend on stacks of repair overlays.

Once an authored region is approved, the regional bake will:

1. resolve every source and donor placement into source-local geometry;
2. union donors that share one source-to-target transform;
3. clip source ownership once at the regional boundary;
4. preserve material partitions and every decoded vertex attribute;
5. weld only boundary vertices whose position and material socket contracts
   agree;
6. triangulate an unavoidable new transition only when the catalog has no
   exact carrier;
7. emit one project-owned regional terrain resource plus a provenance
   manifest listing every donor and generated transition;
8. validate the final combined mesh, not merely each helper strip.

The bake is rejected for any open unintended boundary, non-manifold edge,
degenerate or long sliver triangle, inconsistent winding, material/UV phase
jump, overlapping ownership, missing provenance record, or socket mismatch
that was not explicitly accepted as generated topology.

Runtime contour strips are therefore an authoring fallback and diagnostic, not
the final authority. The final runtime will load the promoted regional resource
directly.

## Current delivery boundary

Implemented now:

- complete occupied-source-cell catalog;
- deterministic topology and four-edge socket signatures;
- exact-match and best-first compatibility ranking;
- Inspector previous/next carousel;
- lossless exact-donor placement through existing scene persistence;
- immutable V2 transition cells and geometry-aware core qualification, so an
  adjacent metadata-only edit cannot replace an untouched source corner;
- Debug/Release package builds and regression tests;
- hidden D3D12 editor load proof.

Still required before production promotion:

- multi-cell assembly cards built from consistent donor translations;
- a non-destructive target-cell visual preview before autosave;
- offline regional mesh emission and final combined-mesh validators;
- visual golden fixtures for unedited source, exact donor, mixed donor, and
  genuinely generated transitions.

Those are deliberate next layers on the same catalog. They do not require more
coordinate-specific repair-strip tuning.
