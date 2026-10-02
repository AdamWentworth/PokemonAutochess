include("${POKEMON_MODEL_SUPPORT_SOURCE_DIR}/cmake/ComposeMaterialProfile.cmake")
pokemon_compose_material_profile(TARGET PAC_WorldMaterialShaders
    OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/.phlosion/materials/world"
    ENGINE_ROOT "${_pac_resolved_engine_source_dir}"
    FIELD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/field")
