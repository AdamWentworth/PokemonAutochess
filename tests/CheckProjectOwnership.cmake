if (NOT DEFINED PAC_ROOT OR NOT DEFINED PHLOSION_ROOT)
    message(FATAL_ERROR
        "PAC_ROOT and PHLOSION_ROOT are required")
endif()

set(_required_project_files
    "${PAC_ROOT}/src/game/assets/environment/PublishedEnvironmentScene.h"
    "${PAC_ROOT}/src/game/assets/environment/PublishedEnvironmentScene.cpp"
    "${PAC_ROOT}/src/game/render/environment/Route1FieldGroundMaterial.h"
    "${PAC_ROOT}/src/game/ui/legacy/Card.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorAssetCatalog.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorAssetCatalog.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorCommands.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorCommands.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorHierarchy.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorHierarchy.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorLayoutTransactions.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorLayoutTransactions.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorPersistence.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorPersistence.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorPreviewCatalog.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorPreviewCatalog.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorSceneMutationSession.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorSceneMutationSession.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorSceneMutations.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorSceneMutations.cpp"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorViewportProjection.h"
    "${PAC_ROOT}/src/game/editor/PokemonAutochessEditorViewportProjection.cpp"
    "${PAC_ROOT}/tools/PokemonAutochessEditorProject.cpp")
if (DEFINED PHLOSION_PACKAGES_ROOT AND
    NOT PHLOSION_PACKAGES_ROOT STREQUAL "")
    list(APPEND _required_project_files
        "${PHLOSION_PACKAGES_ROOT}/packages/tile-tools/phlosion.package.json")
endif()
foreach(_file IN LISTS _required_project_files)
    if (NOT EXISTS "${_file}")
        message(FATAL_ERROR "Project-owned implementation is missing: ${_file}")
    endif()
endforeach()

set(_forbidden_engine_paths
    "${PHLOSION_ROOT}/src/engine/assets/lgpe"
    "${PHLOSION_ROOT}/src/engine/render/Route1FieldGroundMaterial.h"
    "${PHLOSION_ROOT}/src/engine/ui/Card.h"
    "${PHLOSION_ROOT}/src/engine/editor/TileTools.cpp"
    "${PAC_ROOT}/packages/tile-tools")
foreach(_path IN LISTS _forbidden_engine_paths)
    if (EXISTS "${_path}")
        message(FATAL_ERROR
            "Pokemon Autochess implementation leaked into Phlosion Engine: ${_path}")
    endif()
endforeach()

file(READ "${PAC_ROOT}/phlosion.project.json" _project_descriptor)
if (_project_descriptor MATCHES
    "\"id\"[ \t\r\n]*:[ \t\r\n]*\"phlosion.tile-tools\"")
    message(FATAL_ERROR
        "Blender-authored environments must not load the retired source tile-editing package")
endif()

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

# These modules remain usable without a renderer, editor, world, or engine
# service locator. Gameplay adapters depend on them, never the reverse.
file(GLOB_RECURSE _arena_sources LIST_DIRECTORIES false
    "${PAC_ROOT}/src/game/arena/*.h" "${PAC_ROOT}/src/game/arena/*.cpp")
foreach(_file IN LISTS _arena_sources)
    file(STRINGS "${_file}" _includes REGEX "^[ \t]*#include")
    foreach(_include IN LISTS _includes)
        if (_include MATCHES "\"" AND NOT _include MATCHES "\"game/arena/")
            message(FATAL_ERROR "Arena logic includes a higher-level dependency: ${_file}: ${_include}")
        endif()
        if (_include MATCHES "[<\"](engine/|GL/|SDL|vulkan/)")
            message(FATAL_ERROR "Arena logic acquired a runtime/render dependency: ${_file}: ${_include}")
        endif()
    endforeach()
endforeach()
message(STATUS "Arena logic dependency boundary is intact")
