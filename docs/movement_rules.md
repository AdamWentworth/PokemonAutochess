# 📜 movement_rules.md

---

## movement_rules:

```yaml
grid:
  allow_diagonal: true
  diagonal_cost: 1.414
  straight_cost: 1.0
  pathfinding: A*

planning:
  type: simultaneous
  can_predict_future_positions: false
  reserve_cells_during_planning: true       # Reservations are made after planning, not during
  reservations_affect_pathfinding: false     # Units path based on the initial grid state only
  conflict_resolution_determines_reservations: true
  must_win_conflict_to_commit: true

planning_order:
  type: randomized    # Ensures fairness across units during sequential loop execution
  order_by:
    - unit_id         # Used as a fallback sort key for tie resolution

movement_commitment:
  requires_reservation: true
  requires_conflict_win: true

conflicts:
  resolution_order:
    - by_distance_to_enemy
    - tie_breaker: higher_movement_speed
    - fallback: lower_unit_id
  losers:
    - remain_stationary
    - replan_next_frame
    - may_retry_if:
        - remains_optimal: true
        - no_alternatives: true

swapping:
  allowed: false
  mutual_swap_prevention: true
  enforced_during_conflict_resolution: true
  slide_past_allowed: false
  fallback: replan_alternate_route
  notes:
    - Swaps are always disallowed, even if logically possible or symmetrical.
    - Swap conflicts always result in re-evaluation and alternate pathfinding.
    - Units attempting mutual swaps are both blocked.

occupancy:
  occupied_if:
    - has_unit: true
    - reserved_by_unit: true

application:
  only_committed_units_move: true
  interpolate: true
  snap_if_below_threshold: true
  reservation_grid_usage:
    - planning: false
    - conflict_resolution: true
    - movement_application: true
  failed_move:
    animate: false
    interpolate: false
  notes:
    - "Interpolation" refers to visual movement between cells.
    - "Snap" refers to instant alignment if too close to animate.
    - Visual snapping is avoided, but logical snapping ensures alignment.

engagement:
  trigger: adjacent_to_enemy
  disables_movement: true
  actions:
    - attack
    - reorient

orientation:
  rules:
    - if_adjacent_enemy: face_engaged_enemy
    - else_if_visible_enemy: face_nearest_enemy
    - else: retain_previous_facing
  update_timing: post_movement

deadlock:
  allow_overlap: false
  correction:
    - relocate_to_nearest_free_cell
    - fallback_spawn_if_no_adjacent_space
  visual_feedback:
    - animate_correction: false
    - snap_instantly: true
  notes:
    - Deadlock correction is a system-level override, not part of normal movement logic.

pathfinding_failsafe:
  on_failure:
    - remain_stationary
    - retry_next_frame: true
  retry_conditions:
    - not_adjacent: true
    - not_disabled: true
```

---

## 🔹 1. Grid-Based Pathfinding

- Units move on a 2D grid of fixed-size cells.
- A* pathfinding is used to reach the nearest enemy.
- Diagonal movement is allowed, but costs slightly more (`1.414` vs `1.0` for straight).

---

## 🔹 2. Simultaneous Movement Planning

- All units plan their move for the current frame simultaneously.
- Future positions of other units are not known, only current positions.
- Destination cells are reserved during planning.
- Units only move if they win conflicts and commit to the destination.

---

## 🔹 3. Conflict Resolution

- If multiple units target the same cell:
  - **Winner** is the unit closer to its enemy.
  - **Tie-breaker**: unit with higher movement speed.
- Losing units:
  - Remain stationary.
  - Re-evaluate their path on the next frame.
- Retrying the same cell is allowed only if:
  - It’s still the optimal route.
  - No other valid paths exist.

---

## 🔹 4. Swapping Support

- Swapping between units is **always disallowed**.
- Mutual swaps (A↔B) are prevented entirely — neither may move.
- No sliding past or positional flipping.
- All swap scenarios result in full re-evaluation and alternate routing.

---

## 🔹 5. Grid Occupancy

A cell is considered occupied if:
- A unit is currently in the cell.
- A unit has reserved it for this frame via conflict resolution.

Units:
- Never move into occupied cells.
- Remain visually centered in their current cell unless moving.
- During movement, interpolate smoothly to the center of the target cell.

---

## 🔹 6. Movement Application

After conflict resolution:
- Only units that secured their destination may move.
- Movement is visualized as interpolation toward the new cell.
- If the move is very short, snapping occurs instantly (logically, not visually).
- Units that failed to secure movement:
  - Do not animate or interpolate.
  - Remain fully stationary.

---

## 🔹 7. Adjacent Engagement

- Units adjacent to an enemy do not pathfind — they are considered **engaged**.
- While engaged:
  - Movement is disabled.
  - Units may attack or reorient.
  - Pathfinding retries are suspended.

---

## 🔹 8. Unit Orientation

- Units face:
  - Their engaged enemy (if adjacent),
  - The nearest visible enemy (if no engagement),
  - Or retain previous facing (if no targets).
- Orientation is updated after movement — may be smoothed visually.

---

## 🔹 9. Deadlock & Overlap Resolution

> **System override (not part of standard movement)**

- If two units end up in the same cell (rare edge case):
  - Immediately relocated to nearest free cell.
  - If none nearby, fallback to a predefined safe spawn.
- Corrections:
  - Are instant.
  - Have no animation or visible interpolation.
  - Are silent and non-intrusive.

---

## 🔹 10. Failsafe Pathfinding

- If pathfinding fails (no reachable enemy due to blocks/congestion):
  - Unit stays still this frame.
  - Will retry next frame unless:
    - Engaged (adjacent to an enemy),
    - Disabled by another system.
- Unit remains idle until the board state changes if no path ever becomes available.

---

## 🧠 Optional Enhancements (Experimental / Future)

### ✔️ Priority Movement System
- Units with higher speed resolve conflicts first.
- Helps fast units navigate congested areas better.

### ✔️ Multi-frame Movement Lock
- After a successful move, the unit "locks" its new cell for 1–2 frames.
- Prevents tight clustering and unrealistic chain-following.

### ✔️ Staggered Conflict Resolution
- Conflict resolution can be staggered by:
  - Unit ID,
  - Faction,
  - Board position.
- Helps reduce simultaneous conflicts in crowded spaces and improves determinism.

---
