# CPU vs GPU Work Split (3D Games)

Date: 2026-03-03

Goal: define a practical CPU vs GPU contract, then map what PokemonAutochess is actually doing right now.

## Core Rule
- CPU owns gameplay truth and frame orchestration.
- GPU owns parallel rendering math and pixel throughput.

Do not move authoritative gameplay state to GPU if determinism, replayability, or networking correctness matters.

## Recommended Split

### CPU (authoritative, control-heavy)
- Game simulation: rules, combat resolution, economy, buffs/debuffs, round logic.
- AI and high-level behavior/state machines.
- Input and UI logic.
- Scene orchestration:
  - visibility set construction (or hybrid with GPU culling),
  - draw list assembly,
  - resource lifetime/streaming decisions.
- Asset IO/decode/decompression and cache management.
- Animation control flow:
  - clip selection/blending state decisions,
  - event markers and gameplay-triggered animation events.
- Physics/gameplay collision where determinism is required.

### GPU (parallel, throughput-heavy)
- Vertex transforms and skinning/morphing.
- Material evaluation (PBR), normal mapping, lighting, shadows.
- Rasterization and depth testing.
- Post-processing (tone mapping, bloom, color grading, AA).
- Particle simulation/rendering (often compute + draw).
- Image-based lighting work (prefiltered env lookups / PMREM usage).
- Optional GPU culling/LOD selection for large scenes.

## PokemonAutochess: Current Implementation Map

This section is based on direct code parse of runtime and backend render paths.

### CPU-owned right now
- Gameplay simulation and world state:
  - `src/game/runtime/GameSession.cpp`
  - `src/game/runtime/GameRunner.cpp`
- Frame orchestration and draw list construction:
  - builds `worldBackgroundQuads`, `worldQuads`, `worldTriangles`, `world3DTriangles`, `worldIndexedBatches`, `lines`, `sprites`, `textLines`.
  - `src/game/runtime/GameSession.cpp`
- Projected unit pose and animation evaluation:
  - `computeProceduralPose(...)`
  - `evaluateScenePose(...)`
  - `src/game/runtime/shared/projected/SharedProjectedUnitRenderer.cpp`
- Projected mesh prep, budgeting, and path selection:
  - triangle budget scaling per unit, fast textured path gating, parity tint policy.
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`
- Per-vertex transform work (hot path still CPU-heavy):
  - CPU skinning helpers (`skinVertexAtNode`, `ensureSkinMatricesForNode`)
  - CPU procedural deform (`deformLocalVertex`)
  - CPU normal/tangent reconstruction and model-space to world-space conversion.
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
- Per-triangle submission and CPU vertex packing:
  - appends `WorldMeshVertex` and indices per triangle/submesh.
  - fallback shading path uses CPU `shadeVertexLitColor(...)`.
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTriangleSubmit.cpp`
- Particle simulation:
  - per-particle update and CPU staging buffer fill.
  - `src/engine/vfx/ParticleSystem.cpp`
- Perf metric aggregation and JSON emission:
  - `projected_pose_eval_ms`, `projected_model_ms`, `projected_overlay_ms`, `gpu_frame_valid`, etc.
  - `src/game/runtime/GameRunner.cpp`

### GPU-owned right now
- PBR world shading for indexed mesh batches:
  - normal, metallic-roughness, occlusion, emissive sampling.
  - environment contribution and tone mapping in shader path.
  - OpenGL: `src/engine/render/opengl/OpenGLRenderBackendWorldPipeline.cpp`
  - D3D12: `src/engine/render/d3d12/D3D12RenderBackendPipelines.cpp`
- Texture/material binding and draw submission:
  - OpenGL world draw binding path.
  - D3D12 descriptor-based world draw binding path.
  - OpenGL: `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
  - D3D12: `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`
- Rasterization, depth/stencil, blending, and present:
  - handled by each backend pipeline/device.
- Particle draw:
  - rendered on GPU after CPU simulation buffer upload.
  - `src/engine/vfx/ParticleSystem.cpp`

### Hybrid or conditional paths (important)
- GPU clip skinning exists in code path and is now backend-gated at session level:
  - OpenGL and D3D12 default ON, with backend env gates available for rollback/debug.
  - `src/game/runtime/GameSession.cpp`
- Resolver supports GPU clip skinning batch setup (`configureGpuClipSkinningBatch(...)`) for eligible indexed/textured projected batches.
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
- Material mode 2 routes world model lighting to backend shaders (GPU PBR), but vertex/index build is still CPU-side.
  - `batch.materialMode = 2u`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`

