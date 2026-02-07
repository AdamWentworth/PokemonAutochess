# 🧩 Pokemon Autochess

A **custom 3D engine + game prototype** for a Pokémon-inspired auto-battler (grid placement → scripted combat).

- Engine + game code in modern **C++20**
- Rendering via **OpenGL 3.x** + **SDL2**
- Gameplay systems + combat/shop logic scripted in **Lua** (bound via **sol2**)

> This repository is an educational/prototype project; it is not affiliated with Nintendo/Game Freak/The Pokémon Company.

---

## ✅ Current Status (What’s Implemented)

- **Engine core**: application loop, windowing, input mapping, system registry
- **Rendering**: camera, board/grid rendering, model loading + animation support, shader + resource caches
- **UI**: text rendering, cards, health bars, battle feed, boot loading/progress view
- **Gameplay runtime**: `GameRuntime` (thin) → `GameSession` (world/state/systems/UI wiring)
- **Game states**: placement + combat states
- **Gameplay systems**: round system, shop system, movement, combat, bench/cards, unit interaction
- **VFX**: particle system + TailFire VFX modules
- **Tests**: small executable with smoke tests

---

## 🛠️ Tech Stack

### Core
- **C++20** (project-wide standard)
- **CMake** (targets: `Engine`, `PAC_GameObjects`, `PokemonAutochess`, `PAC_Tests`)
- **vcpkg manifest mode** via `vcpkg.json`

### Runtime + Rendering
- **SDL2** + **SDL2_ttf** (windowing/input + font rendering)
- **OpenGL** + **glad** (GL loader)
- **GLM** (math)

### Scripting + Data
- **Lua** + **sol2** (Lua bindings)
- **nlohmann-json** (config/data parsing)
- **fastgltf** (glTF ingestion)
- **stb** (image IO helpers)

---

## 📁 Repo Layout (High-Level)

- `src/engine/` — engine core, rendering, UI, utilities, VFX
- `src/game/` — game app/world, state machine, systems, scripting bindings, configs
- `scripts/` — Lua gameplay logic (combat, shop, etc.)
- `assets/` — runtime assets copied into the build output on build
- `tests/` — lightweight smoke tests (`PAC_Tests`)

---

## 🚀 Getting Started

### ✅ Build Requirements

**Recommended (Windows)**
- Visual Studio 2026 (uses **Visual Studio 18 2026** generator preset)
- CMake (supports presets; the repo includes `CMakePresets.json`)
- vcpkg installed locally
- `VCPKG_ROOT` environment variable set to your vcpkg directory

---

### 🧰 Build (Using CMake Presets)

From the repo root:

    # Configure (uses CMakePresets.json)
    cmake --preset vs2026

    # Build Debug
    cmake --build --preset debug

    # Build Release
    cmake --build --preset release

Notes:
- Assets under `assets/` are copied into the build directory automatically as part of the build.
- Dependencies are defined in `vcpkg.json` (manifest mode). If your vcpkg is configured correctly, CMake + the toolchain will resolve them.

---

### 🏗️ Build (Manual Configure Command)

If you prefer explicit flags instead of presets:

    cmake -S . -B build ^
      -G "Visual Studio 18 2026" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake

    cmake --build build --config Debug

---

### 🎯 Build Targets (What Each Command Builds)

By default, `cmake --build build --config Debug` builds **everything** in the default target (Visual Studio uses `ALL_BUILD`).
If you want a specific piece, use the named targets below.

| Target | What It Builds | Command |
| --- | --- | --- |
| `engine_core` | Headless engine core library | `cmake --build build --config Debug --target engine_core` |
| `engine_platform` | SDL/window/input layer | `cmake --build build --config Debug --target engine_platform` |
| `engine_render` | Rendering + UI + resources | `cmake --build build --config Debug --target engine_render` |
| `PAC_GameObjects` | Game runtime library (shared by exe + tests) | `cmake --build build --config Debug --target PAC_GameObjects` |
| `PokemonAutochess` | The game executable | `cmake --build build --config Debug --target PokemonAutochess` |
| `PAC_Tests` | The tests executable | `cmake --build build --config Debug --target PAC_Tests` |
| `PAC_All` | Convenience aggregate (engine + game + tests) | `cmake --build build --config Debug --target PAC_All` |

