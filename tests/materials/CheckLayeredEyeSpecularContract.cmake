include("${CMAKE_CURRENT_LIST_DIR}/ReadCharacterProgram.cmake")
if(NOT DEFINED PAC_ROOT)
    message(FATAL_ERROR "PAC_ROOT is required")
endif()

set(GL_PATH
    "${PAC_ROOT}/src/game/render/materials/character/opengl_declarations.glsl")
set(D3D_PATH
    "${PAC_ROOT}/src/game/render/materials/character/d3d12_declarations.hlsl")
set(VK_DIRECT_PATH
    "${PAC_ROOT}/src/game/render/materials/character/world_evaluation.glsl")
set(VK_INDIRECT_PATH
    "${PAC_ROOT}/src/game/render/materials/character/world_indirect_evaluation.glsl")
set(VK_MATERIAL_PATH
    "${PAC_ROOT}/src/game/render/materials/character/character_lighting.glsl")

foreach(SOURCE_PATH IN ITEMS
        "${GL_PATH}"
        "${D3D_PATH}"
        "${VK_DIRECT_PATH}"
        "${VK_INDIRECT_PATH}")
    read_character_program("${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "bakedEyeDiffuse"
            "separateEyeCoat")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "PLA eye token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
endforeach()

read_character_program("${GL_PATH}" GL_TEXT)
foreach(REQUIRED_TOKEN IN ITEMS
        "layeredEyeCoat && uMaterialRect1.w < -0.5"
        "separateEyeCoat ? eyeSurfaceNormal : n")
    string(FIND "${GL_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "OpenGL PLA eye-specular token '${REQUIRED_TOKEN}' is missing")
    endif()
endforeach()

read_character_program("${D3D_PATH}" D3D_TEXT)
foreach(REQUIRED_TOKEN IN ITEMS
        "layeredEyeMode && uProjectedShadowRowY.w < -0.5f"
        "normalScale * 0.8f"
        "if (separateEyeCoat)")
    string(FIND "${D3D_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "D3D12 PLA eye-specular token '${REQUIRED_TOKEN}' is missing")
    endif()
endforeach()

foreach(SOURCE_PATH IN ITEMS "${VK_DIRECT_PATH}" "${VK_INDIRECT_PATH}")
    read_character_program("${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "bakedEyeDiffuse"
            "pbrFactors.x * 0.8"
            "? -2.0"
            "separateEyeCoat ? 0.0 : 1.0")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Vulkan PLA eye-specular token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
endforeach()

read_character_program("${VK_MATERIAL_PATH}" VK_MATERIAL_TEXT)
string(FIND
    "${VK_MATERIAL_TEXT}"
    "dielectricSpecularIntensity < -1.5"
    VK_SENTINEL_OFFSET)
if(VK_SENTINEL_OFFSET EQUAL -1)
    message(FATAL_ERROR
        "Vulkan PLA sparse-highlight sentinel is missing")
endif()

message(STATUS "PLA eye dielectric-specular parity contract verified")
