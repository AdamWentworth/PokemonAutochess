Archived on: 2026-03-31
Reason: Historical detailed Phase 1 implementation spec; the persistent-render-item design now lives mostly in code and only the remaining restructuring work stays active.
Superseded by: `docs/RENDER_RESTRUCTURING_OUTSTANDING.md`, `docs/RENDER_PATH_FILE_MAP.md`

# Render Phase 1 Spec: Persistent Render Items

Date: 2026-03-22

Purpose: define the first restructuring slice from
`docs/RENDER_RESTRUCTURING_PLAN.md` in implementation terms so the next work can
start from a concrete design instead of ad hoc optimization.

## Phase 1 Goal

Reduce heavy-scene CPU cost by stopping the renderer from rebuilding so much
per-unit/per-submesh batch state every frame.

This phase does not attempt to solve skinned instancing yet. It is the
foundation for that later work.

Primary target metrics:

- `avg_projected_model_prep_ms`
- `avg_projected_model_ms`
- `avg_render_build_ms`

Secondary target metrics:

- lower per-frame allocations and hash churn
- lower transient batch setup cost in dense scenes
- keep `D3D12` and `OpenGL` behavior functionally identical at the shared-path
  level

## Non-Goals

This phase does not try to do the following:

- animated instancing
- GPU-side animation sampling
- material system redesign
- backend-specific submission redesign
- `OpenGL`-specific uniform/UBO refactors
- gameplay-side changes

If a proposed change mainly targets draw-call collapse, it belongs to Phase 2,
not this phase.

## Current Problem

Today the heavy-scene render path still rebuilds a large amount of unit-local
state every frame across:

- `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`
- `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`
- `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`

What is already good:

- shared geometry/template reuse exists
- GPU clip skinning is already active on the retained path
- scene-pose caching exists

What is still expensive:

- per-unit prep orchestration
- per-submesh batch object population
- repeated material/geometry routing into `WorldIndexedBatch`
- repeated selection of fast-path state for content that has not materially
  changed

That means the renderer is re-deriving frame-local batch state from stable mesh
and material facts too often.

## Design Summary

Introduce persistent render items for projected unit models.

High-level idea:

- build stable render-item records once for a `(unit, mesh, submesh)` shape
- separate static item data from dynamic frame data
- update only the dynamic fields each frame
- queue batches from persistent items rather than rebuilding full batch state
  from scratch

This should preserve the current shared render path while shrinking the amount
of work done in `prepareProjectedUnitBackendMesh(...)` and
`renderProjectedUnitModel(...)`.

## Proposed Data Model

### 1. Render item key

Stable identity for one projected submesh contribution.

```cpp
struct ProjectedRenderItemKey {
    std::uint64_t unitId = 0;
    const render_model::MeshData* mesh = nullptr;
    std::uint32_t submeshIndex = 0;
};
```

Properties:

- unique per visible unit submesh
- stable while the unit uses the same mesh and submesh layout
- does not encode transient animation state

### 2. Static item data

Data that should only change when mesh/material identity changes.

```cpp
struct ProjectedRenderItemStaticData {
    std::string geometryCacheKey;
    const IRenderBackend::WorldMeshVertex* sharedVertices = nullptr;
    std::size_t sharedVertexCount = 0;
    const std::uint32_t* sharedIndices = nullptr;
    std::size_t sharedIndexCount = 0;

    std::string textureKey;
    std::string textureCacheKey;
    std::string normalTextureKey;
    std::string normalTextureCacheKey;
    std::string metallicRoughnessTextureKey;
    std::string metallicRoughnessTextureCacheKey;
    std::string occlusionTextureKey;
    std::string occlusionTextureCacheKey;
    std::string emissiveTextureKey;
    std::string emissiveTextureCacheKey;

    std::uint8_t materialMode = 0;
    std::uint8_t alphaMode = 0;
    std::uint8_t blendMode = 0;
    std::uint8_t characterInkingEnabled = 0;

    std::uint8_t skinnedBatch = 0;
    std::int32_t triNodeIndex = -1;
    std::int32_t meshNodeIndex = -1;
    std::uint8_t canUseSharedNodeTransform = 0;
    std::uint8_t hasStableGpuTemplate = 0;
};
```

Intent:

- hold mesh/submesh/template identity
- preserve authored material identity and resource keys
- encode static path eligibility so it is not recomputed from scratch each
  frame

### 3. Dynamic item data

Data expected to change during ordinary rendering.

