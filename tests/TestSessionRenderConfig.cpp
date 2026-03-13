#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/session/SessionRenderConfig.h"

namespace {

std::optional<std::string> readRawEnv(const char* name) {
    if (name == nullptr || *name == '\0') return std::nullopt;

#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) return std::nullopt;
    std::unique_ptr<char, decltype(&std::free)> holder(raw, &std::free);
    return std::string(holder.get());
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) return std::nullopt;
    return std::string(raw);
#endif
}

bool setEnvVar(const char* name, const char* value) {
    if (name == nullptr || *name == '\0') return false;
#if defined(_MSC_VER)
    return _putenv_s(name, value == nullptr ? "" : value) == 0;
#else
    if (value == nullptr) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
#endif
}

struct ScopedEnvVar {
    explicit ScopedEnvVar(std::string key)
        : name(std::move(key))
        , previous(readRawEnv(name.c_str())) {}

    ~ScopedEnvVar() {
        if (previous.has_value()) {
            setEnvVar(name.c_str(), previous->c_str());
        } else {
            setEnvVar(name.c_str(), nullptr);
        }
        game::runtime::session_render_config::resetForTests();
    }

    std::string name;
    std::optional<std::string> previous;
};

class FakeRenderBackend final : public IRenderBackend {
public:
    explicit FakeRenderBackend(std::string backendId)
        : backendId_(std::move(backendId)) {}

    const char* backendId() const override { return backendId_.c_str(); }
    void beginFrame(float, float, float, float) override {}
    void endFrame() override {}
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}

private:
    std::string backendId_;
};

} // namespace

bool test_session_render_config_contract(std::string& outFail) {
    using game::runtime::session_render_config::backendGpuClipSkinningEnabled;

    {
        ScopedEnvVar triLimit("PAC_BACKEND_MODEL_TRI_LIMIT");
        ScopedEnvVar triBudget("PAC_BACKEND_MODEL_TRI_FRAME_BUDGET");

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
        ScopedEnvVar preload("PAC_BACKEND_PRELOAD_MODELS");
        ScopedEnvVar ui("PAC_BACKEND_PREWARM_UI_SPRITES");
        ScopedEnvVar growl("PAC_BACKEND_GROWL_LEGACY_VFX");
        ScopedEnvVar premul("PAC_BACKEND_TAIL_FIRE_PREWARM_PREMUL");

        setEnvVar("PAC_BACKEND_PRELOAD_MODELS", "off");
        setEnvVar("PAC_BACKEND_PREWARM_UI_SPRITES", "FALSE");
        setEnvVar("PAC_BACKEND_GROWL_LEGACY_VFX", "0");
        setEnvVar("PAC_BACKEND_TAIL_FIRE_PREWARM_PREMUL", "1");
        game::runtime::session_render_config::resetForTests();

        if (game::runtime::session_render_config::backendPreloadModelCacheEnabled() ||
            game::runtime::session_render_config::backendUiSpritePrewarmEnabled() ||
            game::runtime::session_render_config::backendUseLegacyGrowlWaveVfxEnabled() ||
            !game::runtime::session_render_config::backendPrewarmLegacyTailFirePremulEnabled()) {
            outFail = "SessionRenderConfig should honor boolean env toggles for startup/render flags.";
            return false;
        }
    }

    {
        ScopedEnvVar global("PAC_BACKEND_GPU_CLIP_SKINNING");
        ScopedEnvVar gl("PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL");
        ScopedEnvVar dx("PAC_BACKEND_GPU_CLIP_SKINNING_D3D12");
        ScopedEnvVar other("PAC_BACKEND_GPU_CLIP_SKINNING_OTHER");

        FakeRenderBackend opengl("opengl");
        FakeRenderBackend d3d12("d3d12");
        FakeRenderBackend test("test");

        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING", "1");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_OPENGL", "0");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_D3D12", "1");
        setEnvVar("PAC_BACKEND_GPU_CLIP_SKINNING_OTHER", "1");
        game::runtime::session_render_config::resetForTests();

        if (backendGpuClipSkinningEnabled(&opengl) ||
            !backendGpuClipSkinningEnabled(&d3d12) ||
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
