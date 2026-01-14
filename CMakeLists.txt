cmake_minimum_required(VERSION 4.2)

# NOTE:
# Do NOT set CMAKE_TOOLCHAIN_FILE here.
# Toolchains must be provided at configure time (e.g., via CMakePresets.json or -DCMAKE_TOOLCHAIN_FILE=...).

project(PokemonAutochess)

option(PAC_VERBOSE_STARTUP "Enable verbose startup/model-load logging" OFF)

# NEW: If ON, tinygltf is not found, not included, and not compiled.
option(PAC_DISALLOW_TINYGLTF "Disallow tinygltf entirely (no headers, no TU, no fallback)" ON)

# Force C++20 (Model.cpp requires it on MSVC)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# fastgltf (vcpkg). Target is typically fastgltf::fastgltf, and it pulls simdjson as a dependency in vcpkg.
find_package(fastgltf CONFIG REQUIRED)

# sol2 (vcpkg)
find_package(sol2 CONFIG REQUIRED)
if (TARGET sol2::sol2)
    set(PAC_SOL2_TARGET sol2::sol2)
elseif (TARGET sol2)
    set(PAC_SOL2_TARGET sol2)
else()
    message(FATAL_ERROR "sol2 package found, but no known CMake target (sol2::sol2 or sol2) is available.")
endif()

# Project include root
set(PROJECT_SRC_ROOT "${CMAKE_SOURCE_DIR}/src")

# ---- tinygltf (optional, guarded) ----
set(TINYGLTF_INCLUDE_DIRS "")
if (NOT PAC_DISALLOW_TINYGLTF)
    # tinygltf (vcpkg): header-only; use include-path lookup (NOT find_package CONFIG).
    find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
    if (NOT TINYGLTF_INCLUDE_DIRS)
        message(FATAL_ERROR "tinygltf headers not found (tiny_gltf.h). Is vcpkg dependency 'tinygltf' installed?")
    endif()
endif()

# ---------------- Engine (static lib) ----------------

# Build Engine sources list so we can conditionally add TinyGltfImpl.cpp
set(ENGINE_SOURCES
    # Engine Core
    src/engine/core/Application.cpp
    src/engine/core/Window.cpp
    src/engine/core/SystemRegistry.cpp

    # Engine Utils
    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderLibrary.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp

    # Engine Render
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/GLTFAccessors.cpp
    src/engine/render/Model.cpp

    # Step 1: fastgltf compile smoke test (no runtime behavior change)
    src/engine/render/FastGltfSmokeTest.cpp

    # Engine UI
    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp

    # NEW: boot loading screen (Option B)
    src/engine/ui/BootLoadingView.cpp
)

# Only compile tinygltf implementation TU when tinygltf is allowed.
if (NOT PAC_DISALLOW_TINYGLTF)
    list(APPEND ENGINE_SOURCES src/engine/render/TinyGltfImpl.cpp)
endif()

add_library(Engine STATIC ${ENGINE_SOURCES})

target_include_directories(Engine
    PUBLIC
        "${PROJECT_SRC_ROOT}"
)

# Only add tinygltf include path when allowed.
if (NOT PAC_DISALLOW_TINYGLTF)
    target_include_directories(Engine PUBLIC "${TINYGLTF_INCLUDE_DIRS}")
endif()

target_link_libraries(Engine PRIVATE
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    OpenGL::GL
    glad::glad
    glm::glm
    lua
    nlohmann_json::nlohmann_json
    fastgltf::fastgltf
    ${PAC_SOL2_TARGET}
)

# Ensure MSVC actually uses C++20 mode (older CMake/MSVC combos can ignore CMAKE_CXX_STANDARD)
if (MSVC)
    target_compile_options(Engine PRIVATE /std:c++20 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(Engine PRIVATE -std=c++20)
endif()

# Apply compile defs AFTER the target exists
target_compile_definitions(Engine PRIVATE
    PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
    PAC_DISALLOW_TINYGLTF=$<BOOL:${PAC_DISALLOW_TINYGLTF}>
)

# ---------------- Game (exe) ----------------
add_executable(PokemonAutochess
    main.cpp

    # Game Core
    src/game/GameWorld.cpp
    src/game/GameStateManager.cpp
    src/game/LuaBindings.cpp
    src/game/LuaScript.cpp
    src/game/PokemonConfigLoader.cpp
    src/game/GameConfig.cpp
    src/game/LogBus.cpp

    # NEW: Moves config loader
    src/game/MovesConfigLoader.cpp

    # Game Systems
    src/game/systems/CameraSystem.cpp
    src/game/systems/UnitInteractionSystem.cpp
    src/game/systems/RoundSystem.cpp
    src/game/systems/CardSystem.cpp
    src/game/systems/BenchSystem.cpp
    src/game/systems/MovementSystem.cpp
    src/game/systems/CombatSystem.cpp
    src/game/systems/ShopSystem.cpp

    # Game UI
    src/game/ui/CardFactory.cpp

    # States
    src/game/state/PlacementState.cpp
    src/game/state/CombatState.cpp

    # Lua-first generic state wrapper
    src/game/ScriptedState.cpp
)

target_include_directories(PokemonAutochess
    PRIVATE
        "${PROJECT_SRC_ROOT}"
)

target_link_libraries(PokemonAutochess PRIVATE
    Engine
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    OpenGL::GL
    glad::glad
    glm::glm
    lua
    ${PAC_SOL2_TARGET}
)

if (MSVC)
    target_compile_options(PokemonAutochess PRIVATE /std:c++20 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(PokemonAutochess PRIVATE -std=c++20)
endif()

target_compile_definitions(PokemonAutochess PRIVATE
    PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
    PAC_DISALLOW_TINYGLTF=$<BOOL:${PAC_DISALLOW_TINYGLTF}>
)

# Copy shaders alongside the binary
file(COPY assets/shaders/engine/default.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/default.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/grid.vert   DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/grid.frag   DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/model/model.vert   DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/model/model.frag   DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/card.vert       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/card.frag       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/text.vert       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/text.frag       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/healthbar.vert  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/healthbar.frag  DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)

# Windows entrypoint
set_target_properties(PokemonAutochess PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup")
