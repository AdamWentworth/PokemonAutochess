// tests/TestMain.cpp
#include <iostream>
#include <string>
#include <vector>

bool test_lua_bindings_smoke(std::string& outFail);
bool test_eventbus_basic(std::string& outFail);
bool test_gameconfig_diagnostics(std::string& outFail);
bool test_ecs_smoke(std::string& outFail);
bool test_ecs_destroy_cleans_components(std::string& outFail);
bool test_ecs_for_each_join(std::string& outFail);
bool test_ecs_structural_change_deferral(std::string& outFail);
bool test_rng_determinism(std::string& outFail);
bool test_manual_time_source(std::string& outFail);
bool test_content_invariants(std::string& outFail);
bool test_battle_invariants(std::string& outFail);
bool test_end_to_end_headless(std::string& outFail);
bool test_movement_invariants(std::string& outFail);
bool test_model_parse_smoke(std::string& outFail);
bool test_gltf_asset_smoke(std::string& outFail);
bool test_script_api_contract(std::string& outFail);
bool test_round_flow_headless(std::string& outFail);
bool test_animset_roles_smoke(std::string& outFail);
bool test_combat_slice_headless(std::string& outFail);
bool test_render_pipeline_smoke(std::string& outFail);
bool test_animset_glb_name_smoke(std::string& outFail);
bool test_placement_to_combat_headless(std::string& outFail);
bool test_animset_clip_name_smoke(std::string& outFail);
bool test_layering_engine_no_game_includes(std::string& outFail);
bool test_state_manager_input_deferral(std::string& outFail);
bool test_mode_split_flow(std::string& outFail);
bool test_mode_split_menu_entries(std::string& outFail);
bool test_shop_layout_invariants(std::string& outFail);
bool test_lua_card_parser_contract(std::string& outFail);
bool test_lua_text_menu_parser_contract(std::string& outFail);
bool test_lua_script_helpers(std::string& outFail);
bool test_gameworld_merge_progression(std::string& outFail);
bool test_gameworld_spawn_bench_flow(std::string& outFail);
bool test_gameworld_income_flow(std::string& outFail);
bool test_gameworld_inventory_healing(std::string& outFail);
bool test_gameworld_reset_economy_state(std::string& outFail);
bool test_gameworld_capture_preconditions(std::string& outFail);
bool test_gameworld_capture_success_resolution(std::string& outFail);
bool test_gameworld_capture_failure_recovery(std::string& outFail);
bool test_gameworld_type_line_counts(std::string& outFail);
bool test_gameworld_nearest_enemy_position(std::string& outFail);
bool test_pokemon_config_loader_contract(std::string& outFail);
bool test_evolution_flyer_loader_contract(std::string& outFail);
bool test_source_ascii_hygiene(std::string& outFail);

struct TestCase {
    const char* name;
    bool (*fn)(std::string&);
};

static int run(const char* name, bool (*fn)(std::string&), int& failCount) {
    std::string fail;
    const bool ok = fn(fail);
    if (!ok) { ++failCount; std::cerr << "[PAC_Tests] FAIL: " << name << " :: " << fail << "\n"; return 1; }
    std::cout << "[PAC_Tests] PASS: " << name << "\n";
    return 0;
}

static bool shouldRun(const std::vector<std::string>& filters, const char* name) {
    if (filters.empty()) return true;
    for (const auto& f : filters) {
        if (f == name) return true;
    }
    return false;
}

int main(int argc, char** argv) {
    std::vector<std::string> filters;
    bool listOnly = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") {
            listOnly = true;
            continue;
        }
        if (arg.rfind("--filter=", 0) == 0) {
            filters.push_back(arg.substr(9));
            continue;
        }
        if (arg == "--filter" && i + 1 < argc) {
            filters.push_back(argv[++i]);
            continue;
        }
        filters.push_back(arg);
    }

    const TestCase tests[] = {
        {"lua_bindings_smoke", &test_lua_bindings_smoke},
        {"eventbus_basic", &test_eventbus_basic},
        {"gameconfig_diagnostics", &test_gameconfig_diagnostics},
        {"ecs_smoke", &test_ecs_smoke},
        {"ecs_destroy_cleans_components", &test_ecs_destroy_cleans_components},
        {"ecs_for_each_join", &test_ecs_for_each_join},
        {"ecs_structural_change_deferral", &test_ecs_structural_change_deferral},
        {"rng_determinism", &test_rng_determinism},
        {"manual_time_source", &test_manual_time_source},
        {"content_invariants", &test_content_invariants},
        {"battle_invariants", &test_battle_invariants},
        {"end_to_end_headless", &test_end_to_end_headless},
        {"movement_invariants", &test_movement_invariants},
        {"model_parse_smoke", &test_model_parse_smoke},
        {"gltf_asset_smoke", &test_gltf_asset_smoke},
        {"script_api_contract", &test_script_api_contract},
        {"round_flow_headless", &test_round_flow_headless},
        {"animset_roles_smoke", &test_animset_roles_smoke},
        {"combat_slice_headless", &test_combat_slice_headless},
        {"render_pipeline_smoke", &test_render_pipeline_smoke},
        {"animset_glb_name_smoke", &test_animset_glb_name_smoke},
        {"placement_to_combat_headless", &test_placement_to_combat_headless},
        {"animset_clip_name_smoke", &test_animset_clip_name_smoke},
        {"layering_engine_no_game_includes", &test_layering_engine_no_game_includes},
        {"state_manager_input_deferral", &test_state_manager_input_deferral},
        {"mode_split_flow", &test_mode_split_flow},
        {"mode_split_menu_entries", &test_mode_split_menu_entries},
        {"shop_layout_invariants", &test_shop_layout_invariants},
        {"lua_card_parser_contract", &test_lua_card_parser_contract},
        {"lua_text_menu_parser_contract", &test_lua_text_menu_parser_contract},
        {"lua_script_helpers", &test_lua_script_helpers},
        {"gameworld_merge_progression", &test_gameworld_merge_progression},
        {"gameworld_spawn_bench_flow", &test_gameworld_spawn_bench_flow},
        {"gameworld_income_flow", &test_gameworld_income_flow},
        {"gameworld_inventory_healing", &test_gameworld_inventory_healing},
        {"gameworld_reset_economy_state", &test_gameworld_reset_economy_state},
        {"gameworld_capture_preconditions", &test_gameworld_capture_preconditions},
        {"gameworld_capture_success_resolution", &test_gameworld_capture_success_resolution},
        {"gameworld_capture_failure_recovery", &test_gameworld_capture_failure_recovery},
        {"gameworld_type_line_counts", &test_gameworld_type_line_counts},
        {"gameworld_nearest_enemy_position", &test_gameworld_nearest_enemy_position},
        {"pokemon_config_loader_contract", &test_pokemon_config_loader_contract},
        {"evolution_flyer_loader_contract", &test_evolution_flyer_loader_contract},
        {"source_ascii_hygiene", &test_source_ascii_hygiene},
    };

    if (listOnly) {
        for (const auto& t : tests) std::cout << t.name << "\n";
        return 0;
    }

    int fails = 0;
    int ran = 0;

    for (const auto& t : tests) {
        if (!shouldRun(filters, t.name)) continue;
        ++ran;
        run(t.name, t.fn, fails);
    }

    if (!filters.empty() && ran == 0) {
        std::cerr << "[PAC_Tests] No matching tests.\n";
        return 2;
    }

    if (fails == 0) { std::cout << "[PAC_Tests] All tests passed.\n"; return 0; }
    std::cerr << "[PAC_Tests] " << fails << " test(s) failed.\n";
    return 1;
}