```cpp
struct ProjectedRenderItemDynamicData {
    std::array<float, 16> modelMatrix{};
    float sortDepth = 0.0f;
    float vertexColorMulR = 1.0f;
    float vertexColorMulG = 1.0f;
    float vertexColorMulB = 1.0f;
    float vertexColorMulA = 1.0f;

    std::uint8_t gpuSkinning = 0;
    std::uint8_t gpuSkinningMode = 0;
    std::uint32_t skinMatrixCount = 0;
    const float* sharedSkinMatrices = nullptr;

    std::uint8_t materialAlphaOverride = 0;
    float alphaCutoff = 0.0f;
    std::uint32_t visibilityFrameStamp = 0;
};
```

Intent:

- hold current transform, tint, alpha, sort depth, and skin payload pointer
- avoid touching static geometry/material identity during ordinary frames

### 4. Dirty flags

```cpp
enum class ProjectedRenderItemDirtyBits : std::uint32_t {
    None = 0,
    StaticTemplate = 1 << 0,
    MaterialIdentity = 1 << 1,
    GeometryIdentity = 1 << 2,
    DynamicTransform = 1 << 3,
    DynamicMaterial = 1 << 4,
    DynamicSkinning = 1 << 5,
    Visibility = 1 << 6,
};
```

The important split is:

- static dirtiness means rebuild cached item state
- dynamic dirtiness means refresh the queueable fields only

### 5. Registry entry

```cpp
struct ProjectedRenderItemEntry {
    ProjectedRenderItemKey key;
    ProjectedRenderItemStaticData staticData;
    ProjectedRenderItemDynamicData dynamicData;
    std::uint32_t dirtyBits = 0;
    std::uint32_t lastTouchedFrame = 0;
};
```

## Proposed Ownership

### New shared module

Add a dedicated shared runtime module for persistent projected render items.

Recommended files:

- `src/game/runtime/shared/projected/SharedProjectedRenderItems.h`
- `src/game/runtime/shared/projected/SharedProjectedRenderItems.cpp`

Owned responsibilities:

- registry storage
- key lookup
- dirty-bit management
- frame touch/prune lifecycle
- helper functions for static-vs-dynamic rebuild

### Existing file ownership after the change

`src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`

- owns frame orchestration
- resolves visibility and unit-level context
- asks the render-item registry for the current item set for the unit
- no longer owns full batch reconstruction logic

`src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`

- becomes the main static template builder
- computes mesh/submesh-derived static item data
- should no longer rebuild fully-populated frame batches for ordinary steady
  state

`src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`

- becomes the dynamic updater and queue emitter
- applies current transform/tint/skin payload state onto persistent entries
- emits `WorldIndexedBatch` views from render items

`src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp`

- remains submission/sort/auto-instancing stage
- phase 1 should not require semantic changes here

`src/game/runtime/shared/backend/SharedBackendPoseEval.cpp`

- unchanged for Phase 1 except as a consumer of current animation state

Backend files:

- no new backend architecture in Phase 1
- `D3D12` and `OpenGL` continue to consume `WorldIndexedBatch` output from the
  shared path

## Registry Lifetime Model

### Scope

Use a session-local registry, not a global process cache.

Reason:

- render items depend on live unit ids, mesh pointers, and per-session runtime
  state
- session-local ownership makes invalidation safer

Recommended owner:

- `SessionProjectedWorldView` or another session-scoped render service already
  tied to frame/render lifetime

### Creation

Render items are created when:

- a visible unit is first rendered with a mesh
- the mesh changes
- submesh layout changes
- a prior item was pruned

### Reuse

Render items are reused when:

- the same unit is still using the same mesh and submesh
- static material/geometry identity did not change

### Pruning

Prune entries when:

- unit no longer exists
- item was not touched for `N` frames
- session resets / world clears / snapshot loads invalidate the visible set

Suggested starting policy:

- prune after `120` untouched frames
- hard-clear registry on full world/session reset

## Static Rebuild Rules

A static rebuild is required when any of these change:

- `meshForUnit` pointer
- submesh count or index mapping
- geometry cache key identity
- authored texture/material identity
- fast-path eligibility derived from stable mesh facts
- tri-node / mesh-node association

Static rebuild should produce:

- stable geometry pointers
- stable geometry cache key
- stable texture cache keys
- static path capability flags

This rebuild should be rare relative to ordinary frames.

## Dynamic Update Rules

A dynamic-only refresh is required when any of these change:

- model matrix
- attack pulse / scale / placement transform
- sort depth
- tint or alpha fade
- material time / flipbook time
- GPU skinning mode
- skin payload pointer or count
- visibility stamp

Dynamic update should not touch:

- geometry cache key
- texture key identity
- template vertex/index selection
- static path capability flags

## Proposed Frame Flow

### Current shape

1. visibility pass at unit level
2. prepare mesh state
3. rebuild per-submesh batch state
4. attach current transform/material/skinning
5. push batches
6. sort/submit

### Phase 1 target shape

1. visibility pass at unit level
2. fetch or create persistent render items
3. static rebuild only if dirty
4. dynamic refresh for touched items
5. emit lightweight `WorldIndexedBatch` queue entries from persistent items
6. sort/submit