---

### ▶️ Run

After building:

    # Windows example
    .\build\Debug\PokemonAutochess.exe

You can also open the generated solution in Visual Studio and run via **F5**.

---

### 🧪 Tests

Build and run the test executable:

    cmake --build build --config Debug --target PAC_Tests
    .\build\Debug\PAC_Tests.exe

These are intended as **minimal smoke checks** (Lua bindings, event bus, config parsing, etc.).

---

## 🎮 Gameplay Prototype

### 🔄 Flow (Current Shape)
1. **Placement** phase (starter/unit placement on grid)
2. **Combat** phase (movement + combat systems; combat logic driven by Lua script)
3. **Round/Shop** systems integrate with the session loop (UI + events)

### 🧠 Systems Present in Code
- Round system
- Shop system (Lua-driven roll/price hooks)
- Movement + combat systems
- Bench/cards systems and UI support (cards, battle feed, health bars)

> The exact balancing/content is still prototype-level; expect changes.

---

## 🌀 Lua Scripting

Lua is used for gameplay logic and is bound host-side via **sol2**.

### 📜 Notable scripts
- `scripts/systems/combat.lua`
  - Contains combat timing + tuning hooks and debug tracing gates
- `scripts/systems/shop.lua` (or equivalent shop script in `scripts/systems/`)
  - Implements shop roll logic and emits events for UI/debug

### 🎛️ Tuning
Combat timing/speed values can be overridden by loading:
- `scripts/config/combat_tuning.lua` (if present)

---

## 🎨 Assets & Model Pipeline

- Runtime `assets/` directory is copied into the build output each build (incremental).
- Models/animations are handled via:
  - **glTF** ingestion (`fastgltf`)
  - Per-model animation-set JSON (example: `assets/models/*.animset.json`)

Expect this pipeline to evolve as additional models/animations are added.

---

## 🧭 Debugging & Combat Trace Gating

Tracing is controlled by environment variables (matches the rules implemented in `src/game/logging/DebugTrace.h`):

- `PAC_TRACE_ALL=1`
  - Enables all combat traces.

- `PAC_TRACE_COMBAT="unit:move,unit2:move2"`
  - Comma/semicolon/whitespace-separated tokens.
  - Token format: `unit:move` where either side may be `*` (wildcard).
  - If a token has no `:`, it is treated as a unit-only match (`move="*"`).

### ✅ Examples
- `PAC_TRACE_COMBAT="bulbasaur:vine_whip"`
- `PAC_TRACE_COMBAT="*:vine_whip"`
- `PAC_TRACE_COMBAT="bulbasaur:*"`
- `PAC_TRACE_COMBAT="*:*,pikachu:thunder_shock"`

### 📌 Files using these gates
- `scripts/systems/combat.lua` (Lua-side `emit()` debug prints)
- `src/game/GameWorld.cpp` (animation tick/clamp logs)
- `src/game/scripting/LuaBindings.cpp` (damage/apply tracing)

---

## ✨ Roadmap (Suggested)

### 🔜 Near-term
- Expand content/config coverage (more units/moves/rounds)
- Stabilize the Lua ↔ C++ gameplay API surface
- Improve model/animation streaming + caching
- Increase test coverage for scripting + config ingestion

### 🧩 Longer-term
- More polished UI + effects pass
- More complete auto-battler loop (economy, drafting, synergies)

---

## ⚙️ Build Flags / Options

CMake option(s) present:
- `PAC_VERBOSE_STARTUP` — enables verbose startup/model-load logging

Example:
    cmake --preset vs2026 -DPAC_VERBOSE_STARTUP=ON
