#include "game/runtime/loop/RuntimePerfLogging.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace game::runtime::perf_logging {

namespace {

std::string escapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8u);
    for (const char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

} // namespace

const char* terminalLogModeName(EngineTerminalLogMode mode) {
    switch (mode) {
        case EngineTerminalLogMode::TailFireDebug:
            return "Tail Fire Debug";
        case EngineTerminalLogMode::ScratchVfx:
            return "Scratch VFX";
        case EngineTerminalLogMode::GrowlVfx:
            return "Growl VFX";
        case EngineTerminalLogMode::Performance:
        default:
            return "Performance";
    }
}

EngineTerminalLogMode nextTerminalLogMode(EngineTerminalLogMode mode) {
    switch (mode) {
        case EngineTerminalLogMode::Performance:
            return EngineTerminalLogMode::GrowlVfx;
        case EngineTerminalLogMode::GrowlVfx:
            return EngineTerminalLogMode::ScratchVfx;
        case EngineTerminalLogMode::ScratchVfx:
            return EngineTerminalLogMode::TailFireDebug;
        case EngineTerminalLogMode::TailFireDebug:
        default:
            return EngineTerminalLogMode::Performance;
    }
}

std::string formatTopFixedSystems(const EngineFixedPerfBreakdown& fixedBreakdown) {
    struct FixedSystemEntry {
        const char* name;
        float ms;
    };

    std::array<FixedSystemEntry, 10> entries{{
        {"backend_hydrate", fixedBreakdown.backendHydrateMs},
        {"combat", fixedBreakdown.combatMs},
        {"world", fixedBreakdown.worldMs},
        {"movement", fixedBreakdown.movementMs},
        {"round", fixedBreakdown.roundMs},
        {"state", fixedBreakdown.stateManagerMs},
        {"post_other", fixedBreakdown.postOtherMs},
        {"phasechg", fixedBreakdown.phaseTransitionMs},
        {"camera", fixedBreakdown.cameraMs},
        {"unit", fixedBreakdown.unitInteractionMs},
    }};

    std::sort(entries.begin(), entries.end(), [](const FixedSystemEntry& a, const FixedSystemEntry& b) {
        return a.ms > b.ms;
    });

    std::ostringstream out;
    int emitted = 0;
    for (const auto& entry : entries) {
        if (entry.ms < 0.05f) continue;
        out << (emitted == 0 ? " fsys=" : ",") << entry.name << ":" << entry.ms << "ms";
        ++emitted;
        if (emitted >= 3) break;
    }
    return out.str();
}

std::string formatPerfLine(const EngineFramePerfStats& framePerf) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << "[Perf] FPS=" << framePerf.fps
        << " frame=" << framePerf.frameMs << "ms"
        << " fixed=" << framePerf.fixedMs << "ms"
        << " ftick=" << framePerf.fixedTickMs << "ms"
        << " build=" << framePerf.renderBuildMs << "ms"
        << " submit=" << framePerf.renderSubmitMs << "ms"
        << " present=" << framePerf.presentWaitMs << "ms"
        << " gpu=" << (framePerf.gpuFrameValid ? framePerf.gpuFrameMs : -1.0f) << "ms"
        << " draws=" << framePerf.drawCalls
        << " tris=" << framePerf.triangles
        << " units=" << framePerf.visibleAnimatedUnits
        << " particles=" << framePerf.particleCount
        << " proj=" << framePerf.projectedUnitsMs << "ms"
        << " pose=" << framePerf.projectedPoseEvalMs << "ms"
        << " model=" << framePerf.projectedModelMs << "ms"
        << " prep=" << framePerf.projectedModelPrepMs << "ms"
        << " geom=" << framePerf.projectedModelGeometryMs << "ms"
        << " over=" << framePerf.projectedOverlayMs << "ms"
        << " fscene=i" << framePerf.fastSceneInstances
        << "/d" << framePerf.fastSceneDrawClasses
        << "/s" << framePerf.fastSceneVisibleSkeletons
        << "/b" << framePerf.fastSceneMaterialTableBinds
        << " clipskin=" << framePerf.projectedClipSkinnedUnits
        << " path=r" << framePerf.projectedSharedRigidBatches
        << "/g" << framePerf.projectedGpuClipSkinBatches
        << "/p" << framePerf.projectedGpuClipPaletteBatches
        << "/c" << framePerf.projectedCpuRewriteBatches
        << "/i" << framePerf.projectedIndexedBatchesQueued
        << " render=" << framePerf.renderMs << "ms"
        << " swap=" << framePerf.swapMs << "ms"
        << " ticks=" << framePerf.fixedTicks
        << " drop=" << framePerf.fixedTicksDropped
        << formatTopFixedSystems(framePerf.fixedBreakdown);
    return out.str();
}