### Runtime Flags That Affect CPU/GPU Split
- `PAC_BACKEND_CLIP_SKINNING`
  - Enables clip-skinning feature inside transform resolver.
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshTransforms.cpp`
- `PAC_BACKEND_GPU_CLIP_SKINNING`
  - Global runtime gate for projected GPU clip-skinning path selection.
  - `src/game/runtime/GameSession.cpp`
- `PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL` / `PAC_BACKEND_GPU_CLIP_SKINNING_D3D12`
  - Backend-specific GPU clip-skinning gates.
  - Current default behavior: both enabled.
  - `src/game/runtime/GameSession.cpp`
- `PAC_BACKEND_VERTEX_DEFORM`
  - Toggles CPU procedural vertex deformation path.
  - `src/game/runtime/BackendProceduralPose.h`
- `PAC_BACKEND_MODEL_FAST_TEXTURED`
  - Controls fast textured indexed model path selection.
  - `src/game/runtime/GameSession.cpp`
- `PAC_GLTF_PARITY_STRICT`
  - Controls strict glTF parity tint/material behavior in projected model path.
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshPrep.cpp`
  - `src/game/runtime/shared/projected/SharedProjectedUnitBackendMeshRenderer.cpp`
- `PAC_BACKEND_PBR_BIND_LOG` / `PAC_BACKEND_PBR_BIND_LOG_MAX` / `PAC_BACKEND_PBR_DEBUG_VIEW`
  - Diagnostics for material/texture bind and PBR debug views; useful for backend parity checks.
  - `src/engine/render/opengl/OpenGLRenderBackendWorldDraw.cpp`
  - `src/engine/render/d3d12/D3D12RenderBackendWorldDraw.cpp`

## New Findings Added In This Revision

- OpenGL is using the GPU for rendering and shading. It is not CPU-only.
- D3D12 and OpenGL now both expose backend GPU frame timing in runtime perf reporting (`gpu_frame_valid` path), with OpenGL using timer queries.
- GPU clip skinning infrastructure is active via backend-specific gates, now default-on on both APIs.
- The biggest current render-build cost is still CPU projected model work (`projected_model_ms` / `render_build_ms`), not GPU saturation.
- Strict glTF parity mode defaults enabled through `PAC_GLTF_PARITY_STRICT` in projected backend mesh prep/renderer.
- Shared projected transform resolver now caches local deformed positions and per-node model-space normals/tangents, and uses direction-only skinning for normal/tangent evaluation to cut redundant CPU work.
- Fast textured projected path now splits mixed-node submeshes into per-node indexed batches for GPU clip skinning, increasing GPU skinning eligibility instead of forcing whole submeshes back to CPU skinning.
- Mixed-node split path now reuses the original submesh batch for the first node and preserves reserved vertex/index capacity on cloned batches to avoid per-frame allocator churn.
- Renderer hot loop now precomputes `triNodeIndex` and fast-batch routing per triangle for mixed-node GPU-skinning paths, removing repeated per-triangle hash lookups in the submit loop.
- Fast position-only projected path is no longer blocked by "all submeshes textured"; fallback texture normalization now allows GPU clip-skinning coverage even when source assets have missing base textures.

## Autobattler-Specific Guidance
- Keep combat outcomes and RNG on CPU.
- Keep per-unit visual deformation, skinning, and material shading on GPU where possible.
- Avoid per-unit CPU mesh rebuilds each frame unless strictly necessary.
- Treat all board units equally; do not apply hero-only quality logic.

## Anti-Patterns
- CPU doing per-vertex/per-pixel style work every frame.
- GPU used for authoritative gameplay decisions.
- Backend-specific gameplay behavior divergence (OpenGL vs D3D12).
- Large CPU-side draw-call churn that could be batched/instanced.

## Practical Boundary Tests
- If work scales with triangle/pixel count, default to GPU.
- If work scales with rules/entities and needs determinism, default to CPU.
- If CPU time rises sharply with unit visual complexity, render math is still CPU-heavy.

## Performance Targets (60 FPS baseline)
- Frame budget: 16.67 ms total.
- CPU main-thread target: <= 6-8 ms steady state.
- GPU target: <= 8-10 ms steady state.
- Present wait should be tracked separately from true render cost.

## What To Measure
- CPU:
  - fixed update, render build, render submit, draw-call count.
  - projected pose/model/overlay buckets.
- GPU:
  - frame time and major pass cost (world, shadows, post, particles).
- Cross-backend parity:
  - startup contract signature checks.
  - deterministic scene screenshot diff harness.

## Implementation Strategy (High Level)
1. Keep gameplay deterministic and CPU-authoritative.
2. Move visual hot paths to GPU first (skinning/deformation, then heavier particle paths).
3. Batch and instance aggressively to reduce CPU submit overhead.
4. Keep shader/material contracts shared across APIs.
5. Add quality tiers after max-quality baseline is stable and profiled.

## Summary
- CPU decides what the frame means.
- GPU decides how the frame looks.
- In current code, model rendering quality is shader-driven on GPU, but too much per-unit/per-vertex prep is still CPU-side.
