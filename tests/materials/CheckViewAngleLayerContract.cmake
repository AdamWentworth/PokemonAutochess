include("${CMAKE_CURRENT_LIST_DIR}/ReadCharacterProgram.cmake")
if(NOT DEFINED PAC_ROOT)
    message(FATAL_ERROR "PAC_ROOT is required")
endif()

set(GL_PATH "${PAC_ROOT}/src/game/render/materials/character/opengl_declarations.glsl")
set(D3D_PATH "${PAC_ROOT}/src/game/render/materials/character/d3d12_declarations.hlsl")
set(VK_MATERIAL_PATH "${PAC_ROOT}/src/game/render/materials/character/character_lighting.glsl")
set(VK_DIRECT_PATH "${PAC_ROOT}/src/game/render/materials/character/world_evaluation.glsl")
set(VK_INDIRECT_PATH "${PAC_ROOT}/src/game/render/materials/character/world_indirect_evaluation.glsl")

read_character_program("${GL_PATH}" GL_SOURCE)
foreach(TOKEN IN ITEMS
        "computeMappedNormalFromTexture"
        "uMetallicRoughnessTexture"
        "uMaterialFlipbook1.w"
        "viewAngleLayer")
    if(NOT GL_SOURCE MATCHES "${TOKEN}")
        message(FATAL_ERROR "OpenGL FresnelEffect lost token: ${TOKEN}")
    endif()
endforeach()

read_character_program("${D3D_PATH}" D3D_SOURCE)
foreach(TOKEN IN ITEMS
        "computeMappedFresnelLayerNormal"
        "gMetallicRoughnessTex"
        "uLightProjectionUvRowU.w"
        "viewAngleLayer")
    if(NOT D3D_SOURCE MATCHES "${TOKEN}")
        message(FATAL_ERROR "D3D12 FresnelEffect lost token: ${TOKEN}")
    endif()
endforeach()

read_character_program("${VK_MATERIAL_PATH}" VK_MATERIAL_SOURCE)
foreach(TOKEN IN ITEMS
        "layerNormalMap"
        "surfaceControls.w"
        "useMetallicRoughnessMap"
        "evaluateViewAngleLayerLayer")
    if(NOT VK_MATERIAL_SOURCE MATCHES "${TOKEN}")
        message(FATAL_ERROR "Vulkan FresnelEffect lost token: ${TOKEN}")
    endif()
endforeach()

foreach(VK_PATH IN ITEMS "${VK_DIRECT_PATH}" "${VK_INDIRECT_PATH}")
    read_character_program("${VK_PATH}" VK_SOURCE)
    if(NOT VK_SOURCE MATCHES "evaluateViewAngleLayerLayer")
        message(FATAL_ERROR "Vulkan FresnelEffect call is missing from ${VK_PATH}")
    endif()
    if(NOT VK_SOURCE MATCHES "metallicRoughnessTexture")
        message(FATAL_ERROR "Vulkan secondary normal binding is missing from ${VK_PATH}")
    endif()
endforeach()

message(STATUS "Native FresnelEffect primary/secondary normal contract verified")

foreach(SOURCE_PATH IN ITEMS "${GL_PATH}" "${D3D_PATH}" "${VK_DIRECT_PATH}" "${VK_INDIRECT_PATH}")
    read_character_program("${SOURCE_PATH}" SOURCE_TEXT)
    if(NOT SOURCE_TEXT MATCHES "viewAngleLayerBase")
        message(FATAL_ERROR "Resolved albedo omits view-angle color factors in ${SOURCE_PATH}")
    endif()
endforeach()
