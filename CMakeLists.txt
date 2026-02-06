# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(PokemonAutochess LANGUAGES CXX)

option(PAC_VERBOSE_STARTUP "Enable verbose startup/model-load logging" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- Dependencies ----------------
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

find_package(sol2 CONFIG REQUIRED)
if (TARGET sol2::sol2)
    set(PAC_SOL2_TARGET sol2::sol2)
elseif (TARGET sol2)
    set(PAC_SOL2_TARGET sol2)
else()
    message(FATAL_ERROR "sol2 package found, but no known CMake target (sol2::sol2 or sol2) is available.")
endif()

set(PROJECT_SRC_ROOT "${CMAKE_SOURCE_DIR}/src")

# ---------------- Common target settings ----------------
function(pac_apply_common_target_settings target_name)
    get_target_property(_type ${target_name} TYPE)

    if (_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${target_name}
            INTERFACE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            INTERFACE
                PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )
        # Don't apply compile options to INTERFACE targets here (unnecessary).
    else()
        target_include_directories(${target_name}
            PRIVATE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            PRIVATE
                PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )

        if (MSVC)
            target_compile_options(${target_name} PRIVATE /std:c++20 /permissive- /Zc:__cplusplus)
        else()
            target_compile_options(${target_name} PRIVATE -std=c++20)
        endif()
    endif()
endfunction()

# =====================================================================
# STAGE 1 TRANSITION: split monolithic Engine into engine_* libraries
#
# Goal:
# - engine_core contains no SDL/OpenGL dependencies (headless-capable)
# - engine_platform contains SDL/window/input
# - engine_render contains renderer + GL/graphics
#
# This is an additive refactor: file moves come later. For now, we
# compile existing files into the appropriate targets by listing them
# here. Later, when you physically move files, only these lists change.
# =====================================================================

# ---------------- engine_core (no SDL/GL) ----------------
add_library(engine_core STATIC
    src/engine/core/SystemRegistry.cpp
    # New core interfaces (additive)
    src/engine/core/StdoutLogger.cpp
    src/engine/core/EventBus.cpp
)

target_sources(engine_core PRIVATE
    # headers (for IDEs)
    src/engine/core/SystemRegistry.h
    src/engine/core/Updatable.h
    src/engine/core/ILogger.h
    src/engine/core/StdoutLogger.h
    src/engine/core/IEventBus.h
    src/engine/core/EventBus.h
    src/engine/core/Services.h
)

target_link_libraries(engine_core
    PRIVATE
        glm::glm
)

pac_apply_common_target_settings(engine_core)

# ---------------- engine_platform (SDL, window/input) ----------------
add_library(engine_platform STATIC
    src/engine/core/Window.cpp
    src/engine/input/SdlKeyMap.cpp
)

target_link_libraries(engine_platform
    PUBLIC
        SDL2::SDL2
        SDL2_ttf::SDL2_ttf
    PRIVATE
        glm::glm
)

pac_apply_common_target_settings(engine_platform)

# ---------------- engine_render (OpenGL + renderer + resources) ----------------
add_library(engine_render STATIC
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp
    src/engine/render/ModelAnimation.cpp
    src/engine/render/ModelCache.cpp

    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp
    src/engine/ui/BootLoadingView.cpp

    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderCache.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/Log.cpp
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp

    src/engine/vfx/ParticleSystem.cpp
)

target_link_libraries(engine_render
    PRIVATE
        OpenGL::GL
        glad::glad
        glm::glm
        lua
        nlohmann_json::nlohmann_json
        fastgltf::fastgltf
        ${PAC_SOL2_TARGET}
)

pac_apply_common_target_settings(engine_render)

# ---------------- Engine (compat umbrella target) ----------------
# Keep the existing target name "Engine" so downstream targets don't change.
add_library(Engine INTERFACE)
target_link_libraries(Engine
    INTERFACE
        engine_core
        engine_platform
        engine_render
)
pac_apply_common_target_settings(Engine)

# ---------------- Game objects (shared by exe + tests) ----------------
add_library(PAC_GameObjects STATIC
    # Game Core
    src/game/GameApp.cpp
    src/game/GameWorld.cpp
    src/game/GameStateManager.cpp
    src/game/GameConfig.cpp
    src/game/GamePreload.cpp
    src/game/GameRuntime.cpp
    src/game/GameBootstrap.cpp
    src/game/GameSession.cpp

    # Game Config
    src/game/config/PokemonConfigLoader.cpp
    src/game/config/MovesConfigLoader.cpp
    src/game/config/AttackAnimConfigLoader.cpp
    src/game/config/AnimSetLoader.cpp
    src/game/config/FlyerConfigLoader.cpp

    # Game Animations
    src/game/animation/FlightLocomotion.cpp

    # Game Logging
    src/game/logging/LogBus.cpp

    # Game Scripting
    src/game/scripting/LuaBindings.cpp
    src/game/scripting/LuaBindings_Core.cpp
    src/game/scripting/LuaBindings_World.cpp
    src/game/scripting/LuaBindings_UnitMove.cpp
    src/game/scripting/LuaBindings_Util.cpp
    src/game/scripting/LuaScript.cpp

    # Game States
    src/game/state/ScriptedState.cpp
    src/game/state/PlacementState.cpp
    src/game/state/CombatState.cpp

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
    src/game/ui/HealthBarQuery.cpp

    # Game VFX
    src/game/vfx/TailFireVFX.cpp
    src/game/vfx/TailFireVFXConfigDB.cpp
)

target_link_libraries(PAC_GameObjects
    PUBLIC
        Engine
        glm::glm
        nlohmann_json::nlohmann_json
)

pac_apply_common_target_settings(PAC_GameObjects)

# ---------------- Executable ----------------
add_executable(PokemonAutochess
    main.cpp
)

target_link_libraries(PokemonAutochess
    PRIVATE
        PAC_GameObjects
)

pac_apply_common_target_settings(PokemonAutochess)
