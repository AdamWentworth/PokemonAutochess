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
| `cover_regions` | Stable grass-node identity and a union of transformed grass-clump polygons in the source X/Z plane. |
| `scene_content_sha256`, `board_content_sha256` | Hashes of canonical JSON content, independent of file whitespace and line endings. |

Cells are 100 cm wide and height levels are 50 cm. Surface values are lawn=0,
dirt=1, dark lawn=2. Ramp values are flat=0, north=1, east=2, south=3, west=4.
North is decreasing source Z, or increasing Blender Y. The cell-height function
interpolates the ramp continuously; shared-edge height differences distinguish a
connected ramp from an abrupt ledge at the same nominal elevation.

Playability currently follows the existing 8x8 board. It does not follow lawn
colour, prop bounds, or all connected visual terrain. Changing the playable area
requires an explicit board/gameplay decision, not an art-only terrain edit.

Encounter regions use `rendered_clump_footprints`: nominal 100 cm squares around
the same clump centres used by rendering, including its dense half-cell border
ring. Core indices are cell corners; clump centres include the +50 cm offset.
The authored prefab position, scale and yaw transform the complete footprint,
preserving hooked shapes. `EncounterGrassFootprint.h` supplies shared centres
for rendering and archive validation; the Python exporter mirrors that rule.
Disabled grass props produce no region. The simulation uses these regions for
sight, targeting and camouflage, as described in `ENCOUNTER_GRASS.md`.

## Runtime boundary

The schema describes geometry independently of movement policy.
`AuthoredCombatMap` now enables ground walking across equal shared-edge heights,
including connected ramps, and cardinal jumps across full, level south-facing
drops. Other discontinuities are walls. A negative height delta alone does not
permit jumping in other directions or across an uneven ramp side. Configured
flyers bypass height restrictions while respecting occupancy. See
`COMBAT_MOVEMENT.md` for animation and targeting behavior. `AuthoredCombatMap` also connects the published cover polygons and
evaluates continuous-position concealment; see `ENCOUNTER_GRASS.md`.

Rendered standing height comes from `AuthoredGroundSurface`, which samples the
actual authored floor, including rounded caps and ramps. Logical adjacency comes
from the map data. This separation allows cosmetic mesh refinements to preserve
gameplay meaning without putting source-mesh repair rules into pathfinding.

The asset-independent exporter/validator is
`tools/environment/blender/arena_map.py`. Validation rejects duplicate cells,
missing board/reserve coverage, out-of-range tile attributes, stale scene or board
hashes, and inconsistent derived connections/regions. The export installer treats
the complete arena as one PHSC archive, described below.

## Atomic runtime archive

`content/phlosion/environment/arena-pilot/arena.phscene` is the active arena.
It contains the authored scene, cooked terrain, map, board registration,
composition metadata, and authoring recipe. The engine's PHSC codec checks file
and archive integrity. JSON is normalized before encoding, so indentation and
checkout line endings do not change a revision. `AuthoredArenaBundle` validates identity, bounded cell and
polygon data, exact directed edges, board/reserve registration, visible grass
footprints, and nine interior floor-height probes per tile (0.12 cm tolerance).
Cosmetic rounded edges may differ between probes; this is not a proof of every
triangle or surface material. Floor paint does not decide gameplay access.

Both runtime and editor use `ArenaSceneActivation`. They require the complete
archive for a bundled scene; loose files never substitute for absent entries.
Scene/terrain/map files remain authoring and review mirrors. Editing those mirrors
alone does not change the active arena. Editor scenery/registration mutations are
disabled for bundled arenas; unit starting positions remain editable.

`publish_arena.py` checks recipe, saved-source and export SHA-256 hashes, verifies
staged copies again, cooks and qualifies a candidate, then atomically replaces the
active archive. OS-owned writer locks release after process death. An interruption
before replacement leaves the previous archive active; repeating publication
repairs any partly updated review mirrors. It never activates a mixture of files.
Backups are versioned by archive and Blender-source hashes; `latest-source.json`
points to the verified restore source. A failed configured depot backup prevents
local activation. Publication without a configured depot is local-only.

The shared gameplay boundary is `game::arena::CombatMapRules`, carried by
`GameWorld::combatMap()`. Planning, perception and melee queries consume it.
Null rules preserve flat traversal and full visibility for other scenes.
Diagonals must satisfy both directed cardinal walking routes and the two
occupancy checks; ledge drops require a cardinal step. Flight animation
(`usesAirLocomotion`) is separate from `TraversalCapabilities`. The bundled
arena's policy is activated before simulation, independently of the renderer.
