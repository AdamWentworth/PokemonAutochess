#include "game/state/BackendShopSnapshot.h"

#include <string>
#include <vector>

bool test_backend_shop_snapshot_contract(std::string& outFail) {
    using game::state::backend_shop::ActionType;
    using game::state::backend_shop::PlacementInput;
    using game::state::backend_shop::Rect;
    using game::state::backend_shop::BuildInput;
    using game::state::backend_shop::Entry;
    using game::state::backend_shop::applyPlacement;
    using game::state::backend_shop::buildEntries;
    using game::state::backend_shop::findByAction;
    using game::state::backend_shop::findByKeyboardSlot;
    using game::state::backend_shop::findByPoint;
    using game::state::backend_shop::keyboardSlotFor;

    {
        BuildInput in;
        in.shopMode = true;
        in.mainCount = 3;
        in.itemCount = 2;
        in.includeItemRow = true;
        in.includeReroll = true;
        in.includeReady = true;
        const auto entries = buildEntries(in);

        if (entries.size() != 7u) {
            outFail = "shop snapshot entry count mismatch";
            return false;
        }
        if (entries[0].action != ActionType::ShopCard || entries[0].keyboardSlot != 1 || entries[0].sourceIndex != 0u) {
            outFail = "first main card entry mismatch";
            return false;
        }
        if (entries[2].action != ActionType::ShopCard || entries[2].keyboardSlot != 3 || entries[2].sourceIndex != 2u) {
            outFail = "third main card entry mismatch";
            return false;
        }
        if (entries[3].action != ActionType::ItemCard || entries[3].keyboardSlot != 4 || entries[3].sourceIndex != 0u) {
            outFail = "first item card entry mismatch";
            return false;
        }
        if (entries[4].action != ActionType::ItemCard || entries[4].keyboardSlot != 5 || entries[4].sourceIndex != 1u) {
            outFail = "second item card entry mismatch";
            return false;
        }
        if (entries[5].action != ActionType::ShopReroll || entries[5].keyboardSlot != 6) {
            outFail = "reroll entry slot mismatch";
            return false;
        }
        if (entries[6].action != ActionType::ShopReady || entries[6].keyboardSlot != 7) {
            outFail = "ready entry slot mismatch";
            return false;
        }

        const Entry* reroll = findByAction(entries, ActionType::ShopReroll, 0);
        if (!reroll || reroll->keyboardSlot != 6) {
            outFail = "findByAction should locate reroll action entry";
            return false;
        }
        if (keyboardSlotFor(entries, ActionType::ShopReady, 0) != 7) {
            outFail = "keyboardSlotFor ready action mismatch";
            return false;
        }
    }

    {
        BuildInput in;
        in.shopMode = true;
        in.mainCount = 3;
        in.itemCount = 2;
        in.includeItemRow = false;
        in.includeReroll = true;
        in.includeReady = true;
        const auto entries = buildEntries(in);
        if (entries.size() != 5u) {
            outFail = "shop snapshot should omit item entries when includeItemRow is false";
            return false;
        }
        if (entries[3].action != ActionType::ShopReroll || entries[3].keyboardSlot != 4) {
            outFail = "reroll slot should shift when item row is hidden";
            return false;
        }
        if (entries[4].action != ActionType::ShopReady || entries[4].keyboardSlot != 5) {
            outFail = "ready slot should shift when item row is hidden";
            return false;
        }
        if (keyboardSlotFor(entries, ActionType::ItemCard, 0) != 0) {
            outFail = "keyboardSlotFor should return zero when item row is omitted";
            return false;
        }
    }

    {
        BuildInput in;
        in.shopMode = false;
        in.mainCount = 2;
        const auto entries = buildEntries(in);
        if (entries.size() != 2u) {
            outFail = "starter snapshot entry count mismatch";
            return false;
        }
        if (entries[0].action != ActionType::StarterCard || entries[1].action != ActionType::StarterCard) {
            outFail = "starter snapshot action kind mismatch";
            return false;
        }
        if (keyboardSlotFor(entries, ActionType::StarterCard, 1) != 2) {
            outFail = "starter keyboard slot mapping mismatch";
            return false;
        }
    }

    {
        std::vector<Entry> entries(2);
        entries[0].keyboardSlot = 1;
        entries[0].x = 10.0f;
        entries[0].y = 20.0f;
        entries[0].w = 40.0f;
        entries[0].h = 30.0f;
        entries[1].keyboardSlot = 2;
        entries[1].x = 100.0f;
        entries[1].y = 120.0f;
        entries[1].w = 30.0f;
        entries[1].h = 20.0f;

        const Entry* e1 = findByKeyboardSlot(entries, 2);
        if (!e1 || e1->keyboardSlot != 2) {
            outFail = "findByKeyboardSlot should return matching entry";
            return false;
        }
        if (findByKeyboardSlot(entries, 9) != nullptr) {
            outFail = "findByKeyboardSlot should return null when slot is missing";
            return false;
        }

        const Entry* hit1 = findByPoint(entries, 25.0f, 35.0f);
        if (!hit1 || hit1->keyboardSlot != 1) {
            outFail = "findByPoint should hit first entry";
            return false;
        }
        const Entry* hit2 = findByPoint(entries, 115.0f, 125.0f);
        if (!hit2 || hit2->keyboardSlot != 2) {
            outFail = "findByPoint should hit second entry";
            return false;
        }
        if (findByPoint(entries, 80.0f, 80.0f) != nullptr) {
            outFail = "findByPoint should return null outside of hit rects";
            return false;
        }
    }

    {
        BuildInput in;
        in.shopMode = true;
        in.mainCount = 2;
        in.itemCount = 1;
        in.includeItemRow = true;
        in.includeReroll = true;
        in.includeReady = true;
        auto entries = buildEntries(in);

        std::vector<Rect> mainRects = {
            Rect{10.0f, 20.0f, 30.0f, 40.0f},
            Rect{50.0f, 60.0f, 70.0f, 80.0f}
        };
        std::vector<Rect> itemRects = {
            Rect{90.0f, 100.0f, 110.0f, 120.0f}
        };
        PlacementInput placement;
        placement.mainRects = &mainRects;
        placement.itemRects = &itemRects;
        placement.rerollRect = Rect{130.0f, 140.0f, 150.0f, 160.0f};
        placement.hasRerollRect = true;
        placement.readyRect = Rect{170.0f, 180.0f, 190.0f, 200.0f};
        placement.hasReadyRect = true;

        applyPlacement(entries, placement);

        if (entries[0].x != 10.0f || entries[0].y != 20.0f || entries[0].w != 30.0f || entries[0].h != 40.0f) {
            outFail = "applyPlacement should map main card index 0 rect";
            return false;
        }
        if (entries[1].x != 50.0f || entries[1].y != 60.0f || entries[1].w != 70.0f || entries[1].h != 80.0f) {
            outFail = "applyPlacement should map main card index 1 rect";
            return false;
        }
        if (entries[2].x != 90.0f || entries[2].y != 100.0f || entries[2].w != 110.0f || entries[2].h != 120.0f) {
            outFail = "applyPlacement should map item card rect";
            return false;
        }
        if (entries[3].x != 130.0f || entries[3].y != 140.0f || entries[3].w != 150.0f || entries[3].h != 160.0f) {
            outFail = "applyPlacement should map reroll rect";
            return false;
        }
        if (entries[4].x != 170.0f || entries[4].y != 180.0f || entries[4].w != 190.0f || entries[4].h != 200.0f) {
            outFail = "applyPlacement should map ready rect";
            return false;
        }
    }

    {
        BuildInput in;
        in.shopMode = true;
        in.mainCount = 1;
        in.includeReroll = true;
        auto entries = buildEntries(in);
        PlacementInput placement;
        // Intentionally omit all placement data to ensure no accidental writes.
        applyPlacement(entries, placement);
        for (const Entry& e : entries) {
            if (e.x != 0.0f || e.y != 0.0f || e.w != 0.0f || e.h != 0.0f) {
                outFail = "applyPlacement should preserve zero rects when placement is missing";
                return false;
            }
        }
    }

    return true;
}
