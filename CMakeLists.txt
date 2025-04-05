# CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(PokemonAutochess)

set(CMAKE_CXX_STANDARD 17)

# Use vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
else()
    set(CMAKE_TOOLCHAIN_FILE "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

find_package(SDL2 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(PokemonAutochess
    main.cpp
    src/engine/core/Application.cpp
    src/engine/core/Window.cpp
    src/engine/utils/Shader.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp
    src/game/GameWorld.cpp
    src/game/GameStateManager.cpp
    src/game/StarterSelectionState.cpp
)

# Force console subsystem and entry point
set_target_properties(PokemonAutochess PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup")

target_include_directories(PokemonAutochess PRIVATE 
    engine
    game
)

find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(PokemonAutochess PRIVATE
    SDL2::SDL2
    OpenGL::GL
    glad::glad
    glm::glm
)

file(COPY assets/shaders/engine/default.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/default.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/grid.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/engine/grid.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/model/model.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/model/model.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/ui_shader.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY assets/shaders/ui/ui_shader.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
