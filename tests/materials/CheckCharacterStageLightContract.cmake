include("${CMAKE_CURRENT_LIST_DIR}/ReadCharacterProgram.cmake")
if(NOT DEFINED PAC_ROOT)
    message(FATAL_ERROR "PAC_ROOT is required")
endif()

set(GL_PATH "${PAC_ROOT}/src/game/render/materials/character/opengl_declarations.glsl")
set(D3D_PATH "${PAC_ROOT}/src/game/render/materials/character/d3d12_declarations.hlsl")
set(VK_PATH "${PAC_ROOT}/src/game/render/materials/character/character_lighting.glsl")

foreach(SOURCE_PATH IN ITEMS "${GL_PATH}" "${D3D_PATH}" "${VK_PATH}")
    read_character_program("${SOURCE_PATH}" SOURCE_TEXT)
    foreach(REQUIRED_TOKEN IN ITEMS
            "sourceSceneShadowVisibility"
            "sourceSceneShadowBypass"
            "effectiveDirectShadowVisibility"
            "shadowedWrappedLambert"
            "sourceSceneShadowBypass * sourceSceneShadowBypass"
            "biasedLambert * effectiveDirectShadowVisibility"
            "shadowedWrappedLambert - authoredShadowShift"
            "max(directDiffuse"
            "layeredCharacterLocalReflectionDirection"
            "layeredCharacterEmissionColor"
            "-0.44695543"
            "0.64944804"
            "-0.61518134"
            "reflect(-viewDirection, mappedNormal)")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Z-A scene-light token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
    if(NOT SOURCE_TEXT MATCHES
            "sourceSceneShadowVisibility = 1\\.0[f]*")
        message(FATAL_ERROR
            "Z-A unavailable scene-shadow boundary is not neutral in ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES
            "sourceSceneShadowBypass = 0\\.0[f]*")
        message(FATAL_ERROR
            "Z-A unavailable scene-shadow bypass is not neutral in ${SOURCE_PATH}")
    endif()
    if(SOURCE_TEXT MATCHES
            "shadowProcessDomain = (clamp|saturate)\\([^\\n]*wrappedLambert - authoredShadowShift")
        message(FATAL_ERROR
            "Z-A ShadowingShift still bypasses the scene-shadow stage in ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES
            "layeredCharacterLocalReflectionDirection[^}]*return reflect\\(-viewDirection, mappedNormal\\)")
        message(FATAL_ERROR
            "Z-A local-reflection direction changed in ${SOURCE_PATH}")
    endif()
endforeach()

set(D3D_INTERNAL_PATH
    "${PAC_ROOT}/config/render/world_materials.json")
read_character_program("${D3D_INTERNAL_PATH}" D3D_INTERNAL_TEXT)
foreach(REQUIRED_TOKEN IN ITEMS
        "lightProjectionUvRowU.0"
        "32"
        "materialRect0W")
    string(FIND "${D3D_INTERNAL_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
    if(TOKEN_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Z-A body-emission transport token '${REQUIRED_TOKEN}' is missing from ${D3D_INTERNAL_PATH}")
    endif()
endforeach()

message(STATUS "Z-A scene-light staging contract verified")
