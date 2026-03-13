#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/startup/RuntimeBackendCardUiPrewarm.h"

namespace {

class RecordingBackend final : public IRenderBackend {
public:
    const char* backendId() const override { return "test"; }
    void beginFrame(float, float, float, float) override { ++beginFrameCalls; }
    void endFrame() override { ++endFrameCalls; }
    void onResize(int, int) override {}
    bool requiresOpenGLContext() const override { return false; }
    bool handlesPresentation() const override { return false; }
    void shutdown() override {}

    void drawDebugQuads(const DebugQuad*, std::size_t quadCount, int, int) override {
        totalQuadCount += quadCount;
    }
    void drawDebugLines(const DebugLine*, std::size_t lineCount, int, int) override {
        totalLineCount += lineCount;
    }
    void drawDebugSprites(const DebugSprite*, std::size_t spriteCount, int, int) override {
        totalSpriteCount += spriteCount;
    }

    int beginFrameCalls = 0;
    int endFrameCalls = 0;
    std::size_t totalQuadCount = 0u;
    std::size_t totalLineCount = 0u;
    std::size_t totalSpriteCount = 0u;
};

} // namespace

bool test_runtime_backend_card_ui_prewarm_contract(std::string& outFail) {
    {
        RecordingBackend backend;
        const auto summary = game::runtime::backend_card_ui_prewarm::run(
            &backend,
            1920,
            1080,
            {
                "assets/ui/frame_gold.png",
                "assets/images/item_placeholder.png",
                "assets/images/pokedollar.png",
                "assets/portraits/charmander_card.png",
                "assets/portraits/squirtle_card.png",
            });

        if (summary.portraitCount != 2u ||
            summary.renderedCardCount != 2u ||
            !summary.submittedFrame ||
            backend.beginFrameCalls != 1 ||
            backend.endFrameCalls != 1 ||
            backend.totalQuadCount == 0u ||
            backend.totalLineCount == 0u) {
            outFail = "RuntimeBackendCardUiPrewarm should filter shared assets, render the remaining card UI once, and submit a frame.";
            return false;
        }
    }

    {
        RecordingBackend backend;
        const auto summary = game::runtime::backend_card_ui_prewarm::run(
            &backend,
            0,
            1080,
            {"assets/portraits/charmander_card.png"});
        if (summary.submittedFrame || backend.beginFrameCalls != 0 || backend.endFrameCalls != 0) {
            outFail = "RuntimeBackendCardUiPrewarm should skip submission when drawable dimensions are invalid.";
            return false;
        }
    }

    return true;
}
