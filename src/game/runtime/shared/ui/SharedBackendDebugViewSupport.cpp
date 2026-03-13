#include "game/runtime/shared/ui/SharedBackendDebugViewSupport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace game::runtime::shared_backend_debug_view_support {

std::string trimDebugLine(std::string s, std::size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

const ItemAtlasIcon* findItemAtlasIcon(const std::string& id) {
    static const ItemAtlasIcon kIcons[] = {
        {"pokeball", 1, 4},
        {"potion", 2, 4},
        {"burn_heal", 2, 6},
        {"antidote", 2, 5},
        {"paralyze_heal", 2, 9},
    };
    for (const auto& icon : kIcons) {
        if (id == icon.id) return &icon;
    }
    return nullptr;
}

glm::vec2 itemAtlasUvMin(int row, int col) {
    constexpr int kCols = 13;
    constexpr int kRows = 14;
    constexpr float kPadU = 0.08f;
    constexpr float kPadV = 0.08f;
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u0 = static_cast<float>(c - 1) / static_cast<float>(kCols);
    float v0 = static_cast<float>(r - 1) / static_cast<float>(kRows);
    u0 += (kPadU / static_cast<float>(kCols));
    v0 += (kPadV / static_cast<float>(kRows));
    return {u0, v0};
}

glm::vec2 itemAtlasUvMax(int row, int col) {
    constexpr int kCols = 13;
    constexpr int kRows = 14;
    constexpr float kPadURight = 0.06f;
    constexpr float kPadVBottom = 0.06f;
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u1 = static_cast<float>(c) / static_cast<float>(kCols);
    float v1 = static_cast<float>(r) / static_cast<float>(kRows);
    u1 -= (kPadURight / static_cast<float>(kCols));
    v1 -= (kPadVBottom / static_cast<float>(kRows));
    return {u1, v1};
}

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

void hashBytes(OverlayHash& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<OverlayHash>(bytes[i]);
        hash *= kOverlayHashPrime;
    }
}

void hashInt(OverlayHash& hash, int value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashSize(OverlayHash& hash, std::size_t value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashBool(OverlayHash& hash, bool value) {
    const unsigned char byte = value ? 1u : 0u;
    hashBytes(hash, &byte, sizeof(byte));
}

void hashFloatQuantized(OverlayHash& hash, float value, float scale) {
    const int quantized = static_cast<int>(std::lround(static_cast<double>(value) * scale));
    hashInt(hash, quantized);
}

void hashString(OverlayHash& hash, const std::string& value) {
    hashSize(hash, value.size());
    if (!value.empty()) {
        hashBytes(hash, value.data(), value.size());
    }
}

void hashVec3(OverlayHash& hash, const glm::vec3& value) {
    hashFloatQuantized(hash, value.r);
    hashFloatQuantized(hash, value.g);
    hashFloatQuantized(hash, value.b);
}

} // namespace game::runtime::shared_backend_debug_view_support
