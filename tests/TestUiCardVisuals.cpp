#include "game/runtime/backend_ui/CardVisuals.h"
#include "engine/render/SpriteTextureCardArt.h"

#include <string>
#include <vector>

bool test_ui_card_visuals_contract(std::string& outFail) {
    using game::runtime::ui_cards::CardVisualInput;
    using game::runtime::ui_cards::appendStylizedCard;
    using game::runtime::ui_cards::appendStylizedCardLayered;
    using game::runtime::ui_cards::computeCardVisualLayout;
    using game::runtime::ui_cards::fnv1aHash;
    using game::runtime::ui_cards::makeCardArtSprite;
    using game::runtime::ui_cards::makeCardFrameSprite;
    using game::runtime::ui_cards::resolveCardImagePath;
    using engine::render::sprite_card_art::isProxyPath;
    using engine::render::sprite_card_art::sourcePathFromProxy;

    if (fnv1aHash("charmander") != fnv1aHash("charmander")) {
        outFail = "fnv1aHash should be deterministic for identical input";
        return false;
    }
    if (fnv1aHash("charmander") == fnv1aHash("squirtle")) {
        outFail = "fnv1aHash should vary across different names";
        return false;
    }

    {
        std::vector<IRenderBackend::DebugQuad> quads;
        CardVisualInput in;
        in.x = 100.0f;
        in.y = 80.0f;
        in.w = 220.0f;
        in.h = 150.0f;
        in.title = "Charmander";
        in.subtitle = "Lv5";
        appendStylizedCard(quads, in, 0.9f);
        if (quads.size() < 1u) {
            outFail = "appendStylizedCard should emit backing geometry for a valid card";
            return false;
        }
        if (quads.size() < 5u) {
            outFail = "appendStylizedCard should emit explicit border geometry for frame visibility fallback";
            return false;
        }
        if (quads[1].r < 0.80f || quads[1].g < 0.60f) {
            outFail = "appendStylizedCard border fallback should be gold-tinted";
            return false;
        }

        const auto layout = computeCardVisualLayout(in);
        if (layout.artW <= 0.0f || layout.artH <= 0.0f) {
            outFail = "computeCardVisualLayout should emit positive art dimensions";
            return false;
        }
        const auto sprite = makeCardArtSprite(
            in,
            resolveCardImagePath("", "charmander", false),
            1.0f);
        if (!isProxyPath(sprite.texturePath) ||
            sourcePathFromProxy(sprite.texturePath) != "assets/images/charmander.png") {
            outFail = "card sprite path should resolve to backend card art proxy";
            return false;
        }
        if (sprite.w <= 0.0f || sprite.h <= 0.0f) {
            outFail = "card sprite should have positive geometry";
            return false;
        }
        const auto frame = makeCardFrameSprite(in, 1.0f);
        if (frame.texturePath != "assets/ui/frame_gold.png") {
            outFail = "card frame sprite should resolve to legacy gold frame asset";
            return false;
        }
        if (frame.w != in.w || frame.h != in.h) {
            outFail = "card frame sprite should cover full card rect";
            return false;
        }

        const auto uvSprite = makeCardArtSprite(
            in,
            resolveCardImagePath("", "charmander", false),
            1.0f,
            0.75f,
            0.80f,
            0.25f,
            0.20f);
        if (uvSprite.u0 != 0.25f || uvSprite.v0 != 0.20f ||
            uvSprite.u1 != 0.75f || uvSprite.v1 != 0.80f) {
            outFail = "card sprite UVs should preserve normalized atlas crop bounds";
            return false;
        }
    }

    {
        CardVisualInput in;
        in.x = 48.0f;
        in.y = 40.0f;
        in.w = 196.0f;
        in.h = 132.0f;
        in.title = "Squirtle";
        in.subtitle = "Lv 4  Cost 2g";
        in.keyboardSlot = 2;

        std::vector<IRenderBackend::DebugQuad> baseQuads;
        std::vector<IRenderBackend::DebugQuad> textQuads;
        std::vector<IRenderBackend::DebugLine> textLines;
        appendStylizedCardLayered(baseQuads, &textQuads, in, 0.92f, &textLines);
        if (baseQuads.empty()) {
            outFail = "appendStylizedCardLayered should emit base card geometry";
            return false;
        }
        if (textLines.empty()) {
            outFail = "appendStylizedCardLayered should emit text line geometry when line sink is provided";
            return false;
        }
        if (!textQuads.empty()) {
            outFail = "appendStylizedCardLayered should avoid text quad emission when line sink is provided";
            return false;
        }
    }

    {
        CardVisualInput in;
        in.x = 48.0f;
        in.y = 40.0f;
        in.w = 196.0f;
        in.h = 132.0f;
        in.title = "Bulbasaur";
        in.subtitle = "Lv 3";

        std::vector<IRenderBackend::DebugQuad> baseQuads;
        appendStylizedCardLayered(baseQuads, nullptr, in, 0.92f, nullptr);
        if (baseQuads.empty()) {
            outFail = "appendStylizedCardLayered should still emit base geometry when text layers are omitted";
            return false;
        }
    }

    {
        std::vector<IRenderBackend::DebugQuad> quads;
        CardVisualInput in;
        in.x = 12.0f;
        in.y = 10.0f;
        in.w = 0.0f;
        in.h = 100.0f;
        in.title = "Invalid";
        appendStylizedCard(quads, in, 1.0f);
        if (!quads.empty()) {
            outFail = "appendStylizedCard should not emit quads for zero-sized cards";
            return false;
        }
    }

    {
        std::vector<IRenderBackend::DebugQuad> withBadge;
        std::vector<IRenderBackend::DebugQuad> withoutBadge;
        CardVisualInput in;
        in.x = 24.0f;
        in.y = 24.0f;
        in.w = 180.0f;
        in.h = 120.0f;
        in.title = "Potion";
        in.subtitle = "Lv2";
        in.item = true;
        in.keyboardSlot = 4; // keyboard slot should not affect OpenGL-style visuals
        appendStylizedCard(withBadge, in, 0.85f);
        in.keyboardSlot = 0;
        appendStylizedCard(withoutBadge, in, 0.85f);
        if (withBadge.size() != withoutBadge.size()) {
            outFail = "keyboard slot should not alter OpenGL-style card backing geometry";
            return false;
        }
    }

    {
        const std::string itemPath = resolveCardImagePath("", "Potion", true);
        if (itemPath != "assets/images/item_placeholder.png") {
            outFail = "item cards should default to item placeholder portrait";
            return false;
        }
        const std::string explicitPath =
            resolveCardImagePath("assets/images/charmander.png", "pikachu", false);
        if (!isProxyPath(explicitPath) ||
            sourcePathFromProxy(explicitPath) != "assets/images/charmander.png") {
            outFail = "existing explicit card image path should map to backend card art proxy";
            return false;
        }

        const std::string missingExplicit =
            resolveCardImagePath("assets/images/custom_missing.png", "pikachu", false);
        if (missingExplicit != "assets/images/item_placeholder.png") {
            outFail = "missing explicit image path should fall back to placeholder";
            return false;
        }
    }

    return true;
}



