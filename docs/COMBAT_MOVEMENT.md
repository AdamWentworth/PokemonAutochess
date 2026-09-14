# Combat movement

Status: Active
Type: Contract
Last updated: 2026-09-13

Normal games use the Blender-authored flat dirt arena
(`routes/route1-flat-experiment`). Its Crowded Battle scenario exercises movement
and combat. The South Entrance (`routes/route1-pilot`) retains terrain, encounter
grass and ledge scenarios for those mechanics.

`MovementSystem` owns movement timing and reservations; `game/arena/CombatMap`
owns grid planning and the traversal/perception/melee policy interface. Each step reserves its origin
and destination until arrival. Diagonal steps also reserve their two adjoining
cells, and A* cannot cut across an occupied or reserved corner. Reservations for
existing moves are established before any idle unit plans another step.

`game/systems/UnitFacing.h` owns horizontal facing for native movement and script
commands. Walking, flying and jumping face the current travel segment, including
detours away from an enemy. Combat's target-facing commands cannot override that
direction. The segment remains stable at arrival and throughout ledge landing;
continuous locomotion between steps keeps its last direction until the next step
is planned. Once locomotion stops, a unit may turn toward a visible combat target.
Pure vertical or zero-length directions preserve yaw. Placement and round-reset
orientations remain separate from traversal.

The movement collision regressions exercise all eight directions, queued commits,
combat-facing overrides, airborne movement, stationary targeting, and crowded
turns. The ledge test also checks facing through takeoff, flight and landing.
Native and editor matrix cases `movement-facing-approach`, `movement-facing-turns`
and `movement-facing-ledge` qualify the same phases on OpenGL, Vulkan and D3D12.
`movement-crowded-meeting` additionally checks the crowded formation after it
reaches combat range. The frame-72 case now expects continued forward progress
where the former target-selection bug caused unnecessary turns.

```powershell
./tools/render_parity_matrix.ps1 -Config RelWithDebInfo -Cases movement-facing-approach,movement-facing-turns,movement-facing-ledge,movement-crowded-meeting -OutputDir debug/crowded-pathing/native
./tools/housekeeping/check_editor_workflow.ps1 -Cases movement-facing-approach,movement-facing-turns,movement-facing-ledge,movement-crowded-meeting -OutputDirectory debug/crowded-pathing/editor
```

Idle units compete in order of distance to the nearest terrain-reachable enemy, movement speed,
then stable unit ID. Equally near enemies are selected by stable ID as well.
Target selection measures squared straight-line distance between grid cells
(`dx*dx + dz*dz`), also used by script nearest-enemy queries. Counting only the
larger axis made distant diagonal enemies tie with the opponent directly ahead,
funneling a whole formation toward its lowest-ID opponent. Adjacent melee still
includes diagonals and follows the map's melee permissions. The crowded-row
regression checks direct initial targets, no unnecessary retreat, and timely
engagement at equal and mixed speeds, at 120 Hz, 30 Hz and 5 Hz.
A unit routes around a reserved corridor when possible. If every attack position
is occupied, it can approach a closer reachable cell and queue there; it waits
when no closer position is available. Arrival releases the completed step for
the next planner update. Units with zero speed do not claim new destinations.

When a visible opponent is already moving, pursuit aims beside its reserved
endpoint, including reservations made earlier in the same update. A unit holds
its position when that opponent is already approaching a legal melee meeting
point. This prevents both units treating the approach corridor as a stationary
obstacle and making a sidestep or return trip toward the opponent's old tile.
The complete corridor remains reserved. Visibility is checked at the opponent's
actual position; predicted grass membership never reveals or hides it early.
Attack range and cliff restrictions still use actual combat positions.

Authored takeoff animations retain their reservations while the flyer is still
on the ground. Configured flyers bypass height barriers but follow the same
occupancy and corridor rules. A model having generic jump/landing clips does not
grant flight; species configuration or explicit airborne metadata supplies that
capability. `ENCOUNTER_GRASS.md` defines concealment, attack reveals and the
last-seen investigations and terrain-legal search patrol used when no visible,
reachable target remains.

## Combat targeting

New engagements choose the nearest visible, attackable enemy using the same
squared grid distance as movement. HP is not a target priority; genuine distance
ties use stable unit ID. Cardinal neighbors therefore take priority over diagonal
neighbors when acquiring a target in aligned rows.

Native and Lua combat retain that focus between attack cycles while the enemy
remains alive, hostile, visible, and within terrain-legal melee range. A closer or
weaker newcomer does not steal it. `ScriptAPI::canEngageEnemy` supplies the shared
validity query. Units do not acquire combat targets or start attacks during
locomotion. An existing attack cycle keeps its animation target; the next attack
must reacquire if the old enemy fainted, entered capture, disappeared, changed
teams, moved out of range, or became concealed or terrain-inaccessible.

