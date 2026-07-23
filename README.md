# Pokemon Autochess

A custom 3D engine plus game prototype for a Pokemon-inspired auto-battler (grid placement -> scripted combat).

This repo is engine-first. The engine is built to be reusable for future games; the current game is one client.

> This repository is an educational/prototype project; it is not affiliated with Nintendo/Game Freak/The Pokemon Company.

---

## Current Status
- Engine core: application loop, windowing, input mapping, system registry
- Rendering: shared gameplay presentation path with OpenGL, Vulkan, and D3D12 backends, camera, board/grid rendering, model loading plus animation, shader plus resource caches
- UI: text rendering, cards, health bars, battle feed, boot loading/progress view
- Gameplay runtime: `GameRuntime` -> `GameSession` (world/state/systems/UI wiring)
- Game states: placement plus combat
- Gameplay systems: round, shop, movement, combat, bench/cards, unit interaction
- VFX: reusable VFX code under `src/vfx/`, game-specific VFX under `src/game/vfx/`, and preview tooling via `PAC_VfxPreviewer` plus `VfxLab`
- Tests: headless smoke tests, invariants, optional GL smoke draw, and optional runtime smoke for OpenGL/Vulkan/D3D12
- Data pipeline: JSON configs plus cooker plus packaged content bundle

---

## Tech Stack
| Area | Tech |
| --- | --- |
| Language | C++20 |
| Build | CMake, vcpkg manifest |
| Windowing plus Input | SDL2 |
| Rendering | OpenGL 3.x, Vulkan, Direct3D 12, glad, GLM |
| Scripting | Lua, sol2 |
| Data | nlohmann-json, fastgltf, stb |

---

## Repo Layout
- `src/engine/` engine core, rendering, UI, utilities, VFX
- `src/game/` game runtime, state machine, systems, scripting bindings, game-specific VFX, and game-facing preview adapters
- `src/vfx/` reusable VFX effects, runtime bridges, and reusable preview support
- `scripts/` Lua gameplay logic
- `assets/` runtime assets
- `tests/` headless tests and invariants
- `tools/` offline tools, build scripts, and installer
- `docs/` active engineering docs plus archived historical plans

---

## Getting Started (Windows)
Requirements:
- Visual Studio 2026 Build Tools (project default), or the tested Visual Studio 2022 Build Tools fallback
- CMake 3.22+ (presets currently expect 4.2)
- vcpkg installed and `VCPKG_ROOT` set
- Ninja when using the optional MSVC/Ninja presets

Build with presets:

```powershell
cmake --preset vs2026
cmake --build --preset debug
cmake --build --preset release
```

On a machine with Visual Studio 2022 Build Tools, use:

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
```

For faster command-line iteration, open a Visual Studio Developer PowerShell
or Developer Command Prompt and use the same MSVC toolchain through Ninja:

```powershell
cmake --preset ninja-msvc
cmake --build --preset ninja-debug
```

Manual configure:

```powershell
cmake -S . -B build `
  -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Debug
```

Notes:
- Assets under `assets/` are used at runtime and expected to be present.
- Dependencies, including Vulkan headers/loader and the shader compiler, are defined in `vcpkg.json` (manifest mode). A Vulkan-capable display driver is still required at runtime.
- Ninja is only the build executor; vcpkg remains the dependency manager.

---

## Build Targets
| Target | What It Builds | Command |
| --- | --- | --- |
| `engine_core` | Headless engine core library | `cmake --build build --config Debug --target engine_core` |
| `engine_platform` | SDL/window/input layer | `cmake --build build --config Debug --target engine_platform` |
| `engine_render` | Rendering plus UI plus resources | `cmake --build build --config Debug --target engine_render` |
| `PAC_GameObjects` | Game runtime library (shared by exe plus tests) | `cmake --build build --config Debug --target PAC_GameObjects` |
| `PokemonAutochess` | Game executable | `cmake --build build --config Debug --target PokemonAutochess` |
| `PAC_VfxPreviewer` | Game-facing VFX preview tool | `cmake --build build --config Debug --target PAC_VfxPreviewer` |
| `VfxLab` | Reusable VFX lab tool | `cmake --build build --config Debug --target VfxLab` |
| `PAC_Tests` | Tests executable | `cmake --build build --config Debug --target PAC_Tests` |
| `PAC_All` | Convenience aggregate (engine plus game plus tests) | `cmake --build build --config Debug --target PAC_All` |

