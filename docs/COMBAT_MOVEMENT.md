# Combat movement

The current gameplay test map is the Blender-authored Route 1 south entrance
(`routes/route1-pilot`, art pass 10). Open its Planning or Battle preset in
Phlosion Editor. The south clearing is deferred.

`MovementSystem` owns automatic combat movement. Each step reserves its origin
and destination until arrival. Diagonal steps also reserve their two adjoining
cells, and A* cannot cut across an occupied or reserved corner. Reservations for
existing moves are established before any idle unit plans another step.

Idle units compete in order of distance to the nearest enemy, movement speed,
then stable unit ID. Equally near enemies are selected by stable ID as well.
A unit routes around a reserved corridor when possible. If every attack position
is occupied, it can approach a closer reachable cell and queue there; it waits
when no closer position is available. Arrival releases the completed step for
the next planner update. Units with zero speed do not claim new destinations.

Authored takeoff animations retain their reservations while the flyer is still
on the ground. Flight currently follows the same board occupancy rules; separate
flight movement, grass visibility and directional ledge traversal remain future
mechanics. Rendered attacks can still have visual lunges independent of these
logical movement positions.

Verification:

```powershell
.\build\Release\PAC_Tests.exe --filter movement_collision_regressions
.\build\Release\PAC_Tests.exe --filter movement_invariants
.\build\Release\PAC_Tests.exe --filter end_to_end_headless
.\build\Release\PAC_Tests.exe --filter route1_arena_pilot_contract
```

The collision regressions cover stolen destinations, premature origin release,
blocked diagonal corners, a narrow queue, and 16-unit approaches at 120 Hz,
30 Hz and 5 Hz. They measure separation throughout each frame's movement and
verify that reversing unit storage order preserves the resulting paths.
