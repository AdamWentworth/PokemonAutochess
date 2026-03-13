#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace game::runtime::hud {

struct InventoryEntry {
    std::string id;
    int count = 0;
};

inline std::string humanizeToken(std::string token) {
    for (char& ch : token) {
        if (ch == '_' || ch == '-') ch = ' ';
    }

    bool nextUpper = true;
    for (char& ch : token) {
        const unsigned char u = static_cast<unsigned char>(ch);
        if (std::isspace(u)) {
            nextUpper = true;
            continue;
        }
        ch = static_cast<char>(nextUpper ? std::toupper(u) : std::tolower(u));
        nextUpper = false;
    }
    return token;
}

inline std::vector<InventoryEntry> normalizeInventoryEntries(
    const std::vector<std::pair<std::string, int>>& raw,
    std::size_t maxCount) {
    std::vector<InventoryEntry> out;
    out.reserve(raw.size());
    for (const auto& kv : raw) {
        if (kv.first.empty()) continue;
        if (kv.second <= 0) continue;
        out.push_back(InventoryEntry{kv.first, kv.second});
    }

    std::sort(out.begin(), out.end(), [](const InventoryEntry& a, const InventoryEntry& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.id < b.id;
    });

    if (maxCount > 0 && out.size() > maxCount) {
        out.resize(maxCount);
    }
    return out;
}

inline int clampInventoryOffset(int offset, int visibleCount, std::size_t totalCount) {
    if (visibleCount <= 0) return 0;
    const std::size_t maxOffsetSize =
        (totalCount > static_cast<std::size_t>(visibleCount))
            ? (totalCount - static_cast<std::size_t>(visibleCount))
            : 0u;
    const int maxOffset = static_cast<int>(maxOffsetSize);
    return std::clamp(offset, 0, maxOffset);
}

inline int stepInventoryOffset(int offset, int wheelDelta, int visibleCount, std::size_t totalCount) {
    int next = offset;
    if (wheelDelta > 0) {
        --next;
    } else if (wheelDelta < 0) {
        ++next;
    }
    return clampInventoryOffset(next, visibleCount, totalCount);
}

inline std::vector<InventoryEntry> sliceInventoryEntries(const std::vector<InventoryEntry>& entries,
                                                         int offset,
                                                         std::size_t maxVisible) {
    std::vector<InventoryEntry> out;
    if (entries.empty() || maxVisible == 0) return out;

    const int clampedOffset = clampInventoryOffset(offset, static_cast<int>(maxVisible), entries.size());
    const std::size_t begin = static_cast<std::size_t>(clampedOffset);
    const std::size_t end = std::min(entries.size(), begin + maxVisible);
    out.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) {
        out.push_back(entries[i]);
    }
    return out;
}

inline std::string formatInventoryEntry(const InventoryEntry& item) {
    return humanizeToken(item.id) + " x" + std::to_string(item.count);
}

inline std::string formatTypeLineEntry(const std::string& type, int uniqueLineCount) {
    return humanizeToken(type) + " x" + std::to_string(std::max(0, uniqueLineCount));
}

inline std::string formatUnitEntry(const std::string& name, int level) {
    return humanizeToken(name) + " Lv" + std::to_string(std::max(1, level));
}

inline std::string formatShopCardEntry(const std::string& name, int level, int cost) {
    return formatUnitEntry(name, level) + "  " + std::to_string(std::max(0, cost)) + "g";
}

} // namespace game::runtime::hud
