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
            "resolveRefractiveEyeParallaxUv"
            "cross(geometricNormal, tangent)"
            "refractionK"
            "footprint"
            "12.0"
            "10.0"
            "floor(layerScale) + 2.0"
            "currentDepth = 1.0"
            "previousDepth = 1.1"
            "previousHeight = 1.0"
            "layer < 14"
            "-footprint.x, footprint.y"
            "sampledHeight >= currentDepth"
            "currentOffset -= offsetStep"
            "currentOffset += offsetStep")
        string(FIND "${SOURCE_TEXT}" "${REQUIRED_TOKEN}" TOKEN_OFFSET)
        if(TOKEN_OFFSET EQUAL -1)
            message(FATAL_ERROR
                "Z-A exact eye-parallax token '${REQUIRED_TOKEN}' is missing from ${SOURCE_PATH}")
        endif()
    endforeach()
    if(NOT SOURCE_TEXT MATCHES
            "grazingFade = 1\\.0[f]* - pow\\(1\\.0[f]* - normalDotView, 5\\.0[f]*\\)")
        message(FATAL_ERROR
            "Z-A fifth-power view fade is missing from ${SOURCE_PATH}")
    endif()
    if(NOT SOURCE_TEXT MATCHES
            "footprint = abs\\((uvDx|dFdx\\(uv\\)) \\+ (uvDy|dFdy\\(uv\\))\\)")
        message(FATAL_ERROR
            "Z-A summed-derivative footprint is missing from ${SOURCE_PATH}")
    endif()
    if(SOURCE_TEXT MATCHES "layerCount = (mix|lerp)\\(8\\.0[f]*, 16\\.0[f]*")
        message(FATAL_ERROR
            "Legacy approximate Z-A eye layer schedule remains in ${SOURCE_PATH}")
    endif()
endforeach()

message(STATUS "Z-A exact eye parallax contract verified")
