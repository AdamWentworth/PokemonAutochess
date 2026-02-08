# CMakeLists.txt

cmake_minimum_required(VERSION 3.22)
project(PokemonAutochess LANGUAGES CXX)

option(PAC_VERBOSE_STARTUP "Enable verbose startup/model-load logging" OFF)
option(PAC_BUILD_TOOLS "Build developer tools (data cooker)" ON)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------- Dependencies ----------------
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lua CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

find_package(sol2 CONFIG REQUIRED)
if (TARGET sol2::sol2)
    set(PAC_SOL2_TARGET sol2::sol2)
elseif (TARGET sol2)
    set(PAC_SOL2_TARGET sol2)
else()
    message(FATAL_ERROR "sol2 package found, but no known CMake target (sol2::sol2 or sol2) is available.")
endif()

set(PROJECT_SRC_ROOT "${CMAKE_SOURCE_DIR}/src")

# ---------------- Common target settings ----------------
function(pac_apply_common_target_settings target_name)
    get_target_property(_type ${target_name} TYPE)

    if (_type STREQUAL "INTERFACE_LIBRARY")
        target_include_directories(${target_name}
            INTERFACE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            INTERFACE
                PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )
    else()
        target_include_directories(${target_name}
            PRIVATE
                "${PROJECT_SRC_ROOT}"
        )
        target_compile_definitions(${target_name}
            PRIVATE
                PAC_VERBOSE_STARTUP=$<BOOL:${PAC_VERBOSE_STARTUP}>
        )

        if (MSVC)
            target_compile_options(${target_name} PRIVATE /std:c++20 /permissive- /Zc:__cplusplus /FS)
        else()
            target_compile_options(${target_name} PRIVATE -std=c++20)
        endif()
    endif()
endfunction()

# ---------------- engine_core (no SDL/GL) ----------------
add_library(engine_core STATIC
    src/engine/core/SystemRegistry.cpp
    src/engine/core/StdoutLogger.cpp
    src/engine/core/EventBus.cpp
    src/engine/core/ecs/World.cpp
)
target_sources(engine_core PRIVATE
    src/engine/core/SystemRegistry.h
    src/engine/core/Updatable.h
    src/engine/core/ILogger.h
    src/engine/core/StdoutLogger.h
    src/engine/core/IEventBus.h
    src/engine/core/EventBus.h
    src/engine/core/Services.h
    src/engine/core/AssetPackFormat.h
    src/engine/core/IRandom.h
    src/engine/core/Random.h
    src/engine/core/ITimeSource.h
    src/engine/core/TimeSources.h
    src/engine/core/ecs/Entity.h
    src/engine/core/ecs/ComponentStorage.h
    src/engine/core/ecs/ISystem.h
    src/engine/core/ecs/Scheduler.h
    src/engine/core/ecs/World.h
    src/engine/core/ecs/View.h
)
target_link_libraries(engine_core PRIVATE glm::glm)
pac_apply_common_target_settings(engine_core)

# ---------------- engine_platform (SDL, window/input) ----------------
add_library(engine_platform STATIC
    src/engine/platform/Window.cpp
    src/engine/input/SdlKeyMap.cpp
)
target_link_libraries(engine_platform
    PUBLIC SDL2::SDL2 SDL2_ttf::SDL2_ttf
    PRIVATE glm::glm
)
pac_apply_common_target_settings(engine_platform)

# ---------------- engine_render (OpenGL + renderer + resources) ----------------
add_library(engine_render STATIC
    src/engine/render/Renderer.cpp
    src/engine/render/Camera3D.cpp
    src/engine/render/BoardRenderer.cpp
    src/engine/render/Model.cpp
    src/engine/render/ModelAnimation.cpp
    src/engine/render/ModelCache.cpp
    src/engine/render/ModelFastGltfLoader.cpp

    src/engine/ui/UIManager.cpp
    src/engine/ui/Card.cpp
    src/engine/ui/TextRenderer.cpp
    src/engine/ui/HealthBarRenderer.cpp
    src/engine/ui/BattleFeed.cpp
    src/engine/ui/BootLoadingView.cpp

    src/engine/utils/Shader.cpp
    src/engine/utils/ShaderCache.cpp
    src/engine/utils/ResourceManager.cpp
    src/engine/utils/Log.cpp
    src/engine/utils/stb_image_impl.cpp
    src/engine/utils/stb_image_write_impl.cpp

    src/engine/vfx/ParticleSystem.cpp
)
target_link_libraries(engine_render
    PRIVATE
        OpenGL::GL
        glad::glad
        glm::glm
        lua
        nlohmann_json::nlohmann_json
        fastgltf::fastgltf
        ${PAC_SOL2_TARGET}
)
pac_apply_common_target_settings(engine_render)

