# Encounter grass

Status: Active
Type: Contract
Last updated: 2026-09-09

South Entrance uses the grass clump footprints published in `arena.phscene` for
concealment, including the dense half-cell border ring. Dark lawn and sparse
decorative tufts do not grant cover. Individual swaying blade tips do not change
the boundary.

## Sight and combat

- Each Pokemon sees enemies on open ground and inside its own connected grass
  patch. It cannot see enemies in another patch. Allies are always visible.
- Shared edges and overlapping grass polygons form one patch. Corner contacts
  of 10 cm or less do not connect otherwise separate patches; disconnected
  islands remain separate even when they belong to one authored prefab.
- Membership follows continuous ground position, not rounded cell occupancy.
  Airborne Pokemon and ledge jumpers receive no ground-cover concealment.
- Starting an attack reveals its user through the attack window and for 1.25
  seconds afterward. New attacks recheck sight. Already scheduled hits and
  launched projectiles still resolve if their target enters cover.
- The player sees the union of allied units' sight. This does not give individual
  Pokemon remote targeting knowledge from a teammate in another patch.
- New captures require player-team sight. Debug visibility does not grant it.

The shared `CombatMapRules` policy drives native movement, script queries,
attack acquisition, enemy-facing commands, player presentation and capture selection. Physical route
reachability is evaluated after acquisition, without accidentally recomputing
sight from hypothetical pathfinding positions. Attack reveals use fixed
simulation time and survive debug snapshot reloads.

## Searching

Each Pokemon remembers its most recently pursued visible opponent for up to
eight simulation seconds after losing sight. It stores an observed location,
not a link to the hidden opponent. A visible movement endpoint entering grass
records that patch; otherwise the lead is the last seen position. Observations
continue during committed moves and attacks. Hidden movement never refreshes
the location or timer, and teammates/debug vision do not supply memories.

With no visible, reachable target, a unit first investigates its remembered
patch via the nearest reachable entry, or checks its last seen open-ground
cell. Route planning uses terrain and visible units; hidden reservations can block
the immediate step but cannot redirect the distant search. Normal collision and
ledge rules still apply. It completes an existing step before changing route. Reaching the area without acquiring an
enemy dismisses the lead; blocked investigations can wait until the eight-second
limit. A currently visible, reachable enemy takes priority and replaces the
lead. Memory never enables targeting, attacks, facing a hidden unit or player
visibility. Flying units can visit the area but still use normal sight rules.

Without a useful memory, a unit advances toward the enemy end of its lane,
then returns through the next lane. The deterministic
serpentine search visits reachable board cells, including grass. Occupied or
unreachable waypoints are skipped. Searching uses ordinary step reservations,
diagonal corridor checks, ramps and one-way ledge jumps; it never takes hidden
enemy coordinates as a goal. Combat takes priority as soon as a target appears.

This removes the simple hidden-versus-hidden idle deadlock. It is not a general
battle-termination guarantee: permanently separated terrain, immobile units or
crowded passages can still prevent engagement. A future round timeout/outcome
rule should handle those cases rather than granting omniscient targeting.

## Presentation and editor check

Moving grounded Pokemon bend nearby blade clusters and produce a small wake.
Standing Pokemon hold a gentler opening without continuous movement flutter.
Grass recovers after contact ends; its patch footprint never grows or shrinks.
Hidden opponents do not contribute new contact animation to the player's view.
Their body, shadow and unit HUD are omitted from projected rendering.
Attack and projectile effects already in progress remain visible.

Above the viewport select **Route 1 - South Entrance > Grass Test**, then Play.
Bulbasaur and Rattata start in different patches and search until they find an
opponent. **F10** toggles **Show concealed units** with a visible reminder; it
changes presentation only. The fixture is
`config/debug/editor_route1_pilot_grass.json`. The override, search state and location memory are
preserved in debug snapshots. New rounds, fresh games and editor repositioning
clear old memories; a fresh game also resets the override.

## Verification

- `PAC_Arena.logic`: connected/disconnected cover, continuous boundaries,
  airborne/revealed actors, deterministic full-board searches and legal steps.
- `encounter_grass_gameplay`: the real Grass Test keeps Rattata concealed during
  its first southward step; one-way pursuit, queued/idle facing, script queries,
  individual/team sight, attack rejection/reveals, scheduled impacts and searches.
- `encounter_grass_memory`: seen entry versus never-seen enemies, hidden-position
  independence, actual reacquisition, expiry, visible-target priority and resets.
- `ledge_jump_rendering`: real model submissions, including visible/hidden/debug
  transitions with no hidden body, shadow or HUD submissions.
- `route1_arena_pilot_contract`: actual authored grass skin palettes change under
  stationary and moving contact and recover to ambient wind after release.
- `session_debug_snapshot_contract`: reveal, search and debug-view round trips.
