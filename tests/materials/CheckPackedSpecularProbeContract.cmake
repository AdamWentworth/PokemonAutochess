include("${CMAKE_CURRENT_LIST_DIR}/ReadCharacterProgram.cmake")
if(NOT DEFINED PAC_ROOT)
    message(FATAL_ERROR "PAC_ROOT is required")
endif()

set(GL_PATH "${PAC_ROOT}/src/game/render/materials/character/opengl_declarations.glsl")
set(D3D_PATH "${PAC_ROOT}/src/game/render/materials/character/d3d12_declarations.hlsl")
set(VK_PATH "${PAC_ROOT}/src/game/render/materials/character/character_lighting.glsl")

foreach(SOURCE_PATH IN ITEMS "${GL_PATH}" "${D3D_PATH}" "${VK_PATH}")
    read_character_program("${SOURCE_PATH}" SOURCE_TEXT)
    if(NOT SOURCE_TEXT MATCHES "samplePackedSpecularProbe")
        message(FATAL_ERROR "SV local-probe sampler is missing from ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES "atlasSize.*atlasSize.y.*3|atlasWidth.*atlasHeight.*3")
        message(FATAL_ERROR "SV packed-cube atlas qualification is missing from ${SOURCE_PATH}")
    endif()
endforeach()

read_character_program("${GL_PATH}" GL_SOURCE)
read_character_program("${D3D_PATH}" D3D_SOURCE)
read_character_program("${VK_PATH}" VK_SOURCE)
if(NOT GL_SOURCE MATCHES "decodePackedProbeHalf" OR
   NOT GL_SOURCE MATCHES "texelFetch")
    message(FATAL_ERROR "OpenGL lost exact packed-half reconstruction")
endif()
if(NOT D3D_SOURCE MATCHES "f16tof32" OR
   NOT D3D_SOURCE MATCHES "probeTexture.GetDimensions")
    message(FATAL_ERROR "D3D12 lost exact packed-half reconstruction")
endif()
if(NOT VK_SOURCE MATCHES "decodePackedProbeHalf" OR
   NOT VK_SOURCE MATCHES "texelFetch")
    message(FATAL_ERROR "Vulkan lost exact packed-half reconstruction")
endif()

message(STATUS "SV local specular probe contract verified")