# ---------------- Engine (compat umbrella target) ----------------
add_library(Engine INTERFACE)
target_link_libraries(Engine INTERFACE engine_core engine_platform engine_render)
pac_apply_common_target_settings(Engine)

# ---------------- engine_runtime (optional SDL/GL loop host) ----------------
add_library(engine_runtime STATIC
    src/engine/runtime/Application.cpp
)
target_link_libraries(engine_runtime PRIVATE engine_core engine_platform engine_render)
pac_apply_common_target_settings(engine_runtime)

# ---------------- Game objects (shared by exe + tests) ----------------
add_library(PAC_GameObjects STATIC
    # Game Core
    src/game/GameApp.cpp
    src/game/GameRunner.cpp
    src/game/GameWorld.cpp
    src/game/GameStateManager.cpp
    src/game/GameConfig.cpp
    src/game/GamePreload.cpp
    src/game/GameRuntime.cpp
    src/game/GameBootstrap.cpp
    src/game/GameSession.cpp
    src/game/GameUpdateGraph.cpp

    # Game Config
    src/game/config/PokemonConfigLoader.cpp
    src/game/config/MovesConfigLoader.cpp
    src/game/config/AttackAnimConfigLoader.cpp
    src/game/config/AnimSetLoader.cpp
    src/game/config/FlyerConfigLoader.cpp

    # Game Animations
    src/game/animation/FlightLocomotion.cpp

    # Game Logging
    src/game/logging/LogBus.cpp

    # Game UI
    src/game/ui/HealthBarQuery.cpp

    # Game VFX
    src/game/vfx/TailFireVFX.cpp
    src/game/vfx/TailFireVFXConfigDB.cpp
    src/game/vfx/GrassImpactVFX.cpp
    src/game/vfx/LeechSeedProjectileVFX.cpp

    # Game Assets
    src/game/assets/DevAssetStore.cpp
    src/game/assets/PackedAssetStore.cpp

    # Game Scripting
    src/game/scripting/LuaBindings.cpp
    src/game/scripting/LuaBindings_Core.cpp
    src/game/scripting/LuaBindings_World.cpp
    src/game/scripting/LuaBindings_UnitMove.cpp
    src/game/scripting/LuaBindings_Util.cpp
    src/game/scripting/LuaScript.cpp
    src/game/scripting/ScriptAPI.cpp
    src/game/scripting/ScriptEventBus.cpp

    # Game States
    src/game/state/ScriptedState.cpp
    src/game/state/PlacementState.cpp
    src/game/state/CombatState.cpp

    # Game Systems
    src/game/systems/CameraSystem.cpp
    src/game/systems/UnitInteractionSystem.cpp
    src/game/systems/RoundSystem.cpp
    src/game/systems/CardSystem.cpp
    src/game/systems/BenchSystem.cpp
    src/game/systems/MovementSystem.cpp
    src/game/systems/CombatSystem.cpp
    src/game/systems/ShopSystem.cpp
)
target_link_libraries(PAC_GameObjects PUBLIC Engine glm::glm nlohmann_json::nlohmann_json)
pac_apply_common_target_settings(PAC_GameObjects)

# ---------------- Executable ----------------
add_executable(PokemonAutochess
    src/main.cpp
)
target_link_libraries(PokemonAutochess PRIVATE PAC_GameObjects)
pac_apply_common_target_settings(PokemonAutochess)

# ---------------- Tools ----------------
if (PAC_BUILD_TOOLS)
    add_executable(PAC_DataCooker
        tools/PAC_DataCooker.cpp
    )
    target_link_libraries(PAC_DataCooker
        PRIVATE
            nlohmann_json::nlohmann_json
    )
    pac_apply_common_target_settings(PAC_DataCooker)
