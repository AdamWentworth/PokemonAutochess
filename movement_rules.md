# 🧭 Movement Rules for Autochess Grid Units

## 🔹 1. Grid-Based Pathfinding
- Units move on a 2D grid of fixed-size cells.
- A* pathfinding is used to reach the nearest enemy.
- Diagonal movement is allowed but slightly more costly (1.414 cost vs. 1.0).

---

## 🔹 2. Simultaneous Movement Planning
- All units plan their move for the current frame before any movement is applied.
- Units cannot see others' future moves, only current positions.
- Target cells are claimed during planning and resolved before applying movement.
- **Units only commit to movement if they have secured the destination cell.**

---

## 🔹 3. Conflict Resolution
- If multiple units want the same cell:
  - **Primary criterion**: The unit closer to its target enemy wins the cell.
  - If distances are equal (or nearly equal), a **consistent, deterministic tie-breaker** is used (e.g. lower unit ID).
  - **Losing units stay in place** this frame and do not visibly attempt to move.
  - Units that lose a conflict will **re-evaluate their path** on subsequent frames to find alternate routes.
  - A unit will not retry the same contested cell unless:
    - It remains the optimal path after re-evaluation, or
    - No other valid paths exist.
  - Units are allowed to remain stuck only if they are **completely boxed in** with no valid paths to their target.

---

## 🔹 4. Swapping Support
- **Swapping between units is not allowed**.
- If Unit A wants to move to a cell currently occupied by Unit B, and Unit B simultaneously wants to move to Unit A’s cell:
  - **Neither unit may move** unless their target cell becomes available through normal resolution.
  - Even if a mutual swap appears logically possible, it is disallowed to preserve consistent collision behavior and visual clarity.
- Units do not "slide past" each other or perform position flips.
- Instead, units should **re-evaluate their path** and attempt to find alternate, valid routes to their target enemy — even if the path is longer or requires moving around the board.
- This ensures units continue making intelligent decisions and do not become stuck simply due to swap scenarios.

---

## 🔹 5. Grid Occupancy
- A grid cell is considered *occupied* if:
  - A unit is currently standing in it, or
  - A unit has successfully secured it through conflict resolution for the current frame.
- Units may not move into any occupied cell under any circumstance.
- Units remain visually centered within their current grid cell at all times, unless:
  - They are actively moving toward a newly secured destination,
  - In which case they interpolate smoothly between grid centers during transition.
  
---

## 🔹 6. Movement Application
- After conflict resolution is complete:
  - **Only units that have secured their destination cell** (through being uncontested or winning a conflict) are allowed to move.
  - These units interpolate smoothly toward the center of their target grid cell based on their movement speed and delta time.
  - If the distance to the destination is smaller than a minimal movement threshold (epsilon), the unit instantly **snaps** to the center of the cell.
  - Units that **did not secure a valid move** this frame:
    - Remain visually stationary,
    - Show no movement animation, displacement, or positional interpolation.
---

## 🔹 7. Adjacent Engagement
- If a unit is currently adjacent to an enemy (within 1 cell in any direction, including diagonals):
  - It will **not attempt to move** as part of normal pathfinding.
  - Instead, the unit remains in its current cell and is considered "engaged" in combat.
  - During this state, the unit may:
    - **Attack** the adjacent enemy,
    - **Reorient** itself to face the target, based on the enemy’s position.
---

## 🔹 8. Unit Orientation
- Units face their **engaged enemy** — the first adjacent enemy they become locked onto in combat.
  - Once a unit becomes adjacent to an enemy, it will continue facing that specific enemy until that enemy is defeated.
- If no enemy is currently adjacent, the unit will:
  - Face the **nearest visible valid enemy** after completing its movement for the current frame,
  - Or retain its previous facing direction if no valid targets exist.
- Orientation is calculated **after movement is applied** and is typically updated instantly, though it may be smoothed for visual fidelity.
---

## 🔹 9. Deadlock & Overlap Resolution
- In the rare event that two or more units occupy the same grid cell due to a system error (e.g. frame-timing edge case):
  - The units are immediately relocated to the **nearest available free cell**, prioritizing adjacent spaces in a fixed, deterministic order (e.g. clockwise).
  - If no adjacent cell is available, the system may:
    - Skip the affected unit's move for the frame,
    - Or reposition it to a fallback spawn location (e.g. last known safe cell or predefined backup cell).
  - **These corrections are applied instantly and silently**, without any visible interpolation or animation.
  - Units may not visibly overlap, snap back, or visibly slide into place due to these corrections.
  - This rule exists to maintain spatial legality and prevent game-breaking deadlocks — it is not part of the standard movement system.
---

## 🔹 10. Failsafe Pathfinding
- If A* pathfinding fails to find a valid route to the unit's nearest enemy:
  - The unit will **remain stationary** for the current frame and will not attempt movement.
  - A pathfinding failure is defined as:
    - No unblocked path exists to any reachable enemy cell,
    - Or all potential paths are currently blocked by occupied or invalid grid cells.
- The unit will **automatically reattempt pathfinding** on subsequent frames, unless:
  - It is adjacent to an enemy (engaged),
  - Or another system explicitly disables movement (e.g. stunned or frozen).
- This logic prevents wasted movement attempts when pathing is impossible due to full congestion or blocking.
- If a unit is fully boxed in and no enemy is reachable by any path, it will remain idle until the board state changes.
---

## 🧠 Bonus: Optional Enhancements (For Future Use)

These features are not required for baseline movement but may be implemented to improve unit flow, realism, or strategic depth:

- **Priority Movement System**  
  Units with higher movement speed are given preference during conflict resolution.  
  This helps faster units navigate congested areas more effectively and reinforces their mechanical identity.

- **Multi-frame Movement Lock**  
  When a unit successfully moves into a new cell, it temporarily **locks** that cell for 1–2 additional frames.  
  Other units may not immediately move into the vacated cell, preventing units from clustering too tightly or forming unrealistic follow lines.  
  This creates more natural spacing between units during movement phases.

- **Staggered Conflict Resolution (Optional Alternative to Movement Lock)**  
  As an alternative or supplement to movement locking, unit conflict resolution can be **staggered** by unit ID, faction, or position on the board.  
  For example, lower ID units may resolve movement earlier in the frame, reducing the number of simultaneous conflicts.  
  This adds a deterministic order to planning and spreads movement decisions across ticks, improving performance and avoiding deadlocks in tightly packed groups.

