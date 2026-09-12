# Arena travel prototype

Status: Active
Type: Runbook
Last updated: 2026-09-12

## Preview

In the editor's Game Preview, select **South Entrance → Travel Test** and press
**Play**. Three board Pokémon and two bench Pokémon recall into Poké Balls
with connected red beams. The scene fades through a dark cover, closed balls
are thrown from the friendly edge to their slots, then open to send the team
out in South Clearing.
After arrival, **R** travels back. The board is editable between trips; the next
trip captures the current formation. Stop/Play restarts the editor scenario.

This is a planning-only prototype. Normal arena scenarios and round progression
do not start travelling automatically. The lab starter sequence is unchanged.
The prototype has no audio; sound playback and travel cues remain a later step.

## Behavior and ownership

`ArenaTravelState` owns the sequence. `TeamTravelVisuals` supplies render-only
scale, body offset, connected beam and Poké Ball position/spin/hinge samples. These reuse the ball
model and hinge animation without setting gameplay capture flags or awarding a
capture. The same unit instances remain alive through the scene change.

- Preserve IDs, board cells, bench slots, facing, HP, energy, XP and levels.
- Recompute world positions against destination terrain, including bench height.
- Reserve valid original cells first; relocate invalid/overlapping cells to the
  nearest free playable cell, with stable row/column tie breaking.
- Clear movement commitments, jumps and perception tied to the previous terrain.
- Lock placement during travel and pause round and unit status timers. After
  arrival, placement resumes; the prototype stays in planning until exited.
- Validate the destination before applying it. A preparation failure restores
  normal presentation and keeps the team on its current map; **R** retries.

Destination CPU preparation happens synchronously when the scenario opens. A
single prepared/previous arena is cached alongside the active arena so repeated
trips can reuse it. This is not background streaming. Production round flow can
request preparation during planning once the next arena is known.

After a 0.65-second introductory hold, recall takes 1.18 seconds, cover 0.22,
reveal 0.25, ball throws 0.78 and send-out 0.90. Recall first displays a closed ball
and beam, then pulls the shrinking body along it. Arrival first reveals the
empty arena; closed balls follow spinning parabolic throws, then open at their
destination slots. The cover waits for an actual opaque draw before
activation and at least two actual destination draws before revealing. Loading
or GPU preparation can extend the covered interval. Fixed updates alone cannot
release the cover. The team stagger is bounded at 0.18 seconds regardless of size.

## Validation

`PAC_Tests.arena_travel_contract` exercises actual arena documents, persistent
roster state, cover/destination draw gates, loading failure, overlapping-cell
fallback and round-timer pause/release. It also checks connected recall presentation,
closed airborne balls, arc height, spin transport and landing before send-out.
The projected-unit travel presentation
test checks temporary model scale/tint and restoration without source-material
mutation. Existing capture and starter tests guard the shared presentation paths.

For a deterministic mid-recall screenshot:

```powershell
powershell.exe -NoProfile -File tools/environment/capture_arena_pilot.ps1 -Snapshot config/debug/editor_route1_pilot_travel.json -OutputDirectory debug/arena-travel/recall -Frame 64
```

Use frame 168 for throws, 218 for send-out and 300 for arrival. Build the game first. The editor
preview ID is `route1-pilot-travel`; rebuild both editor configurations with
`tools/housekeeping/build_editor_pair.ps1` after C++ changes.