`combat_targeting_headless` exercises both drivers: aligned 5v5 formations with
unequal HP and reversed storage order, moving approaches, retained focus across
attacks, all seven invalidation cases, and waiting without an eligible target.
The `movement-crowded-meeting` and `combat-target-focus` native/editor matrix cases
check initial and repeated attack cycles on all three rendering APIs.

```powershell
ctest --test-dir build -C RelWithDebInfo -R combat_targeting_headless --output-on-failure
./tools/render_parity_matrix.ps1 -Config RelWithDebInfo -Cases movement-crowded-meeting,combat-target-focus -OutputDir debug/combat-targeting/native
./tools/housekeeping/check_editor_workflow.ps1 -Cases movement-crowded-meeting,combat-target-focus -OutputDirectory debug/combat-targeting/editor
```

## Ground ledges and ramps

`AuthoredCombatMap` activates from the validated arena archive before the first
combat tick, including headless play. Equal shared-edge heights permit walking
and continuous ramps in both directions. Full, level south-facing drops permit
cardinal jumps. Uphill approaches and the other cliff faces are walls. Ramp side
walls and diagonal ledge shortcuts are blocked. Terrain colour is not an access
rule. Ground melee cannot reach through a cliff; movement can choose another
reachable opponent or use a ramp to reach an upper shelf. Visibility queries
still report visible opponents even when no route exists.

The South Entrance test board has five drop edges, columns 3–7 from row 1 to row 2 (zero
based), and three ramp columns on the west side. The 8×8 footprint is unchanged.

`LedgeJump` owns start, airborne, and landing phases within the committed move.
Start plays in place; airborne loops while a game-owned arc crosses to the lower
cell; landing plays in place before releasing the origin/landing reservation.
The jumper cannot issue a new attack until landing recovery finishes. This does
not grant damage immunity. Competing units must queue or route around the full
corridor, including while the landing animation is playing.

All three rendering APIs and headless play use manifest phase durations. Newer model
imports resolve `jumpdown01_start`, `jumpdown01_loop`, and `land02`; LGPE-style
imports resolve `landA`, `landB`, and `landC`. Optional `jump_start`, `jump_loop`,
and `jump_land` roles override these choices. Missing clips use a stationary
fallback with bounded start/landing delays. Airborne time follows movement speed.
Runtime root displacement is suppressed during jumps so it does not duplicate
the game-owned trajectory; the source clips remain unchanged.

Editor reload snapshots save a jumping unit at its upper endpoint, or at its
lower endpoint during landing. Reload restarts its movement decision from that
safe cell. Repositioning and round recovery clear the jump state. Faint/capture
interruptions keep the committed corridor according to the existing blocking
policy until the unit is removed or reset.

Above the editor viewport, choose **Route 1 - South Entrance**, then
**Scenario / starting setup > Ledge Test**, and press **Play** to replay
Bulbasaur and Rattata descending toward Charmander. Its starting positions are
data in `config/debug/editor_route1_pilot_ledges.json`. The same preview launches
with `tools/environment/preview_route1_pilot.ps1 -Phase ledges`.

Verification:

`ledge_jump_rendering` sends the real Bulbasaur and Rattata meshes through the
public unit renderer. It checks skeletal draw submissions throughout the jump
and changing bone palettes during running, takeoff, and landing. Airborne loops
may hold an authored pose. GPU skinning uses the already evaluated in-place
palette; disabling it is not a root-motion fix.

```powershell
.\build\Release\PAC_Tests.exe --filter movement_collision_regressions
.\build\Release\PAC_Tests.exe --filter ledge_jump_movement
.\build\Release\PAC_Tests.exe --filter ledge_jump_asset_roles
.\build\Release\PAC_Tests.exe --filter ledge_jump_rendering
.\build\Release\PAC_Tests.exe --filter movement_invariants
.\build\Release\PAC_Tests.exe --filter end_to_end_headless
.\build\Release\PAC_Tests.exe --filter route1_arena_pilot_contract
```

The collision regressions cover stolen destinations, premature origin release,
blocked diagonal corners, a narrow queue, and 16-unit approaches at 120 Hz,
30 Hz and 5 Hz. They measure separation throughout each frame's movement and
verify that reversing unit storage order preserves the resulting paths.

Capture and blocking faint presentations retain an interrupted move's corridor.
Removing the unit releases the reservation on the next update; reservations are
rebuilt from current units each tick. Regression tests cover these lifecycle
transitions and policy agreement between MovementSystem and ScriptAPI.

`PAC_ArenaLogicTests` links only the pure arena library, GLM and JSON. Its directed
corner, capability, visibility, data reload and seeded-navigation cases run without
private models or graphics services. `ctest --test-dir build -C Release -L fast`
also runs the Python map and interrupted-publication contracts.
