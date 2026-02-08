# ?? Pokemon Autochess

A **custom 3D engine + game prototype** for a Pokémon-inspired auto-battler (grid placement ? scripted combat).

This repo is **engine-first**. The engine is built to be reusable for future games; the current game is one client.

> This repository is an educational/prototype project; it is not affiliated with Nintendo/Game Freak/The Pokémon Company.

---

## ? Current Status
- Engine core: application loop, windowing, input mapping, system registry
- Rendering: camera, board/grid rendering, model loading + animation support, shader + resource caches
- UI: text rendering, cards, health bars, battle feed, boot loading/progress view
- Gameplay runtime: `GameRuntime` ? `GameSession` (world/state/systems/UI wiring)
- Game states: placement + combat
- Gameplay systems: round, shop, movement, combat, bench/cards, unit interaction
- VFX: particle system + TailFire VFX modules
- Tests: headless smoke tests and invariants
- Data pipeline: JSON configs + cooker + packaged content bundle

---

## ??? Tech Stack
| Area | Tech |
| --- | --- |
| Language | C++20 |
| Build | CMake, vcpkg manifest |
| Windowing + Input | SDL2 |
| Rendering | OpenGL 3.x, glad, GLM |
| Scripting | Lua, sol2 |
| Data | nlohmann-json, fastgltf, stb |

---

## ?? Repo Layout
- `src/engine/` engine core, rendering, UI, utilities, VFX
- `src/game/` game runtime, state machine, systems, scripting bindings
- `scripts/` Lua gameplay logic
- `assets/` runtime assets
- `tests/` headless tests and invariants
- `tools/` offline tools (data cooker)
- `docs/` internal plans and quality notes

---

## ?? Getting Started (Windows)
Requirements
- Visual Studio 2026 or newer
- CMake 3.22+ (presets currently expect 4.2)
- vcpkg installed and `VCPKG_ROOT` set

Build with presets

```powershell
cmake --preset vs2026
cmake --build --preset debug
cmake --build --preset release
```

Manual configure

```powershell
cmake -S . -B build ^
  -G "Visual Studio 18 2026" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

cmake --build build --config Debug
```

---

## ?? Build Targets
| Target | What It Builds | Command |
| --- | --- | --- |
| `engine_core` | Headless engine core library | `cmake --build build --config Debug --target engine_core` |
| `engine_platform` | SDL/window/input layer | `cmake --build build --config Debug --target engine_platform` |
| `engine_render` | Rendering + UI + resources | `cmake --build build --config Debug --target engine_render` |
| `PAC_GameObjects` | Game runtime library (shared by exe + tests) | `cmake --build build --config Debug --target PAC_GameObjects` |
| `PokemonAutochess` | Game executable | `cmake --build build --config Debug --target PokemonAutochess` |
| `PAC_Tests` | Tests executable | `cmake --build build --config Debug --target PAC_Tests` |
| `PAC_All` | Convenience aggregate (engine + game + tests) | `cmake --build build --config Debug --target PAC_All` |

---

## ?? Run
```powershell
.\build\Debug\PokemonAutochess.exe
```

---

## ?? Tests
Build and run the test executable

```powershell
cmake --build build --config Debug --target PAC_Tests
.\build\Debug\PAC_Tests.exe
```

You can also run via CTest

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

CI runs build + tests + data validation on Windows.

---

## ?? Data Pack (Release)
```powershell
cmake --build build --config Release --target PokemonAutochess
cmake --build build --config Release --target PAC_ValidateData
cmake --build build --config Release --target PAC_PackData
```

Ship these artifacts
- `PokemonAutochess.exe`
- `content_pak/content.pak`
- `assets/`
- Required runtime DLLs from your build environment

---

## ?? Design Goals
- Reusable engine modules with strict layering
- Game-owned loop and composition root
- Headless simulation for tests and tooling
- Data-driven gameplay via JSON + cooker

---

## ?? Notes
- Windows-first development today; Ubuntu support is a possible future goal.
- See `docs/` for internal quality notes and test plans.
