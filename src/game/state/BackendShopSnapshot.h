#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace game::state::backend_shop {

enum class ActionType {
    ShopCard,
    StarterCard,
    ItemCard,
    ShopReroll,
    ShopReady
};

struct Entry {
    ActionType action = ActionType::ShopCard;
    std::size_t sourceIndex = 0;
    int keyboardSlot = 0;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct PlacementInput {
    const std::vector<Rect>* mainRects = nullptr;
    const std::vector<Rect>* itemRects = nullptr;
    Rect rerollRect;
    bool hasRerollRect = false;
    Rect readyRect;
    bool hasReadyRect = false;
};

struct BuildInput {
    bool shopMode = true;
    std::size_t mainCount = 0;
    std::size_t itemCount = 0;
    bool includeItemRow = false;
    bool includeReroll = false;
    bool includeReady = false;
};

inline std::vector<Entry> buildEntries(const BuildInput& in) {
    std::vector<Entry> out;
    const std::size_t reserveCount =
        in.mainCount +
        (in.includeItemRow ? in.itemCount : 0u) +
        (in.includeReroll ? 1u : 0u) +
        (in.includeReady ? 1u : 0u);
    out.reserve(reserveCount);

    int slot = 1;
    for (std::size_t i = 0; i < in.mainCount; ++i) {
        Entry entry;
        entry.action = in.shopMode ? ActionType::ShopCard : ActionType::StarterCard;
        entry.sourceIndex = i;
        entry.keyboardSlot = slot++;
        out.push_back(entry);
    }

    if (in.includeItemRow) {
        for (std::size_t i = 0; i < in.itemCount; ++i) {
            Entry entry;
            entry.action = ActionType::ItemCard;
            entry.sourceIndex = i;
            entry.keyboardSlot = slot++;
            out.push_back(entry);
        }
    }

    if (in.includeReroll) {
        Entry entry;
        entry.action = ActionType::ShopReroll;
        entry.keyboardSlot = slot++;
        out.push_back(entry);
    }

    if (in.includeReady) {
        Entry entry;
        entry.action = ActionType::ShopReady;
        entry.keyboardSlot = slot++;
        out.push_back(entry);
    }

    return out;
}

inline const Entry* findByKeyboardSlot(const std::vector<Entry>& entries, int keyboardSlot) {
    if (keyboardSlot <= 0) return nullptr;
    for (const Entry& entry : entries) {
        if (entry.keyboardSlot == keyboardSlot) return &entry;
    }
    return nullptr;
}

inline const Entry* findByAction(const std::vector<Entry>& entries,
                                 ActionType action,
                                 std::size_t sourceIndex = 0) {
    for (const Entry& entry : entries) {
        if (entry.action != action) continue;
        if (entry.sourceIndex != sourceIndex) continue;
        return &entry;
    }
    return nullptr;
}

inline int keyboardSlotFor(const std::vector<Entry>& entries,
                           ActionType action,
                           std::size_t sourceIndex = 0) {
    const Entry* entry = findByAction(entries, action, sourceIndex);
    return entry ? entry->keyboardSlot : 0;
}

inline const Entry* findByPoint(const std::vector<Entry>& entries, float x, float y) {
    // First pass: exact hit region.
    for (const Entry& entry : entries) {
        if (entry.w <= 0.0f || entry.h <= 0.0f) continue;
        if (x < entry.x || x > (entry.x + entry.w)) continue;
        if (y < entry.y || y > (entry.y + entry.h)) continue;
        return &entry;
    }

    // Second pass: forgiving hit region so slightly-off clicks still select
    // the intended menu/card target.
    const Entry* best = nullptr;
    float bestDistSq = std::numeric_limits<float>::max();
    for (const Entry& entry : entries) {
        if (entry.w <= 0.0f || entry.h <= 0.0f) continue;

        float pad = 10.0f;
        switch (entry.action) {
            case ActionType::ShopCard:
            case ActionType::StarterCard:
            case ActionType::ItemCard:
                pad = 18.0f;
                break;
            case ActionType::ShopReroll:
            case ActionType::ShopReady:
                pad = 10.0f;
                break;
        }

        const float left = entry.x - pad;
        const float right = entry.x + entry.w + pad;
        const float top = entry.y - pad;
        const float bottom = entry.y + entry.h + pad;
        if (x < left || x > right || y < top || y > bottom) continue;

        const float cx = entry.x + entry.w * 0.5f;
        const float cy = entry.y + entry.h * 0.5f;
        const float dx = x - cx;
        const float dy = y - cy;
        const float distSq = dx * dx + dy * dy;
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best = &entry;
        }
    }
    if (best) return best;

    return nullptr;
}

inline void applyPlacement(std::vector<Entry>& entries, const PlacementInput& in) {
    for (Entry& entry : entries) {
        switch (entry.action) {
            case ActionType::ShopCard:
            case ActionType::StarterCard:
                if (in.mainRects && entry.sourceIndex < in.mainRects->size()) {
                    const Rect& r = (*in.mainRects)[entry.sourceIndex];
                    entry.x = r.x;
                    entry.y = r.y;
                    entry.w = r.w;
                    entry.h = r.h;
                }
                break;
            case ActionType::ItemCard:
                if (in.itemRects && entry.sourceIndex < in.itemRects->size()) {
                    const Rect& r = (*in.itemRects)[entry.sourceIndex];
                    entry.x = r.x;
                    entry.y = r.y;
                    entry.w = r.w;
                    entry.h = r.h;
                }
                break;
            case ActionType::ShopReroll:
                if (in.hasRerollRect) {
                    entry.x = in.rerollRect.x;
                    entry.y = in.rerollRect.y;
                    entry.w = in.rerollRect.w;
                    entry.h = in.rerollRect.h;
                }
                break;
            case ActionType::ShopReady:
                if (in.hasReadyRect) {
                    entry.x = in.readyRect.x;
                    entry.y = in.readyRect.y;
                    entry.w = in.readyRect.w;
                    entry.h = in.readyRect.h;
                }
                break;
        }
    }
}

} // namespace game::state::backend_shop