---

## VFX Tools
```powershell
.\build\Debug\PAC_VfxPreviewer.exe
.\build\Debug\VfxLab.exe
```

Use `PAC_VfxPreviewer` when the effect needs real board constraints, Pokemon
models, or attack-animation timing. Use `VfxLab` for reusable VFX that should
stay isolated from game-specific preview composition.

---

## Run
```powershell
.\build\Debug\PokemonAutochess.exe
```

Select Vulkan for a one-off launch without changing the saved Display setting:

```powershell
$env:PAC_RENDER_BACKEND = "vulkan"
.\build\Debug\PokemonAutochess.exe
```

The Display menu can also save Vulkan as the backend for the next launch. See
`docs/VULKAN_BACKEND.md` for its current feature and optimization scope.

When running from `dist/Release` during development, sync runtime content first so `config/` changes are reflected:

```powershell
.\tools\sync_runtime_content.ps1 -OutDir dist/Release -Folders config
```

---

## Debug State Snapshots
Use the debug snapshot hotkeys during gameplay:

- `F5` saves the current debug state snapshot
- `F9` loads the current debug state snapshot

By default the snapshot is written to:

```text
data/config/user/debug_state_snapshot.json
```

Override the snapshot path with:

```powershell
$env:PAC_DEBUG_STATE_PATH="C:\path\to\debug_state_snapshot.json"
```

Auto-load that snapshot on startup with:

```powershell
$env:PAC_AUTO_LOAD_DEBUG_SNAPSHOT="1"
```

This is useful for automated benchmark runs that need to start from a fixed
gameplay scene without manual menu input.

---

## Tests
Build and run the test executable:

```powershell
cmake --build build --config Debug --target PAC_Tests
.\build\Debug\PAC_Tests.exe
```

Run tests via CTest:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Optional GL smoke test (real model draw):

```powershell
$env:PAC_TEST_GL=1
ctest --test-dir build -C Debug --output-on-failure -R render_pipeline_smoke
```

Optional override for the model used in GL smoke:

```powershell
$env:PAC_TEST_GL=1
$env:PAC_TEST_MODEL="models/0004_Charmander.glb"
ctest --test-dir build -C Debug --output-on-failure -R render_pipeline_smoke
```

CI runs build plus tests plus data validation on Windows.

## Demo Media Capture
Automated screenshot and video capture for Phlosion/readme demos lives in:

```bash
./tools/capture_demo_media.py screenshots
./tools/capture_demo_media.py videos
```

See `docs/DEMO_MEDIA_CAPTURE.md` for scene names, GPU-machine video settings,
and dependency notes.

---

## Data Pack (Release)
```powershell
cmake --build build --config Release --target PokemonAutochess
cmake --build build --config Release --target PAC_ValidateData
cmake --build build --config Release --target PAC_PackData
```

Or bundle everything with one command:

```powershell
.\tools\release_bundle.ps1
```

Installer script (Inno Setup):
- `tools/PokemonAutochessInstaller.iss`

Build installer (headless):

```powershell
.\tools\build_installer.ps1 -Bundle
```

One-command release plus installer (clean Windows clone) requirements:
- Visual Studio 2026 or newer
- CMake 3.22+ (presets currently expect 4.2)
- vcpkg installed and `VCPKG_ROOT` set
- Inno Setup 6 (provides `ISCC.exe`)

```powershell
$env:VCPKG_ROOT="C:\path\to\vcpkg"
.\tools\build_installer.ps1 -Bundle
```

