# 🧩 Pokemon Autochess

A **custom 3D game engine** and prototype auto-battler inspired by chess, real-time tactics, and the Pokémon universe.

Built in **C++17** using modern rendering, math, and architecture techniques — from scratch.

---

## 🛠️ Tech Stack

This engine is hand-built with modern C++ and real-time rendering in mind:

- **SDL2** – windowing, input, and OpenGL context creation
- **OpenGL 3.3 Core** – GPU-based real-time rendering
- **GLAD** – OpenGL function loader
- **GLSL** – vertex/fragment shaders
- **GLM** – matrix/vector math (camera, transforms)
- **CMake** – cross-platform build system
- **vcpkg** – dependency management

### 🧩 Planned Integrations
- **EnTT** – ECS architecture for game logic
- **Assimp** – 3D model loading (FBX, OBJ, etc)
- **Lua** – scripting support for units/AI
- **ImGui** – debug tools and UI
- **Bullet3** – physics (collisions, movement)

---

## 🎮 Game Concept

> Think **Teamfight Tactics** meets Pokémon, in a stylized 3D grid world.

- Players draft Pokémon and position them on a chessboard-like arena.
- Combat is automatic, based on positioning, abilities, and synergies.
- Strategy comes from placement, team composition, and ability timing.

### 🔭 Visual Style
- Fixed top-down / isometric camera (TFT-like)
- Stylized models and animations
- Vibrant battlefield with dynamic effects

---

## ✨ Features Roadmap

| Feature                     | Status      |
|----------------------------|-------------|
| ✅ OpenGL + SDL2 bootstrap  | Complete    |
| ✅ Shader pipeline + MVP    | Complete    |
| ✅ 3D camera (perspective)  | Complete    |
| ✅ Grid tile renderer       | Done        |
| 🧪 Model loading (Assimp)   | In progress |
| 🔜 ECS system (EnTT)        | Next        |
| 🔜 Draft system             | Planned     |
| 🔜 Unit AI + combat         | Planned     |
| 🔜 UI, effects, polish      | Future      |

---

## 🎓 Educational Alignment

This project supports and extends topics from **BCIT’s Bachelor of Applied Computer Science – Game Development Option**:

- ✅ Real-time rendering (OpenGL, GLSL, cameras)
- ✅ Engine structure and modular C++
- ✅ Systems architecture with ECS
- 🔜 Scripting and AI (Lua)
- 🔜 Physics, animation, and input systems

---

## 🚀 Getting Started

### Prerequisites

- **CMake** 3.21+
- **Visual Studio 2022** / `clang++` / `g++`
- **vcpkg** installed and configured

### 📦 Build Instructions

```bash
# Clone and set up vcpkg (one-time)
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.bat
./vcpkg integrate install

# Install engine dependencies
vcpkg install sdl2 glad glm

# Generate build files
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake

# Build the project
cmake --build build


## 🌀 Gameplay Flow & Round System

The game takes place in **Kanto**, combining elements of classic Pokémon progression with auto-battler mechanics similar to *Auto Chess* and *Teamfight Tactics*.

### 🎬 Game Start: Starter Selection

- Players begin in **Pallet Town** by choosing a starter: **Bulbasaur**, **Charmander**, or **Squirtle**.
- This is a **free round** with no combat — a chance to set your initial direction.
- The starter is your foundation, but early opportunities exist to pivot or evolve depending on unit offerings.

### 🔄 Round Progression

Rounds represent different Kanto locations or encounters. Each round offers a new challenge, unit choices, or key battles:

| Round | Location / Theme       | Encounter Type       | Notes |
|-------|------------------------|----------------------|-------|
| 1     | Pallet Town            | Starter Selection    | Free round – pick your starter |
| 2–3   | Route 1                | PvE: Wild Pokémon     | Early units, some synergy seeds |
| 4     | Viridian City          | PvE + Shop            | Item drops + expanded shop pool |
| 5     | Viridian Forest        | PvE: Swarm battle     | Status effects, type counters |
| 6     | Route 22 (optional)    | PvE or PvP variant    | Chance to pivot, strong wilds |
| 7     | Pewter City            | **Boss: Gym Leader** | Battle against **Brock** and his themed team |

Future rounds will follow this pattern: **travel → encounter → evolve/upgrade → major battle**.

### 🧠 Synergy System (Early Concept)

Like TFT or Auto Chess, Pokémon will gain bonuses based on shared traits — but beyond types.

#### Planned synergy categories:

- **Type Synergy** – Traditional (e.g. Fire, Water, Grass). Grants team bonuses.
- **Origin Synergy** – Based on where/how the Pokémon was acquired (starter, route, gym leader, etc).
- **Evolution Chains** – Owning connected evolutions (e.g. Charmander + Charmeleon) provides bonuses.
- **Role Synergy** – Broad tactical categories like:
  - **Tanks** – High HP/defense
  - **Speedsters** – Fast attack animations or movement
  - **Status Masters** – Use paralysis, sleep, poison, etc.
  - **Glass Cannons** – High burst damage, low durability

These synergies influence unit stats, ability cooldowns, and team-wide effects.

---

More details will be added as we build out the round engine, combat loop, and draft/shop systems.
