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
    src/engine/render/Model.cpp
    src/engine/render/ModelAnimation.cpp
    src/engine/render/ModelCache.cpp

    # Engine VFX (NEW)
    src/engine/vfx/ParticleSystem.cpp

    # Engine UI
    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp
    src/engine/ui/BootLoadingView.cpp
)

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

pac_apply_common_target_settings(Engine)

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
    src/game/MovesConfigLoader.cpp

    # Game VFX (NEW)
    src/game/vfx/CharmanderTailFireVFX.cpp

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
    src/game/ScriptedState.cpp
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

pac_apply_common_target_settings(PokemonAutochess)

# ---------------- Assets (runtime copy) ----------------
# IMPORTANT:
# The old approach copied shaders at CMake *configure time* only, so edits to shader files
# would not show up unless you reconfigured CMake. :contentReference[oaicite:1]{index=1}
#
# This copies the entire assets/ folder into the build directory *every build* so the
# executable can be run from the build folder and always see your latest shaders/textures.
add_custom_target(PAC_CopyAssets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMENT "Copying assets/ to build directory"
)

add_dependencies(PokemonAutochess PAC_CopyAssets)

# Windows entrypoint
if (WIN32)
    set_target_properties(PokemonAutochess PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup"
    )
endif()
