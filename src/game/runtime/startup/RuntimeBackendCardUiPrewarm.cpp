#include "game/runtime/startup/RuntimeBackendCardUiPrewarm.h"

#include <algorithm>
#include <string>
#include <vector>

#include "engine/render/IRenderBackend.h"
#include "game/runtime/backend_ui/CardRenderer.h"
#include "game/runtime/backend_ui/UiScale.h"
#include "game/runtime/shared/ui/SharedBackendDebugViewOverlay.h"
#include "game/runtime/session/SessionBackendRenderHelpers.h"

namespace game::runtime::backend_card_ui_prewarm {

Summary run(IRenderBackend* renderer,
            int drawableW,
            int drawableH,
            const std::vector<std::string>& uiSpritePrewarmPaths) {
    Summary summary;
    if (!renderer || drawableW <= 0 || drawableH <= 0) return summary;

    std::vector<std::string> portraitPaths;
    portraitPaths.reserve(uiSpritePrewarmPaths.size());
    for (const std::string& path : uiSpritePrewarmPaths) {
        if (path == "assets/ui/frame_gold.png" ||
            path == "assets/images/item_placeholder.png" ||
            path == "assets/images/items_atlas.png" ||
            path == "assets/images/pokedollar.png" ||
            path == "assets/images/pokegold.png") {
            continue;
        }
        portraitPaths.push_back(path);
    }
    summary.portraitCount = portraitPaths.size();

    std::vector<IRenderBackend::DebugQuad> baseQuads;
    std::vector<IRenderBackend::DebugLine> textLines;
    std::vector<IRenderBackend::DebugSprite> sprites;
    baseQuads.reserve(256u);
    textLines.reserve(4096u);
    sprites.reserve(64u);

    const float uiScale = runtime::ui_scale::viewportScale(drawableW, drawableH);
    const float edgePad = runtime::ui_scale::edgePad(drawableW, drawableH);
    const float lineStep = runtime::ui_scale::lineStep(drawableW, drawableH);
    const std::size_t cardCount = std::max<std::size_t>(1u, std::min<std::size_t>(portraitPaths.size(), 5u));
    summary.renderedCardCount = cardCount;
    const float cardW = std::clamp(148.0f * uiScale, 120.0f, 210.0f);
    const float cardH = std::clamp(cardW * 1.30f, 150.0f, 290.0f);
    const float gap = std::max(10.0f, cardW * 0.10f);
    const float totalW =
        static_cast<float>(cardCount) * cardW + static_cast<float>(cardCount - 1u) * gap;
    const float rowX = std::max(edgePad, (static_cast<float>(drawableW) - totalW) * 0.5f);
    const float rowY = std::max(
        64.0f,
        static_cast<float>(drawableH) - cardH - lineStep * 4.5f);

    runtime::ui_text::appendTextLines(
        textLines,
        edgePad,
        std::max(10.0f, edgePad - lineStep * 0.15f),
        "Shop",
        std::clamp(2.6f * uiScale, 1.6f, 3.3f),
        0.95f,
        0.95f,
        0.98f,
        1.0f,
        0.88f);

    for (std::size_t i = 0; i < cardCount; ++i) {
        game::runtime::ui_card_renderer::CardRenderInput input;
        input.x = rowX + static_cast<float>(i) * (cardW + gap);
        input.y = rowY;
        input.w = cardW;
        input.h = cardH;
        if (i < portraitPaths.size()) {
            input.displayName = game::runtime::session_backend_render_helpers::makeBackendCardPrewarmLabel(portraitPaths[i]);
        } else {
            input.displayName = "Card " + std::to_string(i + 1u);
        }
        input.subtitle = "Lv5";
        input.explicitImagePath = "assets/images/item_placeholder.png";
        input.keyboardSlot = static_cast<int>(i + 1u);
        input.item = true;
        input.textScale = std::clamp(1.0f * uiScale, 0.70f, 1.35f);
        game::runtime::ui_card_renderer::appendCardLayered(
            baseQuads,
            nullptr,
            &sprites,
            input,
            &textLines);
    }

    const auto addActionButton = [&](float x,
                                     float y,
                                     const std::string& label,
                                     float r,
                                     float g,
                                     float b) {
        const float textScale = 1.0f * 1.35f * uiScale;
        const float textW = std::max(1.0f, runtime::ui_text::measureTextWidth(label, textScale));
        const float textH = std::max(1.0f, runtime::ui_text::measureTextHeight(label, textScale));
        const float padX = std::max(8.0f, textScale * 4.0f);
        const float padY = std::max(5.0f, textScale * 2.5f);

        IRenderBackend::DebugQuad bg;
        bg.x = x - padX;
        bg.y = y - padY;
        bg.w = textW + padX * 2.0f;
        bg.h = textH + padY * 2.0f;
        bg.r = r;
        bg.g = g;
        bg.b = b;
        bg.a = 0.92f;
        baseQuads.push_back(bg);

        runtime::ui_text::appendTextLines(
            textLines,
            x,
            y,
            label,
            textScale,
            0.98f,
            0.98f,
            0.98f,
            1.0f,
            0.88f);
    };
    addActionButton(edgePad + 88.0f * uiScale, rowY + cardH + lineStep * 1.1f, "[6] Reroll", 0.20f, 0.16f, 0.08f);
    addActionButton(
        static_cast<float>(drawableW) - edgePad - 160.0f * uiScale,
        edgePad + lineStep * 0.95f,
        "[7] Ready",
        0.12f,
        0.25f,
        0.14f);

    if (baseQuads.empty() && sprites.empty() && textLines.empty()) return summary;

    renderer->beginFrame(0.05f, 0.05f, 0.07f, 1.0f);
    summary.submittedFrame = true;
    if (!baseQuads.empty()) {
        renderer->drawDebugQuads(baseQuads.data(), baseQuads.size(), drawableW, drawableH);
    }
    if (!sprites.empty()) {
        renderer->drawDebugSprites(sprites.data(), sprites.size(), drawableW, drawableH);
    }
    if (!textLines.empty()) {
        renderer->drawDebugLines(textLines.data(), textLines.size(), drawableW, drawableH);
    }
    renderer->endFrame();
    return summary;
}

} // namespace game::runtime::backend_card_ui_prewarm



