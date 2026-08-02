if (NOT DEFINED PAC_ROOT OR NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR "PAC_ROOT and PHLOSION_ROOT are required")
endif()

set(_required_project_files
    "${PAC_ROOT}/src/game/assets/lgpe/LgpeCanonicalScene.h"
    "${PAC_ROOT}/src/game/assets/lgpe/LgpeCanonicalScene.cpp"
    "${PAC_ROOT}/src/game/render/lgpe/LgpeFieldGroundMaterial.h"
    "${PAC_ROOT}/src/game/ui/legacy/Card.h"
    "${PAC_ROOT}/tools/PokemonAutochessEditorProject.cpp")
foreach(_file IN LISTS _required_project_files)
    if (NOT EXISTS "${_file}")
        message(FATAL_ERROR "Project-owned implementation is missing: ${_file}")
    endif()
endforeach()

set(_forbidden_engine_paths
    "${PHLOSION_ROOT}/src/engine/assets/lgpe"
    "${PHLOSION_ROOT}/src/engine/render/LgpeFieldGroundMaterial.h"
    "${PHLOSION_ROOT}/src/engine/ui/Card.h")
foreach(_path IN LISTS _forbidden_engine_paths)
    if (EXISTS "${_path}")
        message(FATAL_ERROR
            "Pokemon Autochess implementation leaked into Phlosion Engine: ${_path}")
    endif()
endforeach()

file(GLOB_RECURSE _engine_sources LIST_DIRECTORIES false
    "${PHLOSION_ROOT}/src/*.h"
    "${PHLOSION_ROOT}/src/*.cpp")
foreach(_file IN LISTS _engine_sources)
    file(READ "${_file}" _content)
    if (_content MATCHES "#include[ \t]+\"game/")
        message(FATAL_ERROR
            "Phlosion Engine includes a Pokemon Autochess header: ${_file}")
    endif()
endforeach()

message(STATUS "Pokemon Autochess project ownership boundary is intact")