std::string formatPerfJson(const EngineFramePerfStats& framePerf) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "[PerfJSON] {"
        << "\"fps\":" << framePerf.fps
        << ",\"frame_cpu_ms\":" << framePerf.frameMs
        << ",\"fixed_ms\":" << framePerf.fixedMs
        << ",\"fixed_tick_ms\":" << framePerf.fixedTickMs
        << ",\"render_build_ms\":" << framePerf.renderBuildMs
        << ",\"render_submit_ms\":" << framePerf.renderSubmitMs
        << ",\"present_wait_ms\":" << framePerf.presentWaitMs
        << ",\"gpu_frame_ms\":" << (framePerf.gpuFrameValid ? framePerf.gpuFrameMs : -1.0f)
        << ",\"gpu_frame_valid\":" << (framePerf.gpuFrameValid ? 1 : 0)
        << ",\"draw_calls\":" << framePerf.drawCalls
        << ",\"triangles\":" << framePerf.triangles
        << ",\"backend_indexed_opaque_draws\":" << framePerf.indexedOpaqueDraws
        << ",\"backend_indexed_blend_draws\":" << framePerf.indexedBlendDraws
        << ",\"backend_indexed_cached_draws\":" << framePerf.indexedCachedDraws
        << ",\"backend_indexed_dynamic_draws\":" << framePerf.indexedDynamicDraws
        << ",\"backend_indexed_instanced_draws\":" << framePerf.indexedInstancedDraws
        << ",\"backend_indexed_outline_batches\":" << framePerf.indexedOutlineBatches
        << ",\"backend_indexed_geometry_switches\":" << framePerf.indexedGeometrySwitches
        << ",\"backend_indexed_material_switches\":" << framePerf.indexedMaterialSwitches
        << ",\"backend_indexed_texture_switches\":" << framePerf.indexedTextureSwitches
        << ",\"backend_gl_texture_bind_calls\":" << framePerf.indexedGlTextureBindCalls
        << ",\"backend_d3d12_pso_sets\":" << framePerf.indexedD3d12PsoSets
        << ",\"backend_d3d12_descriptor_table_sets\":"
        << framePerf.indexedD3d12DescriptorTableSets
        << ",\"backend_fast_scene_instances\":" << framePerf.fastSceneInstances
        << ",\"backend_fast_scene_draw_classes\":" << framePerf.fastSceneDrawClasses
        << ",\"backend_fast_scene_visible_skeletons\":"
        << framePerf.fastSceneVisibleSkeletons
        << ",\"backend_fast_scene_palette_upload_bytes\":"
        << framePerf.fastScenePaletteUploadBytes
        << ",\"backend_fast_scene_material_table_binds\":"
        << framePerf.fastSceneMaterialTableBinds
        << ",\"backend_fast_scene_indirect_commands\":"
        << framePerf.fastSceneIndirectCommands
        << ",\"visible_animated_units\":" << framePerf.visibleAnimatedUnits
        << ",\"particle_count\":" << framePerf.particleCount
        << ",\"projected_units_ms\":" << framePerf.projectedUnitsMs
        << ",\"projected_pose_eval_ms\":" << framePerf.projectedPoseEvalMs
        << ",\"projected_model_ms\":" << framePerf.projectedModelMs
        << ",\"projected_model_prep_ms\":" << framePerf.projectedModelPrepMs
        << ",\"projected_model_geometry_ms\":" << framePerf.projectedModelGeometryMs
        << ",\"projected_overlay_ms\":" << framePerf.projectedOverlayMs
        << ",\"projected_units_processed\":" << framePerf.projectedUnitsProcessed
        << ",\"projected_model_units\":" << framePerf.projectedModelUnits
        << ",\"projected_clip_skinned_units\":" << framePerf.projectedClipSkinnedUnits
        << ",\"projected_shared_rigid_batches\":" << framePerf.projectedSharedRigidBatches
        << ",\"projected_gpu_clip_skin_batches\":" << framePerf.projectedGpuClipSkinBatches
        << ",\"projected_gpu_clip_palette_batches\":"
        << framePerf.projectedGpuClipPaletteBatches
        << ",\"projected_cpu_rewrite_batches\":" << framePerf.projectedCpuRewriteBatches
        << ",\"projected_indexed_batches_queued\":"
        << framePerf.projectedIndexedBatchesQueued
        << ",\"render_world_compose_ms\":" << framePerf.renderBreakdown.worldComposeMs
        << ",\"render_world_backdrop_ms\":" << framePerf.renderBreakdown.worldBackdropMs
        << ",\"render_world_vfx_ms\":" << framePerf.renderBreakdown.worldVfxMs
        << ",\"render_world_depth_flush_ms\":" << framePerf.renderBreakdown.worldDepthFlushMs
        << ",\"render_overlay_prep_ms\":" << framePerf.renderBreakdown.overlayPrepMs
        << ",\"render_world_background_ms\":" << framePerf.renderBreakdown.worldBackgroundMs
        << ",\"render_world_triangles_3d_ms\":" << framePerf.renderBreakdown.worldTriangles3dMs
        << ",\"render_world_indexed_ms\":" << framePerf.renderBreakdown.worldIndexedMs
        << ",\"render_world_debug_ms\":" << framePerf.renderBreakdown.worldDebugMs
        << ",\"render_sprite_submit_ms\":" << framePerf.renderBreakdown.spriteMs
        << ",\"render_ui_submit_ms\":" << framePerf.renderBreakdown.uiMs
        << ",\"render_other_ms\":" << framePerf.renderBreakdown.otherMs
        << ",\"legacy_render_ms\":" << framePerf.renderMs
        << ",\"legacy_swap_ms\":" << framePerf.swapMs
        << ",\"fixed_ticks\":" << framePerf.fixedTicks
        << ",\"fixed_phase_pre_ms\":" << framePerf.fixedBreakdown.preUpdateMs
        << ",\"fixed_phase_update_ms\":" << framePerf.fixedBreakdown.updatePhaseMs
        << ",\"fixed_phase_post_ms\":" << framePerf.fixedBreakdown.postUpdateMs
        << ",\"fixed_phase_post_other_ms\":" << framePerf.fixedBreakdown.postOtherMs
        << ",\"fixed_phase_transition_ms\":" << framePerf.fixedBreakdown.phaseTransitionMs
        << ",\"fixed_backend_hydrate_ms\":" << framePerf.fixedBreakdown.backendHydrateMs
        << ",\"fixed_camera_ms\":" << framePerf.fixedBreakdown.cameraMs
        << ",\"fixed_unit_interaction_ms\":" << framePerf.fixedBreakdown.unitInteractionMs
        << ",\"fixed_shop_ms\":" << framePerf.fixedBreakdown.shopMs
        << ",\"fixed_round_ms\":" << framePerf.fixedBreakdown.roundMs
        << ",\"fixed_state_manager_ms\":" << framePerf.fixedBreakdown.stateManagerMs
        << ",\"fixed_state_update_ms\":" << framePerf.fixedBreakdown.stateUpdateMs
        << ",\"fixed_state_flush_ms\":" << framePerf.fixedBreakdown.stateFlushMs
        << ",\"fixed_movement_ms\":" << framePerf.fixedBreakdown.movementMs
        << ",\"fixed_movement_plan_ms\":" << framePerf.fixedBreakdown.movementPlanMs
        << ",\"fixed_movement_lua_ms\":" << framePerf.fixedBreakdown.movementLuaMs
        << ",\"fixed_movement_flush_ms\":" << framePerf.fixedBreakdown.movementFlushMs
        << ",\"fixed_movement_advance_ms\":" << framePerf.fixedBreakdown.movementAdvanceMs
        << ",\"fixed_combat_ms\":" << framePerf.fixedBreakdown.combatMs
        << ",\"fixed_combat_plan_ms\":" << framePerf.fixedBreakdown.combatPlanMs
        << ",\"fixed_combat_lua_ms\":" << framePerf.fixedBreakdown.combatLuaMs
        << ",\"fixed_combat_flush_ms\":" << framePerf.fixedBreakdown.combatFlushMs
        << ",\"fixed_world_ms\":" << framePerf.fixedBreakdown.worldMs
        << ",\"fixed_ticks_dropped\":" << framePerf.fixedTicksDropped
        << "}";
    return out.str();
}