The design win is that step 3 becomes infrequent.

## Queue Emission Strategy

Do not replace `WorldIndexedBatch` yet.

Phase 1 should keep the existing submission interface intact and generate
`WorldIndexedBatch` values from persistent render-item state.

Reason:

- it reduces risk
- it preserves current backend compatibility
- it keeps Phase 1 focused on prep/build cost

That means the render-item entry should be viewed as the canonical source, while
the `WorldIndexedBatch` remains the final submission envelope for now.

## Implementation Sequence

### Step 1

Add the registry and entry types.

Files:

- `SharedProjectedRenderItems.h/.cpp`

Deliverable:

- session-local registry with lookup, touch, prune, and reset support

### Step 2

Extract static template build from current prep code.

Files:

- `SharedProjectedUnitBackendMeshPrep.cpp`
- `SharedProjectedRenderItems.cpp`

Deliverable:

- helper that fills `ProjectedRenderItemStaticData` from the current mesh/submesh
  path

### Step 3

Teach the renderer to fetch or create persistent items per unit.

Files:

- `SharedProjectedUnitRenderer.cpp`
- `SharedProjectedUnitModelRenderer.cpp`
- `SharedProjectedRenderItems.cpp`

Deliverable:

- unit render path stops assuming fully transient submesh state

### Step 4

Move dynamic per-frame population into the backend mesh renderer.

Files:

- `SharedProjectedUnitBackendMeshRenderer.cpp`

Deliverable:

- dynamic update path writes only transform/color/sort/skinning data onto
  persistent items, then emits `WorldIndexedBatch`

### Step 5

Add pruning and full-reset hooks.

Files:

- session/world reset locations
- likely `SessionProjectedWorldView.cpp` and/or session reset helpers

Deliverable:

- no stale registry entries across world/session transitions

## Metrics That Must Improve

To keep this phase, the heavy-scene benchmark should show a meaningful drop in:

- `avg_projected_model_prep_ms`
- `avg_render_build_ms`

Nice-to-have but not required:

- lower `avg_projected_model_ms`
- lower `avg_frame_cpu_ms`

Metrics that do not need to move in Phase 1:

- draw calls
- GPU frame time
- backend descriptor table count
- OpenGL texture bind count

If only those backend-facing metrics move, the change is probably solving the
wrong phase.

## Validation Plan

### Functional validation

- heavy snapshot renders identically before/after
- unit mesh swaps still work
- snapshot load / menu transition / session reset do not leave stale models
- no missing board/models

### Perf validation

Use:

- current heavy-scene automated benchmark
- same seed
- same snapshot
- same resolution

Compare before/after:

- `avg_frame_cpu_ms`
- `avg_render_build_ms`
- `avg_projected_model_prep_ms`
- `avg_projected_model_geometry_ms`
- `avg_projected_model_ms`

### Rollback condition

Revert if:

- correctness regresses
- the new registry adds complexity without a clear `render_build_ms` win
- the improvement is too small to justify becoming the foundation for Phase 2

## Risks

### Risk 1: dirty-bit bugs

Symptom:

- stale materials
- stale transforms
- wrong submesh geometry after mesh/animation changes

Mitigation:

- start with conservative invalidation
- optimize invalidation only after correctness is stable

### Risk 2: registry lifetime bugs

Symptom:

- stale unit entries after snapshot loads or world clears

Mitigation:

- hard-clear on full world/session reset first
- only add more surgical reuse after the base path is stable

### Risk 3: too little win

Symptom:

- code becomes more complex but `render_build_ms` barely moves

Mitigation:

- keep Phase 1 narrowly focused
- do not redesign submission and render items at the same time

## Why This Phase Still Matters

Even if Phase 1 only removes part of the current CPU cost, it is still the
right prerequisite for Phase 2.

Reason:

- skinned instancing will need stable per-item identity
- persistent GPU resource addressing is easier once render items are not
  ephemeral
- without this phase, Phase 2 risks becoming a large backend feature sitting on
  top of a still-transient shared renderer

## Phase 1 Exit Criteria

Phase 1 is complete when:

- projected unit models are represented by persistent render items
- static mesh/material identity is not rebuilt every ordinary frame
- heavy-scene `avg_render_build_ms` and `avg_projected_model_prep_ms` improve
  materially
- correctness is stable across snapshot loads and session resets
- the code leaves a clean foundation for Phase 2 skinned instancing

## Immediate Follow-On

Once Phase 1 lands and proves out, the next spec should be:

- D3D12-first skinned instancing

That next spec should define:

- per-instance skeleton addressing
- shared skin payload residency
- batch compatibility rules for animated instances
- expected impact on draw count and indexed submit cost
