#include <string>

#include "game/runtime/session/SessionRenderConfig.h"
#include "game/runtime/startup/RuntimeStartupAssetPrewarm.h"
#include "TestEnvVarUtils.h"
#include "TestRenderBackendDoubles.h"

namespace {
using test::env_utils::ScopedEnvVar;
using test::env_utils::setEnvVar;
using test::render_doubles::ConfigurableFakeRenderBackend;
using test::render_doubles::FakeRenderBackendConfig;

} // namespace

bool test_session_render_config_contract(std::string& outFail) {
    using game::runtime::session_render_config::backendGpuClipSkinningEnabled;

    {
        ScopedEnvVar triLimit("PAC_BACKEND_MODEL_TRI_LIMIT", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar triBudget("PAC_BACKEND_MODEL_TRI_FRAME_BUDGET", []() {
            game::runtime::session_render_config::resetForTests();
        });

        setEnvVar("PAC_BACKEND_MODEL_TRI_LIMIT", "99");
        setEnvVar("PAC_BACKEND_MODEL_TRI_FRAME_BUDGET", "900000");
        game::runtime::session_render_config::resetForTests();
        if (game::runtime::session_render_config::backendModelTriangleLimit() != 512u ||
            game::runtime::session_render_config::backendModelTriangleFrameBudget() != 720000u) {
            outFail = "SessionRenderConfig should clamp backend triangle env values to safe bounds.";
            return false;
        }
    }

    {
        ScopedEnvVar preload("PAC_BACKEND_PRELOAD_MODELS", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar ui("PAC_BACKEND_PREWARM_UI_SPRITES", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar growlPrewarm("PAC_BACKEND_PREWARM_GROWL_VFX", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar tacklePrewarm("PAC_BACKEND_PREWARM_TACKLE_VFX", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar scratchPrewarm("PAC_BACKEND_PREWARM_SCRATCH_VFX", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar particlePrewarm("PAC_BACKEND_PREWARM_PARTICLE_VFX", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar growl("PAC_BACKEND_GROWL_LEGACY_VFX", []() {
            game::runtime::session_render_config::resetForTests();
        });

        setEnvVar("PAC_BACKEND_PRELOAD_MODELS", "off");
        setEnvVar("PAC_BACKEND_PREWARM_UI_SPRITES", "FALSE");
        setEnvVar("PAC_BACKEND_PREWARM_GROWL_VFX", "0");
        setEnvVar("PAC_BACKEND_PREWARM_TACKLE_VFX", "0");
        setEnvVar("PAC_BACKEND_PREWARM_SCRATCH_VFX", "0");
        setEnvVar("PAC_BACKEND_PREWARM_PARTICLE_VFX", "0");
        setEnvVar("PAC_BACKEND_GROWL_LEGACY_VFX", "0");
        game::runtime::session_render_config::resetForTests();

        if (game::runtime::session_render_config::backendPreloadModelCacheEnabled() ||
            game::runtime::session_render_config::backendUiSpritePrewarmEnabled() ||
            game::runtime::session_render_config::backendPrewarmAuthoredVfxEnabled(
                game::runtime::startup_asset_prewarm::AuthoredVfxKind::Growl) ||
            game::runtime::session_render_config::backendPrewarmAuthoredVfxEnabled(
                game::runtime::startup_asset_prewarm::AuthoredVfxKind::Tackle) ||
            game::runtime::session_render_config::backendPrewarmAuthoredVfxEnabled(
                game::runtime::startup_asset_prewarm::AuthoredVfxKind::Scratch) ||
            game::runtime::session_render_config::backendPrewarmGrowlVfxEnabled() ||
            game::runtime::session_render_config::backendPrewarmTackleVfxEnabled() ||
            game::runtime::session_render_config::backendPrewarmScratchVfxEnabled() ||
            game::runtime::session_render_config::backendPrewarmParticleVfxEnabled() ||
            game::runtime::session_render_config::backendUseLegacyGrowlWaveVfxEnabled()) {
            outFail = "SessionRenderConfig should honor boolean env toggles for startup/render flags.";
            return false;
        }
    }

    {
        ScopedEnvVar global("PAC_BACKEND_GPU_CLIP_SKINNING", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar gl("PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar dx("PAC_BACKEND_GPU_CLIP_SKINNING_D3D12", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar vk("PAC_BACKEND_GPU_CLIP_SKINNING_VULKAN", []() {
            game::runtime::session_render_config::resetForTests();
        });
        ScopedEnvVar other("PAC_BACKEND_GPU_CLIP_SKINNING_OTHER", []() {
            game::runtime::session_render_config::resetForTests();
        });

        ConfigurableFakeRenderBackend opengl(FakeRenderBackendConfig{.backendId = "opengl"});
        ConfigurableFakeRenderBackend d3d12(FakeRenderBackendConfig{.backendId = "d3d12"});
        ConfigurableFakeRenderBackend vulkan(FakeRenderBackendConfig{.backendId = "vulkan"});
        ConfigurableFakeRenderBackend test(FakeRenderBackendConfig{.backendId = "test"});

        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING", "1");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL", "0");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_D3D12", "1");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_VULKAN", "1");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_OTHER", "1");
        game::runtime::session_render_config::resetForTests();

        if (backendGpuClipSkinningEnabled(&opengl) ||
            !backendGpuClipSkinningEnabled(&d3d12) ||
            !backendGpuClipSkinningEnabled(&vulkan) ||
            !backendGpuClipSkinningEnabled(&test)) {
            outFail = "SessionRenderConfig should apply global and per-backend GPU clip-skinning gates.";
            return false;
        }

        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING", "0");
        game::runtime::session_render_config::resetForTests();
        if (backendGpuClipSkinningEnabled(&d3d12)) {
            outFail = "SessionRenderConfig should let the global GPU clip-skinning gate disable native backends.";
            return false;
        }
    }

    return true;
}
