#include "game/runtime/BackendDebugText.h"

#include <cmath>
#include <string>
#include <vector>

bool test_backend_debug_text_quads_contract(std::string& outFail) {
    using game::runtime::backend_text::appendTextQuads;
    using game::runtime::backend_text::measureTextHeight;
    using game::runtime::backend_text::measureTextWidth;

    const std::string sample = "Backend HUD";
    const float w1 = measureTextWidth(sample, 1.0f);
    const float w2 = measureTextWidth(sample, 2.0f);
    if (!(w1 > 0.0f)) {
        outFail = "expected positive width for non-empty text";
        return false;
    }
    if (!(w2 > w1 * 1.5f)) {
        outFail = "width scaling mismatch";
        return false;
    }

    const float h1 = measureTextHeight(sample, 1.0f);
    const float h2 = measureTextHeight(sample, 0.5f);
    if (!(h1 > 0.0f && h2 > 0.0f && h2 < h1)) {
        outFail = "height scaling mismatch";
        return false;
    }

    std::vector<IRenderBackend::DebugQuad> quads;
    appendTextQuads(quads, 10.0f, 20.0f, sample, 1.0f, 0.8f, 0.9f, 1.0f, 0.95f);
    if (quads.empty()) {
        outFail = "expected quads for non-empty text";
        return false;
    }
    for (const auto& q : quads) {
        if (!(q.w > 0.0f && q.h > 0.0f)) {
            outFail = "text quad has non-positive size";
            return false;
        }
    }

    const std::size_t before = quads.size();
    appendTextQuads(quads, 0.0f, 0.0f, "", 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (quads.size() != before) {
        outFail = "empty text should not append quads";
        return false;
    }

    if (measureTextWidth("", 1.0f) != 0.0f || measureTextHeight("", 1.0f) != 0.0f) {
        outFail = "empty text measurements should be zero";
        return false;
    }

    return true;
}

