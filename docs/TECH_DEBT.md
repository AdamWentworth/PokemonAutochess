# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

Open items
- Monolithic gameplay/render files remain high-risk for regressions:
  - `src/engine/render/D3D12RenderBackend.cpp` (~1500+ lines after helper extraction)
  - `src/engine/render/gltf/ModelFastGltfTextures.cpp` (~400+ lines)
  - `src/game/scripting/ScriptAPICombat.cpp` (~300+ lines)
- Packaged build smoke run is manual only (no automated run in CI).
- Full fresh-machine validation of the installer flow (no cached vcpkg/toolchain).

Recent improvements
- Removed duplicate CTest name maintenance by deriving CTest registration directly from `tests/TestMain.cpp`.
- Added targeted config-loader regression tests for Pokemon, Evolution, and Flyer parsing contracts.
- Reduced repeated case-normalization logic in `PokemonConfigLoader` and `EvolutionConfigLoader`.
- Normalized source comments/strings to ASCII in `src/` and added `source_ascii_hygiene` regression test.
- Split GameWorld progression/merge/XP logic into `src/game/world/GameWorldProgression.cpp` and added merge progression regression coverage.
- Split GameWorld spawn/bench roster flow into `src/game/world/GameWorldRoster.cpp` with targeted spawn+bench regression coverage.
- Split GameWorld economy/inventory/healing flows into `src/game/world/GameWorldEconomy.cpp` and added income, inventory, and reset regression coverage.
- Split GameWorld capture attempt lifecycle into `src/game/world/GameWorldCapture.cpp` and added capture precondition/success/failure recovery regression coverage.
- Split GameWorld query/synergy helpers into `src/game/world/GameWorldQueries.cpp` and added regression coverage for type-line counting and nearest-enemy targeting.
- Split GameWorld faint/recovery and battle-position restore flows into `src/game/world/GameWorldRecovery.cpp` with targeted lifecycle regression tests.
- Split GameWorld leech-seed status/config/tick logic into `src/game/world/GameWorldLeechSeed.cpp` and added contract+clamp+invalid-state regression coverage.
- Split ScriptAPI world-state/economy/mode surface into `src/game/scripting/ScriptAPIWorldState.cpp` and expanded contract coverage for money/items/mode/video/shop behavior.
- Split ScriptAPI command queue/dispatch path into `src/game/scripting/ScriptAPICommands.cpp` and expanded contract coverage for queued energy command semantics.
- Split ScriptAPI combat/damage flow into `src/game/scripting/ScriptAPICombat.cpp` and added regression coverage for deferred hit-frame damage and mid-cycle request locking.
- Split GameWorld rendering + move-impact VFX routing into `src/game/world/GameWorldRender.cpp` and added ScriptAPI combat-balance multiplier contract coverage.
- Split GameWorld update/animation tick + render-VFX lifecycle into `src/game/world/GameWorldAnimation.cpp` and expanded ScriptAPI contract coverage for attack readiness and min-request timing queries.
- Split move-impact/VFX routing into `src/game/world/GameWorldMoveImpact.cpp` and isolated render-VFX update lifecycle in `src/game/world/GameWorldVfx.cpp` to reduce `GameWorldRender` and `GameWorldAnimation` churn risk.
- Moved runtime orchestration (`GameApp`, `GameRunner`, `GameRuntime`, `GameBootstrap`, `GamePreload`, `GameSession`, `GameUpdateGraph`) into `src/game/runtime/` to reduce `src/game/` top-level sprawl.
- Converted `ShopSystem` from placeholder UI stubs into a phase-driven deterministic offer service and added dedicated phase/roll contract coverage.
- Split `ModelFastGltfLoader` into `ModelFastGltfLoader.cpp` and `ModelFastGltfLoaderHelpers.cpp` and added a source-modularity budget test to guard against re-growth.
- Split fastgltf scene/skin/animation extraction into `src/engine/render/gltf/ModelFastGltfSceneData.cpp` to further reduce `ModelFastGltfLoader.cpp` churn surface.
- Split fastgltf material interpretation and GPU texture upload into `src/engine/render/gltf/ModelFastGltfMaterial.cpp` to remove duplicated GL upload blocks from the core loader flow.
- Split fastgltf texture decode/data-source handling into `src/engine/render/gltf/ModelFastGltfTextures.cpp` and reduced `ModelFastGltfLoaderHelpers.cpp` to general utility/accessor reader responsibilities.
- Extracted D3D12 texture upload/mipmap staging from `src/engine/render/D3D12RenderBackend.cpp` into `src/engine/render/d3d12/D3D12TextureUpload.cpp` and moved D3D12 runtime probe files to `src/game/runtime/d3d12/` to reduce backend-specific top-level sprawl.
- Extended backend model-cache/runtime draw metadata contracts so indexed textured submeshes carry alpha mode/cutoff and wrap modes into D3D12 world shading, reducing hidden GL-vs-D3D material behavior drift.
- Added explicit D3D12 world blend pipeline/pass ordering to match OpenGL-style alpha depth-write behavior and reduce transparent submesh self-occlusion artifacts.
- Set indexed backend model submission to full-mesh by default (with env opt-out) to remove budget-driven geometry loss as a visual parity blocker; residual debt is performance tuning rather than geometry correctness.
- Fixed backend indexed textured alpha handling to avoid double-applying texture alpha (triangle pre-opacity plus pixel texture alpha), which was causing BLEND submesh dropout and missing textured regions during animation/movement.
- Added backend model-cache diagnostics (`PAC_BACKEND_MODEL_VERBOSE` and one-shot runtime miss logs) to make cache-load/render failures observable instead of silent.
- Fixed D3D12 world mesh upload-buffer hazards by adding per-frame world vertex/index offsets (preventing draw-data overwrite between multiple model draws in one frame).

