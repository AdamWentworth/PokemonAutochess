# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)

# Toolchain must be provided at configure time (presets or -DCMAKE_TOOLCHAIN_FILE=...)
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
endfunction()

# ---------------- Engine (static lib) ----------------
add_library(Engine STATIC
    # Engine Core
    src/engine/core/Application.cpp
    src/engine/core/Window.cpp
    src/engine/core/SystemRegistry.cpp

    # Engine Render
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp
    src/engine/render/ModelAnimation.cpp
    src/engine/render/ModelCache.cpp

    # Engine UI
    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp
    src/engine/ui/BootLoadingView.cpp

    # Engine Utils
    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderLibrary.cpp
    src/engine/utils/ShaderCache.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp

    # Engine VFX (NEW)
    src/engine/vfx/ParticleSystem.cpp
)

# IMPORTANT:
# If the long-term goal is "engine owns SDL", then the executable should not link SDL directly.
# SDL is made PUBLIC here so consumers (PokemonAutochess) get it transitively by linking Engine.
target_link_libraries(Engine
    PUBLIC
        SDL2::SDL2
        SDL2_ttf::SDL2_ttf
    PRIVATE
        OpenGL::GL
        glad::glad
        glm::glm
        lua
        nlohmann_json::nlohmann_json
        fastgltf::fastgltf
        ${PAC_SOL2_TARGET}
)

pac_apply_common_target_settings(Engine)

# ---------------- Game (exe) ----------------
add_executable(PokemonAutochess
    main.cpp

    # Game Core
    src/game/GameApp.cpp
    src/game/GameWorld.cpp
    src/game/GameStateManager.cpp
    src/game/GameConfig.cpp

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
    src/game/ui/CardFactory.cpp
    src/game/ui/HealthBarQuery.cpp

    # Game VFX
    src/game/vfx/TailFireVFX.cpp
    src/game/vfx/TailFireVFXConfigDB.cpp
)

# SDL intentionally removed from the game target.
# It will be linked transitively through Engine.
target_link_libraries(PokemonAutochess PRIVATE
    Engine
    OpenGL::GL
    glad::glad
    glm::glm
    lua
    ${PAC_SOL2_TARGET}
)

pac_apply_common_target_settings(PokemonAutochess)

# ---------------- Assets (runtime copy) ----------------
# Copies assets/ into the build directory, but only re-runs when files under assets/ change.
set(PAC_ASSETS_SRC "${CMAKE_SOURCE_DIR}/assets")
set(PAC_ASSETS_DST "${CMAKE_CURRENT_BINARY_DIR}/assets")

# Track individual files so the copy step is incremental.
file(GLOB_RECURSE PAC_ASSET_FILES CONFIGURE_DEPENDS
    "${PAC_ASSETS_SRC}/*"
)

set(PAC_ASSETS_STAMP "${PAC_ASSETS_DST}/.pac_assets.stamp")

add_custom_command(
    OUTPUT "${PAC_ASSETS_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PAC_ASSETS_DST}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PAC_ASSETS_SRC}" "${PAC_ASSETS_DST}"
    COMMAND ${CMAKE_COMMAND} -E touch "${PAC_ASSETS_STAMP}"
    DEPENDS ${PAC_ASSET_FILES}
    COMMENT "Copying assets/ to build directory (incremental)"
    VERBATIM
)

add_custom_target(PAC_CopyAssets DEPENDS "${PAC_ASSETS_STAMP}")
add_dependencies(PokemonAutochess PAC_CopyAssets)

# Windows entrypoint
if (WIN32)
    set_target_properties(PokemonAutochess PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()
