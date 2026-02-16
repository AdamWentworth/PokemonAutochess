# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

Open items
- Monolithic gameplay/render files remain high-risk for regressions:
  - `src/engine/render/gltf/ModelFastGltfLoaderHelpers.cpp` (~500+ lines)
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

