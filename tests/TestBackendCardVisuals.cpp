#include "game/runtime/BackendCardVisuals.h"

#include <string>
#include <vector>

bool test_backend_card_visuals_contract(std::string& outFail) {
    using game::runtime::backend_cards::CardVisualInput;
    using game::runtime::backend_cards::appendStylizedCard;
    using game::runtime::backend_cards::fnv1aHash;

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
        in.subtitle = "Lv 5  Cost 3g";
        in.keyboardSlot = 1;
        appendStylizedCard(quads, in, 0.9f);
        if (quads.size() < 7u) {
            outFail = "appendStylizedCard should emit multiple layered quads for a valid card";
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
        in.subtitle = "Cost 2g";
        in.item = true;
        in.keyboardSlot = 4;
        appendStylizedCard(withBadge, in, 0.85f);
        in.keyboardSlot = 0;
        appendStylizedCard(withoutBadge, in, 0.85f);
        if (withBadge.size() <= withoutBadge.size()) {
            outFail = "keyboard slot badge should add extra debug quads";
            return false;
        }
    }

    return true;
}

