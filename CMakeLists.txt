# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(PokemonAutochess)

set(CMAKE_CXX_STANDARD 17)

# ------------------------------------------------------------------------------
# ✅ VCPKG TOOLCHAIN (for cross-platform dependency management)
# ------------------------------------------------------------------------------

if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
else()
    set(CMAKE_TOOLCHAIN_FILE "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# ------------------------------------------------------------------------------
# ✅ REQUIRED PACKAGES
# ------------------------------------------------------------------------------

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)

# ------------------------------------------------------------------------------
# ✅ SOURCE FILES
# ------------------------------------------------------------------------------

add_executable(PokemonAutochess
    main.cpp

    # Engine
    src/engine/core/Application.cpp
    src/engine/core/Window.cpp
    src/engine/core/SystemRegistry.cpp

    src/engine/utils/Shader.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/stb_image_impl.cpp

    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp

    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp

    # Game
    src/game/GameWorld.cpp
    src/game/GameStateManager.cpp
    src/game/LuaBindings.cpp
    src/game/LuaScript.cpp
    src/game/PokemonConfigLoader.cpp

    src/game/systems/CameraSystem.cpp
    src/game/systems/UnitInteractionSystem.cpp
    src/game/systems/RoundSystem.cpp
    src/game/systems/CardSystem.cpp
    src/game/systems/BenchSystem.cpp

    src/game/ui/CardFactory.cpp

    src/game/state/StarterSelectionState.cpp
)

# ------------------------------------------------------------------------------
# ✅ WINDOWS ENTRY POINT SETTINGS
# ------------------------------------------------------------------------------

set_target_properties(PokemonAutochess PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup")

# ------------------------------------------------------------------------------
# ✅ INCLUDE DIRECTORIES
# ------------------------------------------------------------------------------

target_include_directories(PokemonAutochess PRIVATE 
    engine
    game
    third_party/sol2
)

# ------------------------------------------------------------------------------
# ✅ LINK LIBRARIES
# ------------------------------------------------------------------------------

target_link_libraries(PokemonAutochess PRIVATE
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    OpenGL::GL
    glad::glad
    glm::glm
    lua
)

# ------------------------------------------------------------------------------
# ✅ SHADER FILES (copied to binary dir for runtime loading)
# ------------------------------------------------------------------------------

file(COPY assets/shaders/engine/default.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/default.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/grid.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/grid.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/model/model.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/model/model.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/card.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/card.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/text.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/text.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
