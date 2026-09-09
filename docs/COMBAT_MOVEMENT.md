# Combat movement

Status: Active
Type: Contract
Last updated: 2026-09-09

The current gameplay test map is the Blender-authored Route 1 south entrance
(`routes/route1-pilot`, art pass 10). Open its Planning or Battle preset in
Phlosion Editor. The south clearing is deferred.

`MovementSystem` owns movement timing and reservations; `game/arena/CombatMap`
owns grid planning and the traversal/perception/melee policy interface. Each step reserves its origin
and destination until arrival. Diagonal steps also reserve their two adjoining
cells, and A* cannot cut across an occupied or reserved corner. Reservations for
existing moves are established before any idle unit plans another step.

Idle units compete in order of distance to the nearest terrain-reachable enemy, movement speed,
then stable unit ID. Equally near enemies are selected by stable ID as well.
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

## Ground ledges and ramps

`AuthoredCombatMap` activates from the validated arena archive before the first
combat tick, including headless play. Equal shared-edge heights permit walking
and continuous ramps in both directions. Full, level south-facing drops permit
cardinal jumps. Uphill approaches and the other cliff faces are walls. Ramp side
walls and diagonal ledge shortcuts are blocked. Terrain colour is not an access
rule. Ground melee cannot reach through a cliff; movement can choose another
reachable opponent or use a ramp to reach an upper shelf. Visibility queries
still report visible opponents even when no route exists.

The current board has five drop edges, columns 3–7 from row 1 to row 2 (zero
based), and three ramp columns on the west side. The 8×8 footprint is unchanged.

`LedgeJump` owns start, airborne, and landing phases within the committed move.
Start plays in place; airborne loops while a game-owned arc crosses to the lower
cell; landing plays in place before releasing the origin/landing reservation.
The jumper cannot issue a new attack until landing recovery finishes. This does
not grant damage immunity. Competing units must queue or route around the full
corridor, including while the landing animation is playing.

Both render paths and headless play use manifest phase durations. Newer model
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