endif()

# ---------------- Tests ----------------
if (BUILD_TESTING)
    add_executable(PAC_Tests
        tests/TestMain.cpp
        tests/TestLuaBindings.cpp
        tests/TestEventBus.cpp
        tests/TestGameConfig.cpp
        tests/TestECS.cpp
        tests/TestDeterminism.cpp
        tests/TestContentValidation.cpp
        tests/TestBattleInvariants.cpp
        tests/TestEndToEnd.cpp
        tests/TestMovementSystem.cpp
        tests/TestModelParse.cpp
        tests/TestGltfAsset.cpp
        tests/TestScriptApiContract.cpp
        tests/TestRoundFlow.cpp
        tests/TestAnimsetRoles.cpp
        tests/TestCombatSlice.cpp
        tests/TestRenderSmoke.cpp
        tests/TestAnimsetGlbNames.cpp
        tests/TestPlacementToCombat.cpp
        tests/TestAnimsetClipNames.cpp
        tests/TestLayering.cpp
    )

    target_link_libraries(PAC_Tests
        PRIVATE
            PAC_GameObjects
            engine_core
            glad::glad
            OpenGL::GL
    )

    # Tests include both /src and /tests
    target_include_directories(PAC_Tests
        PRIVATE
            "${PROJECT_SRC_ROOT}"
            "${CMAKE_SOURCE_DIR}/tests"
    )

    if (MSVC)
        target_compile_options(PAC_Tests PRIVATE /std:c++20 /permissive- /Zc:__cplusplus /FS)
    else()
        target_compile_options(PAC_Tests PRIVATE -std=c++20)
    endif()

    # Register individual tests for CTest output granularity.
    set(PAC_TEST_NAMES
        lua_bindings_smoke
        eventbus_basic
        gameconfig_diagnostics
        ecs_smoke
        ecs_destroy_cleans_components
        ecs_for_each_join
        ecs_structural_change_deferral
        rng_determinism
        manual_time_source
        content_invariants
        battle_invariants
        end_to_end_headless
        movement_invariants
        model_parse_smoke
        gltf_asset_smoke
        script_api_contract
        round_flow_headless
        animset_roles_smoke
        combat_slice_headless
        render_pipeline_smoke
        animset_glb_name_smoke
        placement_to_combat_headless
        animset_clip_name_smoke
        layering_engine_no_game_includes
    )

    foreach(test_name IN LISTS PAC_TEST_NAMES)
        add_test(NAME PAC_Tests.${test_name} COMMAND PAC_Tests --filter ${test_name})
    endforeach()

    # Ensure tests run with repo root as working directory so relative config/script paths resolve.
    set(PAC_ALL_TESTS)
    foreach(test_name IN LISTS PAC_TEST_NAMES)
        list(APPEND PAC_ALL_TESTS PAC_Tests.${test_name})
    endforeach()

    set_tests_properties(${PAC_ALL_TESTS} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        ENVIRONMENT "PAC_DATA_ROOT=${CMAKE_SOURCE_DIR};PAC_ASSET_ROOT=${CMAKE_SOURCE_DIR}/assets"
    )
endif()

# ---------------- Convenience aggregate target ----------------
# Build everything with one target (engine libs + game + optional tests/tools).
set(PAC_ALL_TARGETS
    engine_core
    engine_platform
    engine_render
    engine_runtime
    PAC_GameObjects
    PokemonAutochess
)
if (BUILD_TESTING)
    list(APPEND PAC_ALL_TARGETS PAC_Tests)
endif()
if (PAC_BUILD_TOOLS)
    list(APPEND PAC_ALL_TARGETS PAC_DataCooker)
endif()
add_custom_target(PAC_All
    DEPENDS
        ${PAC_ALL_TARGETS}
)

# ---------------- Data pack helper targets ----------------
if (PAC_BUILD_TOOLS)
    add_custom_target(PAC_ValidateData
        COMMAND $<TARGET_FILE:PAC_DataCooker> --validate-only
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS PAC_DataCooker
    )

    add_custom_target(PAC_PackData
        COMMAND $<TARGET_FILE:PAC_DataCooker> --out content_pak/content.pak
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS PAC_DataCooker
    )
endif()

