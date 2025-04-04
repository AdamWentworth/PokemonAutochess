# Pokemon Autochess

A custom-built C++ game engine and prototype for a **3D auto-battler** inspired by the mechanics of chess and the world of Pokémon.

## 🛠️ Tech Stack

This project is built from scratch using modern C++17 and focuses on real-time rendering and engine architecture fundamentals:

- **SDL2** — windowing, input, and OpenGL context creation
- **OpenGL 3.3 Core** — cross-platform real-time 3D rendering
- **GLAD** — OpenGL function loader
- **GLSL** — shader language for GPU programs
- **CMake + vcpkg** — dependency management & builds
- **GLM** — math library for matrices, transforms, and cameras *(now integrated)*
- *(soon)* **EnTT** — ECS for flexible game logic
- *(future)* Lua, Bullet3, ImGui, ImGuizmo, etc.

## 🎮 Game Concept

Players draft Pokémon to a battlefield and place them on a chessboard-like 3D grid. After setup, units auto-battle each other based on abilities and synergy. The player wins rounds by out-positioning and out-strategizing their opponent.

### Visual Style
- Isometric-ish top-down camera (like **TFT**)
- Fixed or orbiting 3D camera
- Stylized models with idle/battle animations

### Core Features (Planned)
- 3D Grid-based battlefield
- Perspective camera with zoom/pan
- Unit synergy and evolution
- Draft + bench system
- Round-based combat system

## 🎓 BCIT Game Dev Curriculum Alignment

This project aligns with BCIT’s **Bachelor of Applied Computer Science – Game Dev Option**:

- Real-time graphics (OpenGL, GLSL, GLM)
- ECS design (EnTT)
- C++ engine architecture
- Modular design using CMake + vcpkg
- Optional scripting & AI systems in Lua
- Scene management, entity systems, rendering pipelines

## 🧪 Development Status

### ✅ Completed
- Triangle rendering
- GLAD + OpenGL + SDL2 setup
- CMake + vcpkg integration
- Shader system and renderer abstraction
- GLM perspective camera support

### 🔨 In Progress
- Board renderer (chess tiles)
- Model loading (Assimp)
- ECS for units and systems

---

## 🚀 Getting Started

### Requirements
- CMake 3.21+
- Visual Studio 2022 / g++ / clang
- vcpkg (dependencies auto-managed)

### Build Instructions

```bash
# One-time vcpkg setup
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.bat
./vcpkg integrate install

# Install dependencies
vcpkg install sdl2 glad glm

# Build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build
