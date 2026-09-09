# Authored arena map contract

Status: Active
Type: Contract
Last updated: 2026-09-08

The saved Blender tile blueprint and prefab placements own authored environment
data. Export creates a separate gameplay map alongside the scene and terrain.
The game never infers camouflage from material colour or navigation from the
triangles used for decorative rocks, fringes, and rounded walls.

## Schema 1

`config/environment/route1_pilot_gameplay.json` uses kind
`pokemon_autochess_arena_map` and source centimetres with Y up. Its fields are:

| Field | Meaning |
| --- | --- |
| `cells` | Saved integer X/Z coordinates, height level, surface, and ramp. |
| `playable_cells` | Explicit board footprint from board registration. |
| `reserve_cells` | Explicit bench footprint; excluded from combat connections. |
| `connections` | Directed cardinal adjacency with the two shared-edge endpoint height differences, destination minus origin, in centimetres. |
| `cover_regions` | Stable grass-node identity and a union of transformed core-cell polygons in the source X/Z plane. |
| `scene_content_sha256`, `board_content_sha256` | Hashes of canonical JSON content, independent of file whitespace and line endings. |

Cells are 100 cm wide and height levels are 50 cm. Surface values are lawn=0,
dirt=1, dark lawn=2. Ramp values are flat=0, north=1, east=2, south=3, west=4.
North is decreasing source Z, or increasing Blender Y. The cell-height function
interpolates the ramp continuously; shared-edge height differences distinguish a
connected ramp from an abrupt ledge at the same nominal elevation.

Playability currently follows the existing 8x8 board. It does not follow lawn
colour, prop bounds, or all connected visual terrain. Changing the playable area
requires an explicit board/gameplay decision, not an art-only terrain edit.

Encounter regions use the published grass core cells, transformed by the authored
prefab's position, scale, and yaw. They preserve hooked shapes and omit the outer
decorative blade ring. Disabled grass props produce no region. The simulation can
later query these regions consistently for sight, targeting, and camouflage.

## Runtime boundary

This schema describes the map; it does not enable new movement or visibility
rules. In particular, a negative height difference is not yet permission to jump,
and a positive difference is not yet an uphill block. MovementSystem retains its
current occupancy/reservation behavior. The next mechanic must decide allowed
transitions for ground units and flyers and test both directions explicitly.

Rendered standing height comes from `AuthoredGroundSurface`, which samples the
actual authored floor, including rounded caps and ramps. Logical adjacency comes
from the map data. This separation allows cosmetic mesh refinements to preserve
gameplay meaning without putting source-mesh repair rules into pathfinding.

The asset-independent exporter/validator is
`tools/environment/blender/arena_map.py`. Validation rejects duplicate cells,
missing board/reserve coverage, out-of-range tile attributes, stale scene or board
hashes, and inconsistent derived connections/regions. The export installer treats
scene, terrain, and gameplay map as one unit and rolls all three back after a
failed native scene validation.
