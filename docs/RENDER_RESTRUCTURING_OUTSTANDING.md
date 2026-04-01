# Render Restructuring Outstanding Work

Status: Active
Type: Roadmap
Last updated: 2026-03-31

This doc tracks the renderer restructuring work that is still outstanding after
the earlier retained-path and persistent-render-item cleanup. Historical plans
and Phase 1 design detail stay in `docs/archive/`.

## Current State
- The repo already has meaningful GPU offloads in the projected model path:
  - GPU clip skinning
  - GPU node-global composition
  - rigid-node GPU transform reuse
  - persistent projected render-item ownership in
    `src/game/runtime/shared/projected/core/SharedProjectedRenderItems.*`
- That means the renderer is no longer at the "make the GPU do anything"
  stage.
- The main remaining problem is structural CPU work around per-unit prep,
  per-batch setup, and backend submission overhead.

## What Phase 1 Already Bought Us
- Persistent projected render items now exist.
- Dynamic vs static projected item state is explicit.
- The world-scene path and shared backend-mesh path can both reuse stable item
  identity instead of treating every batch as fully transient.

This means the old Phase 1 spec is not redundant history, but most of its
design is now represented in code rather than active planning.

## Outstanding Work

### 1. Make the persistent-item path pay off more clearly
- Keep reducing repeated per-frame projected model prep and queue emission work.
- Watch:
  - `projected_model_prep_ms`
  - `projected_model_geometry_ms`
  - `render_build_ms`

### 2. D3D12-first skinned instancing / shared animated submission
- This is still the next major restructuring slice.
- Goal:
  - batch more animated compatible content together
  - reduce per-unit submission/state overhead
  - reduce draw and descriptor churn in dense scenes
- Watch:
  - `render_world_indexed_ms`
  - draw count
  - D3D12 descriptor-table activity
  - total `render_build_ms`

### 3. Static vs dynamic skin payload residency
- Only continue this lane where it helps the measured instancing/submission
  path.
- Do not treat it as a standalone win if it does not remove enough upstream CPU
  structure.

### 4. GPU-side animation sampling
- Still a possible later phase, not the immediate next step.
- It should follow the more structural submission/dataflow work rather than
  replace it.

### 5. OpenGL decision point
- After the D3D12-first restructuring work proves out, decide what subset is
  worth porting cleanly to `OpenGL`.
- Do not force parity-first porting if it blocks a clearly worthwhile D3D12
  design.

## What Is Not The Right Focus
- Repeating small cache/container rewrites that do not reduce structural hot
  work.
- Forcing `OpenGL` parity on a D3D12-specific restructuring idea before the
  D3D12 result is clearly proven.
- Treating every CPU bucket as equally important when the measured wall is
  still shared projected build/submission work.

## Success Signals
- Dense-scene `render_build_ms` continues trending down.
- `projected_model_prep_ms` and `render_world_indexed_ms` move materially, not
  just noise-level tenths.
- The renderer submits fewer animated batches for comparable visible content.
- New restructuring slices reduce CPU structure without breaking the current
  visual result.

## Historical Context
- Broad historical plan:
  - `docs/archive/RENDER_RESTRUCTURING_PLAN.md`
- Detailed Phase 1 design spec:
  - `docs/archive/RENDER_PHASE1_PERSISTENT_RENDER_ITEMS_SPEC.md`