std::string formatGrowlDebugLine(const EngineGrowlDebugStats& growlDebug) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "[Growl] rings=" << growlDebug.activeRingCount
        << " configured=" << growlDebug.configuredPassCount
        << " enabled=" << growlDebug.enabledPassCount
        << " mesh=" << growlDebug.meshPassCount
        << " line=" << growlDebug.linePassCount
        << " quarter_ring=" << growlDebug.quarterRingPassCount
        << " quarter_tex=" << growlDebug.quarterTextureBakePassCount;
    if (!growlDebug.activePasses.empty()) {
        out << " passes=";
        for (std::size_t i = 0; i < growlDebug.activePasses.size(); ++i) {
            const auto& pass = growlDebug.activePasses[i];
            if (i > 0u) out << ",";
            out << pass.eid << ":" << pass.mode
                << "[b" << pass.submittedBatchCount
                << "/v" << pass.submittedVertexCount
                << "/i" << pass.submittedIndexCount << "]";
        }
    }
    return out.str();
}

std::string formatGrowlDebugJson(const EngineGrowlDebugStats& growlDebug) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "[GrowlJSON] {"
        << "\"snapshot_available\":" << (growlDebug.snapshotAvailable ? 1 : 0)
        << ",\"active_rings\":" << growlDebug.activeRingCount
        << ",\"configured_passes\":" << growlDebug.configuredPassCount
        << ",\"enabled_passes\":" << growlDebug.enabledPassCount
        << ",\"mesh_passes\":" << growlDebug.meshPassCount
        << ",\"line_passes\":" << growlDebug.linePassCount
        << ",\"quarter_ring_passes\":" << growlDebug.quarterRingPassCount
        << ",\"quarter_texture_bake_passes\":" << growlDebug.quarterTextureBakePassCount
        << ",\"passes\":[";

    for (std::size_t i = 0; i < growlDebug.activePasses.size(); ++i) {
        const auto& pass = growlDebug.activePasses[i];
        if (i > 0u) out << ",";
        out << "{"
            << "\"id\":\"" << escapeJsonString(pass.id) << "\""
            << ",\"eid\":" << pass.eid
            << ",\"mode\":\"" << escapeJsonString(pass.mode) << "\""
            << ",\"mesh\":\"" << escapeJsonString(pass.meshPath) << "\""
            << ",\"texture\":\"" << escapeJsonString(pass.texturePath) << "\""
            << ",\"quarter_texture_bake\":" << (pass.quarterTextureBake ? 1 : 0)
            << ",\"line_pass\":" << (pass.linePass ? 1 : 0)
            << ",\"scale_mul\":" << pass.scaleMul
            << ",\"alpha_mul\":" << pass.alphaMul
            << ",\"forward_offset\":" << pass.forwardOffset
            << ",\"submitted_batches\":" << pass.submittedBatchCount
            << ",\"submitted_vertices\":" << pass.submittedVertexCount
            << ",\"submitted_indices\":" << pass.submittedIndexCount
            << ",\"submitted_texture_width\":" << pass.submittedTextureWidth
            << ",\"submitted_texture_height\":" << pass.submittedTextureHeight
            << ",\"submitted_translate_x\":" << pass.submittedTranslateX
            << ",\"submitted_translate_y\":" << pass.submittedTranslateY
            << ",\"submitted_translate_z\":" << pass.submittedTranslateZ
            << "}";
    }

    out << "]}";
    return out.str();
}

