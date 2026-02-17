#pragma once

#include <cstddef>
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
    for (const Entry& entry : entries) {
        if (entry.w <= 0.0f || entry.h <= 0.0f) continue;
        if (x < entry.x || x > (entry.x + entry.w)) continue;
        if (y < entry.y || y > (entry.y + entry.h)) continue;
        return &entry;
    }
    return nullptr;
}

} // namespace game::state::backend_shop
