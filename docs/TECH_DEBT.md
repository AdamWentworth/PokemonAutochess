# Tech Debt and Code Smells

This list is intentionally short and focused on the highest-value fixes.

Open items
- Monolithic gameplay/render files remain high-risk for regressions:
  - `src/game/GameWorld.cpp` (~1700+ lines)
  - `src/engine/render/ModelFastGltfLoader.cpp` (~1100+ lines)
  - `src/game/scripting/ScriptAPI.cpp` (~900+ lines)
- `ShopSystem` is still in placeholder mode (`TEMP` stubs for UI/input paths).
- Packaged build smoke run is manual only (no automated run in CI).
- Full fresh-machine validation of the installer flow (no cached vcpkg/toolchain).

Recent improvements
- Removed duplicate CTest name maintenance by deriving CTest registration directly from `tests/TestMain.cpp`.
- Added targeted config-loader regression tests for Pokemon, Evolution, and Flyer parsing contracts.
- Reduced repeated case-normalization logic in `PokemonConfigLoader` and `EvolutionConfigLoader`.
- Normalized source comments/strings to ASCII in `src/` and added `source_ascii_hygiene` regression test.
- Split GameWorld progression/merge/XP logic into `src/game/GameWorldProgression.cpp` and added merge progression regression coverage.
- Split GameWorld spawn/bench roster flow into `src/game/GameWorldRoster.cpp` with targeted spawn+bench regression coverage.
- Split GameWorld economy/inventory/healing flows into `src/game/GameWorldEconomy.cpp` and added income, inventory, and reset regression coverage.
- Split GameWorld capture attempt lifecycle into `src/game/GameWorldCapture.cpp` and added capture precondition/success/failure recovery regression coverage.
- Split GameWorld query/synergy helpers into `src/game/GameWorldQueries.cpp` and added regression coverage for type-line counting and nearest-enemy targeting.
- Split GameWorld faint/recovery and battle-position restore flows into `src/game/GameWorldRecovery.cpp` with targeted lifecycle regression tests.
- Split GameWorld leech-seed status/config/tick logic into `src/game/GameWorldLeechSeed.cpp` and added contract+clamp+invalid-state regression coverage.
- Split ScriptAPI world-state/economy/mode surface into `src/game/scripting/ScriptAPIWorldState.cpp` and expanded contract coverage for money/items/mode/video/shop behavior.