Notes:
- The installer is written to `dist/installer/PokemonAutochessSetup.exe`.
- If `ISCC.exe` is not on PATH, pass `-ISCCPath "C:\Path\To\ISCC.exe"`.

Ship these artifacts:
- `PokemonAutochess.exe`
- `content_pak/content.pak`
- `assets/`
- `config/`
- `scripts/`
- Required runtime DLLs from your build environment

---

## Gameplay Prototype
Flow (current shape):
1. Placement phase (starter/unit placement on grid)
2. Combat phase (movement plus combat systems; combat logic driven by Lua)
3. Round/shop systems integrate with the session loop (UI plus events)

Systems present in code:
- Round system
- Shop system (Lua-driven roll/price hooks)
- Movement plus combat systems
- Bench/cards systems and UI support (cards, battle feed, health bars)

The exact balancing/content is prototype-level and expected to change.

---

## Lua Scripting
Lua is used for gameplay logic and is bound host-side via sol2.

Notable scripts:
- `scripts/systems/combat.lua` for combat timing and tuning hooks
- `scripts/systems/card_shop.lua` for classic shop roll logic and UI/debug events
- `scripts/states/` for state flow and phase transitions

Tuning:
- `scripts/config/combat_tuning.lua` can override combat timing and speed

---

## Assets and Model Pipeline
- Runtime assets live under `assets/`.
- Canonical runtime mesh assets belong under `assets/meshes/`.
- Canonical runtime texture assets belong under `assets/textures/`.
- `assets/vfx/` is for reusable/reference VFX assets, not the default runtime
  landing zone once a mesh or texture path is referenced directly by code or
  config.
- Models and animations are ingested via glTF (`fastgltf`).
- Per-model animation sets are defined in `assets/models/*.animset.json`.

Expect this pipeline to evolve as additional models and animations are added.

---

## Debugging and Combat Trace Gating
Tracing is controlled by environment variables (see `src/game/logging/DebugTrace.h`).

- `PAC_TRACE_ALL=1` enables all combat traces.
- `PAC_TRACE_COMBAT="unit:move,unit2:move2"` enables selective traces.
- Token format is `unit:move` and either side may be `*`.
- A token without `:` is treated as `unit:*`.

Examples:
- `PAC_TRACE_COMBAT="bulbasaur:vine_whip"`
- `PAC_TRACE_COMBAT="*:vine_whip"`
- `PAC_TRACE_COMBAT="bulbasaur:*"`
- `PAC_TRACE_COMBAT="*:*,pikachu:thunder_shock"`

---

## Roadmap (Suggested)
Near-term:
- Keep steady-state renderer work focused on shared runtime frame cost (`docs/RENDERER_PARITY_ROADMAP.md`)
- Improve renderer instrumentation (CPU build/submit/present plus GPU frame timing)
- Reduce heavy combat render cost on target laptop hardware
- Remove user-visible first-use stalls without hurting runtime performance
- Increase test coverage for scripting and config ingestion

Longer-term:
- Move the existing benchmark and screenshot-parity matrices onto a
  representative self-hosted performance/GPU runner
- More polished UI and effects pass
- More complete auto-battler loop (economy, drafting, synergies)

---

## Build Flags and Options
CMake options:
- `PAC_VERBOSE_STARTUP` enables verbose startup/model-load logging
- `PAC_BUILD_TOOLS` toggles developer tools like the data cooker
- `PAC_ENABLE_WARNINGS` enables project warning flags (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` otherwise)
- `PAC_WARNINGS_AS_ERRORS` upgrades warnings to errors (`/WX` or `-Werror`)
  - Defaults to `ON` in CI when the `CI` environment variable is present/non-zero

Example:

```powershell
cmake --preset vs2026 -DPAC_VERBOSE_STARTUP=ON
```

Strict local quality gate example:

```powershell
cmake --preset vs2026 -DPAC_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug --target PAC_Tests
```

---

## Notes
- Windows-first development today; Ubuntu support is a possible future goal.
- See `docs/` for internal quality notes and test plans.
