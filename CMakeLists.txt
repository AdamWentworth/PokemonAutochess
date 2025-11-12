#CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(PokemonAutochess)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
else()
    set(CMAKE_TOOLCHAIN_FILE "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# Dependencies
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)

# Header-only sol2 in third_party/sol2 (expected file: third_party/sol2/sol/sol.hpp)
set(SOL2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/sol2")

# Project include root
set(PROJECT_SRC_ROOT "${CMAKE_SOURCE_DIR}/src")

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

    # Engine Render
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp

    # Engine UI
    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp
)

target_include_directories(Engine
    PUBLIC
        "${PROJECT_SRC_ROOT}"
        "${SOL2_INCLUDE_DIR}"
    PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party"
)

target_link_libraries(Engine PRIVATE
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    OpenGL::GL
    glad::glad
    glm::glm
    lua
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

    # Game Systems
    src/game/systems/CameraSystem.cpp
    src/game/systems/UnitInteractionSystem.cpp
    src/game/systems/RoundSystem.cpp
    src/game/systems/CardSystem.cpp
    src/game/systems/BenchSystem.cpp
    src/game/systems/MovementSystem.cpp
    src/game/systems/CombatSystem.cpp
    src/game/systems/ShopSystem.cpp        # ← NEW

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
        "${SOL2_INCLUDE_DIR}"
        "${CMAKE_SOURCE_DIR}/third_party"
)

target_link_libraries(PokemonAutochess PRIVATE
    Engine
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    OpenGL::GL
    glad::glad
    glm::glm
    lua
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
