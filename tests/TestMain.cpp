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
bool test_environment_helpers_contract(std::string& outFail);
bool test_auto_quit_policy_contract(std::string& outFail);
bool test_game_services_route_helpers(std::string& outFail);
bool test_game_service_render_routes_contract(std::string& outFail);
bool test_startup_render_route_policy_contract(std::string& outFail);
bool test_render_route_ownership_contract(std::string& outFail);
bool test_state_ui_route_policy_contract(std::string& outFail);
bool test_render_routes_contract(std::string& outFail);
bool test_render_policy_api_contract(std::string& outFail);
bool test_logbus_recent_lines_contract(std::string& outFail);
bool test_render_flow_decisions_contract(std::string& outFail);
bool test_backend_render_policy_contract(std::string& outFail);
bool test_renderer_parity_contract_baseline(std::string& outFail);
bool test_renderer_parity_contract_detects_drift(std::string& outFail);
bool test_debug_geometry_line_raster_contract(std::string& outFail);
bool test_backend_hud_formatting_contract(std::string& outFail);
bool test_backend_debug_text_quads_contract(std::string& outFail);
bool test_backend_image_path_contract(std::string& outFail);
bool test_backend_material_shading_contract(std::string& outFail);
bool test_backend_mesh_normals_contract(std::string& outFail);
bool test_backend_procedural_pose_contract(std::string& outFail);
bool test_backend_model_cache_contract(std::string& outFail);
bool test_backend_world_projection_contract(std::string& outFail);
bool test_backend_world_proxy_geometry_contract(std::string& outFail);
bool test_backend_unit_visuals_contract(std::string& outFail);
bool test_shared_projected_world_scene_helpers_contract(std::string& outFail);
bool test_backend_inventory_overlay_contract(std::string& outFail);
bool test_backend_inventory_panel_contract(std::string& outFail);
bool test_backend_card_layout_model_contract(std::string& outFail);
bool test_backend_card_visuals_contract(std::string& outFail);
bool test_backend_card_renderer_contract(std::string& outFail);
bool test_backend_shop_hud_model_contract(std::string& outFail);
bool test_backend_sell_overlay_model_contract(std::string& outFail);
bool test_backend_status_text_contract(std::string& outFail);
bool test_backend_ui_scale_contract(std::string& outFail);
bool test_backend_top_banner_contract(std::string& outFail);
bool test_backend_ui_sell_overlay_policy(std::string& outFail);
bool test_backend_shop_snapshot_contract(std::string& outFail);
bool test_shop_card_conversion_contract(std::string& outFail);
bool test_backend_input_slots_contract(std::string& outFail);
bool test_sell_overlay_ui_policy_contract(std::string& outFail);
bool test_video_init_gl_viewport_guard(std::string& outFail);
bool test_video_preferences_parse_and_roundtrip(std::string& outFail);
bool test_renderer_backend_bootstrap_policy(std::string& outFail);
bool test_renderer_startup_diagnostics_contract(std::string& outFail);
bool test_dxgi_adapter_selection_policy(std::string& outFail);
bool test_d3d12_probe_contract(std::string& outFail);
bool test_content_invariants(std::string& outFail);
bool test_battle_invariants(std::string& outFail);
bool test_end_to_end_headless(std::string& outFail);
bool test_movement_invariants(std::string& outFail);
bool test_model_parse_smoke(std::string& outFail);
bool test_model_loader_source_modularity(std::string& outFail);
bool test_gltf_asset_smoke(std::string& outFail);
bool test_combat_anim_index_cache_contract(std::string& outFail);
bool test_script_api_contract(std::string& outFail);
bool test_round_flow_headless(std::string& outFail);
bool test_animset_roles_smoke(std::string& outFail);
bool test_combat_slice_headless(std::string& outFail);
bool test_move_impact_routing(std::string& outFail);
bool test_move_impact_math(std::string& outFail);
bool test_render_pipeline_smoke(std::string& outFail);
bool test_animset_glb_name_smoke(std::string& outFail);
bool test_placement_to_combat_headless(std::string& outFail);
bool test_combat_route_finishes_headless(std::string& outFail);
bool test_animset_clip_name_smoke(std::string& outFail);
bool test_layering_engine_no_game_includes(std::string& outFail);
bool test_state_manager_input_deferral(std::string& outFail);
bool test_mode_split_flow(std::string& outFail);
bool test_mode_split_menu_entries(std::string& outFail);
bool test_shop_layout_invariants(std::string& outFail);
bool test_lua_card_parser_contract(std::string& outFail);
bool test_lua_text_menu_parser_contract(std::string& outFail);
bool test_lua_script_helpers(std::string& outFail);
bool test_shop_system_phase_contract(std::string& outFail);
bool test_gameworld_merge_progression(std::string& outFail);
bool test_gameworld_spawn_bench_flow(std::string& outFail);
bool test_gameworld_nonrender_with_resources_skips_model_load(std::string& outFail);
bool test_gameworld_backend_render_mode_skips_legacy_model_load(std::string& outFail);
bool test_gameworld_income_flow(std::string& outFail);
bool test_gameworld_inventory_healing(std::string& outFail);
bool test_gameworld_reset_economy_state(std::string& outFail);
bool test_gameworld_capture_preconditions(std::string& outFail);
bool test_gameworld_capture_success_resolution(std::string& outFail);
bool test_gameworld_capture_failure_recovery(std::string& outFail);
bool test_gameworld_capture_nonrender_skips_pokeball_model_load(std::string& outFail);
bool test_gameworld_type_line_counts(std::string& outFail);
bool test_gameworld_nearest_enemy_position(std::string& outFail);
bool test_gameworld_heal_player_units_to_full(std::string& outFail);
bool test_gameworld_capture_restore_player_positions(std::string& outFail);
bool test_gameworld_handle_unit_faint_state_reset(std::string& outFail);
bool test_gameworld_leechseed_apply_contract(std::string& outFail);
bool test_gameworld_leechseed_dt_clamp(std::string& outFail);
bool test_gameworld_leechseed_clears_for_invalid_state(std::string& outFail);
bool test_shared_capture_presentation_contract(std::string& outFail);
bool test_shared_capture_overlay_vfx_contract(std::string& outFail);
bool test_shared_particle_billboard_batches_contract(std::string& outFail);
bool test_shared_particle_vfx_bridge_dispatch_contract(std::string& outFail);
bool test_shared_particle_vfx_styles_contract(std::string& outFail);
bool test_shared_tail_fire_atlas_helpers_contract(std::string& outFail);
bool test_shared_tail_fire_exact_gpu_batches_contract(std::string& outFail);
bool test_gameworld_capture_render_snapshot_timing_contract(std::string& outFail);
bool test_shared_growl_vfx_helpers_contract(std::string& outFail);
bool test_shared_growl_wave_bridge_contract(std::string& outFail);
bool test_shared_growl_wave_batches_contract(std::string& outFail);
bool test_shared_world_indexed_batches_contract(std::string& outFail);
bool test_projected_triangle_submit_clears_geometry_cache_key(std::string& outFail);
bool test_d3d12_world_material_constants_contract(std::string& outFail);
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
        {"environment_helpers_contract", &test_environment_helpers_contract},
        {"auto_quit_policy_contract", &test_auto_quit_policy_contract},
        {"game_services_route_helpers", &test_game_services_route_helpers},
        {"game_service_render_routes_contract", &test_game_service_render_routes_contract},
        {"startup_render_route_policy_contract", &test_startup_render_route_policy_contract},
        {"render_route_ownership_contract", &test_render_route_ownership_contract},
        {"state_ui_route_policy_contract", &test_state_ui_route_policy_contract},
        {"render_routes_contract", &test_render_routes_contract},
        {"render_policy_api_contract", &test_render_policy_api_contract},
        {"logbus_recent_lines_contract", &test_logbus_recent_lines_contract},
        {"render_flow_decisions_contract", &test_render_flow_decisions_contract},
        {"backend_render_policy_contract", &test_backend_render_policy_contract},
        {"renderer_parity_contract_baseline", &test_renderer_parity_contract_baseline},
        {"renderer_parity_contract_detects_drift", &test_renderer_parity_contract_detects_drift},
        {"debug_geometry_line_raster_contract", &test_debug_geometry_line_raster_contract},
        {"backend_hud_formatting_contract", &test_backend_hud_formatting_contract},
        {"backend_debug_text_quads_contract", &test_backend_debug_text_quads_contract},
        {"backend_image_path_contract", &test_backend_image_path_contract},
        {"backend_material_shading_contract", &test_backend_material_shading_contract},
        {"backend_mesh_normals_contract", &test_backend_mesh_normals_contract},
        {"backend_procedural_pose_contract", &test_backend_procedural_pose_contract},
        {"backend_model_cache_contract", &test_backend_model_cache_contract},
        {"backend_world_projection_contract", &test_backend_world_projection_contract},
        {"backend_world_proxy_geometry_contract", &test_backend_world_proxy_geometry_contract},
        {"backend_unit_visuals_contract", &test_backend_unit_visuals_contract},
        {"shared_projected_world_scene_helpers_contract", &test_shared_projected_world_scene_helpers_contract},
        {"backend_inventory_overlay_contract", &test_backend_inventory_overlay_contract},
        {"backend_inventory_panel_contract", &test_backend_inventory_panel_contract},
        {"backend_card_layout_model_contract", &test_backend_card_layout_model_contract},
        {"backend_card_visuals_contract", &test_backend_card_visuals_contract},
        {"backend_card_renderer_contract", &test_backend_card_renderer_contract},
        {"backend_shop_hud_model_contract", &test_backend_shop_hud_model_contract},
        {"backend_sell_overlay_model_contract", &test_backend_sell_overlay_model_contract},
        {"backend_status_text_contract", &test_backend_status_text_contract},
        {"backend_ui_scale_contract", &test_backend_ui_scale_contract},
        {"backend_top_banner_contract", &test_backend_top_banner_contract},
        {"backend_ui_sell_overlay_policy", &test_backend_ui_sell_overlay_policy},
        {"backend_shop_snapshot_contract", &test_backend_shop_snapshot_contract},
        {"shop_card_conversion_contract", &test_shop_card_conversion_contract},
        {"backend_input_slots_contract", &test_backend_input_slots_contract},
        {"sell_overlay_ui_policy_contract", &test_sell_overlay_ui_policy_contract},
        {"video_init_gl_viewport_guard", &test_video_init_gl_viewport_guard},
        {"video_preferences_parse_and_roundtrip", &test_video_preferences_parse_and_roundtrip},
        {"renderer_backend_bootstrap_policy", &test_renderer_backend_bootstrap_policy},
        {"renderer_startup_diagnostics_contract", &test_renderer_startup_diagnostics_contract},
        {"dxgi_adapter_selection_policy", &test_dxgi_adapter_selection_policy},
        {"d3d12_probe_contract", &test_d3d12_probe_contract},
        {"content_invariants", &test_content_invariants},
        {"battle_invariants", &test_battle_invariants},
        {"end_to_end_headless", &test_end_to_end_headless},
        {"movement_invariants", &test_movement_invariants},
        {"model_parse_smoke", &test_model_parse_smoke},
        {"model_loader_source_modularity", &test_model_loader_source_modularity},
        {"gltf_asset_smoke", &test_gltf_asset_smoke},
        {"combat_anim_index_cache_contract", &test_combat_anim_index_cache_contract},
        {"script_api_contract", &test_script_api_contract},
        {"round_flow_headless", &test_round_flow_headless},
        {"animset_roles_smoke", &test_animset_roles_smoke},
        {"combat_slice_headless", &test_combat_slice_headless},
        {"move_impact_routing", &test_move_impact_routing},
        {"move_impact_math", &test_move_impact_math},
        {"render_pipeline_smoke", &test_render_pipeline_smoke},
        {"animset_glb_name_smoke", &test_animset_glb_name_smoke},
        {"placement_to_combat_headless", &test_placement_to_combat_headless},
        {"combat_route_finishes_headless", &test_combat_route_finishes_headless},
        {"animset_clip_name_smoke", &test_animset_clip_name_smoke},
        {"layering_engine_no_game_includes", &test_layering_engine_no_game_includes},
        {"state_manager_input_deferral", &test_state_manager_input_deferral},
        {"mode_split_flow", &test_mode_split_flow},
        {"mode_split_menu_entries", &test_mode_split_menu_entries},
        {"shop_layout_invariants", &test_shop_layout_invariants},
        {"lua_card_parser_contract", &test_lua_card_parser_contract},
        {"lua_text_menu_parser_contract", &test_lua_text_menu_parser_contract},
        {"lua_script_helpers", &test_lua_script_helpers},
        {"shop_system_phase_contract", &test_shop_system_phase_contract},
        {"gameworld_merge_progression", &test_gameworld_merge_progression},
        {"gameworld_spawn_bench_flow", &test_gameworld_spawn_bench_flow},
        {"gameworld_nonrender_with_resources_skips_model_load", &test_gameworld_nonrender_with_resources_skips_model_load},
        {"gameworld_backend_render_mode_skips_legacy_model_load", &test_gameworld_backend_render_mode_skips_legacy_model_load},
        {"gameworld_income_flow", &test_gameworld_income_flow},
        {"gameworld_inventory_healing", &test_gameworld_inventory_healing},
        {"gameworld_reset_economy_state", &test_gameworld_reset_economy_state},
        {"gameworld_capture_preconditions", &test_gameworld_capture_preconditions},
        {"gameworld_capture_success_resolution", &test_gameworld_capture_success_resolution},
        {"gameworld_capture_failure_recovery", &test_gameworld_capture_failure_recovery},
        {"gameworld_capture_nonrender_skips_pokeball_model_load", &test_gameworld_capture_nonrender_skips_pokeball_model_load},
        {"gameworld_type_line_counts", &test_gameworld_type_line_counts},
        {"gameworld_nearest_enemy_position", &test_gameworld_nearest_enemy_position},
        {"gameworld_heal_player_units_to_full", &test_gameworld_heal_player_units_to_full},
        {"gameworld_capture_restore_player_positions", &test_gameworld_capture_restore_player_positions},
        {"gameworld_handle_unit_faint_state_reset", &test_gameworld_handle_unit_faint_state_reset},
        {"gameworld_leechseed_apply_contract", &test_gameworld_leechseed_apply_contract},
        {"gameworld_leechseed_dt_clamp", &test_gameworld_leechseed_dt_clamp},
        {"gameworld_leechseed_clears_for_invalid_state", &test_gameworld_leechseed_clears_for_invalid_state},
        {"shared_capture_presentation_contract", &test_shared_capture_presentation_contract},
        {"shared_capture_overlay_vfx_contract", &test_shared_capture_overlay_vfx_contract},
        {"shared_particle_billboard_batches_contract", &test_shared_particle_billboard_batches_contract},
        {"shared_particle_vfx_bridge_dispatch_contract", &test_shared_particle_vfx_bridge_dispatch_contract},
        {"shared_particle_vfx_styles_contract", &test_shared_particle_vfx_styles_contract},
        {"shared_tail_fire_atlas_helpers_contract", &test_shared_tail_fire_atlas_helpers_contract},
        {"shared_tail_fire_exact_gpu_batches_contract", &test_shared_tail_fire_exact_gpu_batches_contract},
        {"gameworld_capture_render_snapshot_timing_contract", &test_gameworld_capture_render_snapshot_timing_contract},
        {"shared_growl_vfx_helpers_contract", &test_shared_growl_vfx_helpers_contract},
        {"shared_growl_wave_bridge_contract", &test_shared_growl_wave_bridge_contract},
        {"shared_growl_wave_batches_contract", &test_shared_growl_wave_batches_contract},
        {"shared_world_indexed_batches_contract", &test_shared_world_indexed_batches_contract},
        {"projected_triangle_submit_clears_geometry_cache_key", &test_projected_triangle_submit_clears_geometry_cache_key},
        {"d3d12_world_material_constants_contract", &test_d3d12_world_material_constants_contract},
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
