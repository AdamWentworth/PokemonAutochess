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
    engine/core/Application.cpp
    engine/render/Renderer.cpp
    engine/render/Camera3D.cpp
    engine/render/BoardRenderer.cpp
    engine/render/Model.cpp
)

target_include_directories(PokemonAutochess PRIVATE 
    engine
)

find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

target_link_libraries(PokemonAutochess PRIVATE
    SDL2::SDL2
    OpenGL::GL
    glad::glad
    glm::glm
)

file(COPY engine/render/shader.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY engine/render/shader.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY engine/render/grid.vert DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)
file(COPY engine/render/grid.frag DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/engine/render)