std::string formatScratchDebugLine(const EngineScratchDebugStats& scratchDebug,
                                   const EngineFramePerfStats& framePerf,
                                   std::string_view reason) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "[ScratchPerf]";
    if (!reason.empty()) {
        out << " reason=" << reason;
    }
    out << " glows=" << scratchDebug.activeGlowCount
        << " rings=" << scratchDebug.snapshotRingCount
        << " configured=" << scratchDebug.configuredPassCount
        << " enabled=" << scratchDebug.enabledPassCount
        << " batches=" << scratchDebug.submittedBatchCount
        << " alpha=" << scratchDebug.submittedAlphaBatchCount
        << " add=" << scratchDebug.submittedAdditiveBatchCount
        << " premul=" << scratchDebug.submittedPremulBatchCount
        << " inst=" << scratchDebug.submittedInstancedBatchCount
        << " dyn=" << scratchDebug.submittedDynamicBatchCount
        << " instances=" << scratchDebug.submittedInstanceCount
        << " verts=" << scratchDebug.submittedVertexCount
        << " idx=" << scratchDebug.submittedIndexCount
        << " frame=" << framePerf.frameMs << "ms"
        << " build=" << framePerf.renderBuildMs << "ms"
        << " gpu=" << (framePerf.gpuFrameValid ? framePerf.gpuFrameMs : -1.0f) << "ms"
        << " draws=" << framePerf.drawCalls
        << " vfx=" << framePerf.renderBreakdown.worldVfxMs << "ms"
        << " combat=" << framePerf.fixedBreakdown.combatMs << "ms"
        << " world=" << framePerf.fixedBreakdown.worldMs << "ms"
        << " tex=" << framePerf.indexedTextureSwitches
        << " bind=" << framePerf.indexedGlTextureBindCalls;
    return out.str();
}

std::string formatScratchDebugJson(const EngineScratchDebugStats& scratchDebug,
                                   const EngineFramePerfStats& framePerf,
                                   std::string_view reason) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "[ScratchPerfJSON] {"
        << "\"reason\":\"" << escapeJsonString(std::string(reason)) << "\""
        << ","
        << "\"snapshot_available\":" << (scratchDebug.snapshotAvailable ? 1 : 0)
        << ",\"active_glows\":" << scratchDebug.activeGlowCount
        << ",\"snapshot_rings\":" << scratchDebug.snapshotRingCount
        << ",\"configured_passes\":" << scratchDebug.configuredPassCount
        << ",\"enabled_passes\":" << scratchDebug.enabledPassCount
        << ",\"submitted_batches\":" << scratchDebug.submittedBatchCount
        << ",\"submitted_alpha_batches\":" << scratchDebug.submittedAlphaBatchCount
        << ",\"submitted_additive_batches\":" << scratchDebug.submittedAdditiveBatchCount
        << ",\"submitted_premul_batches\":" << scratchDebug.submittedPremulBatchCount
        << ",\"submitted_instanced_batches\":" << scratchDebug.submittedInstancedBatchCount
        << ",\"submitted_dynamic_batches\":" << scratchDebug.submittedDynamicBatchCount
        << ",\"submitted_instances\":" << scratchDebug.submittedInstanceCount
        << ",\"submitted_vertices\":" << scratchDebug.submittedVertexCount
        << ",\"submitted_indices\":" << scratchDebug.submittedIndexCount
        << ",\"frame_cpu_ms\":" << framePerf.frameMs
        << ",\"render_build_ms\":" << framePerf.renderBuildMs
        << ",\"render_submit_ms\":" << framePerf.renderSubmitMs
        << ",\"present_wait_ms\":" << framePerf.presentWaitMs
        << ",\"gpu_frame_ms\":" << (framePerf.gpuFrameValid ? framePerf.gpuFrameMs : -1.0f)
        << ",\"gpu_frame_valid\":" << (framePerf.gpuFrameValid ? 1 : 0)
        << ",\"draw_calls\":" << framePerf.drawCalls
        << ",\"triangles\":" << framePerf.triangles
        << ",\"backend_indexed_blend_draws\":" << framePerf.indexedBlendDraws
        << ",\"backend_indexed_instanced_draws\":" << framePerf.indexedInstancedDraws
        << ",\"backend_indexed_texture_switches\":" << framePerf.indexedTextureSwitches
        << ",\"backend_gl_texture_bind_calls\":" << framePerf.indexedGlTextureBindCalls
        << ",\"render_world_vfx_ms\":" << framePerf.renderBreakdown.worldVfxMs
        << ",\"render_world_indexed_ms\":" << framePerf.renderBreakdown.worldIndexedMs
        << ",\"fixed_combat_ms\":" << framePerf.fixedBreakdown.combatMs
        << ",\"fixed_combat_plan_ms\":" << framePerf.fixedBreakdown.combatPlanMs
        << ",\"fixed_world_ms\":" << framePerf.fixedBreakdown.worldMs
        << "}";
    return out.str();
}

} // namespace game::runtime::perf_logging

