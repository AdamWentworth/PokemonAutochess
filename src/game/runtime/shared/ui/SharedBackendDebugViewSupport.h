#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/ui/InventoryPanel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_backend_debug_view_support {

std::string trimDebugLine(std::string s, std::size_t maxChars);

struct ItemAtlasIcon {
    const char* id;
    int row;
    int col;
};

const ItemAtlasIcon* findItemAtlasIcon(const std::string& id);
glm::vec2 itemAtlasUvMin(int row, int col);
glm::vec2 itemAtlasUvMax(int row, int col);
std::string toLowerCopy(std::string s);

using OverlayHash = std::uint64_t;

inline constexpr OverlayHash kOverlayHashOffset = 1469598103934665603ull;
inline constexpr OverlayHash kOverlayHashPrime = 1099511628211ull;

void hashBytes(OverlayHash& hash, const void* data, std::size_t size);
void hashInt(OverlayHash& hash, int value);
void hashSize(OverlayHash& hash, std::size_t value);
void hashBool(OverlayHash& hash, bool value);
void hashFloatQuantized(OverlayHash& hash, float value, float scale = 1000.0f);
void hashString(OverlayHash& hash, const std::string& value);
void hashVec3(OverlayHash& hash, const glm::vec3& value);

template <typename T>
void appendCachedVector(std::vector<T>& dst, const std::vector<T>& src) {
    if (src.empty()) return;
    dst.insert(dst.end(), src.begin(), src.end());
}

struct RetainedOverlayCache {
    OverlayHash key = 0;
    std::vector<IRenderBackend::DebugQuad> worldQuads;
    std::vector<IRenderBackend::DebugQuad> overlayQuads;
    std::vector<IRenderBackend::DebugLine> lines;
    std::vector<IRenderBackend::DebugLine> textLines;
    std::vector<IRenderBackend::DebugSprite> sprites;
    std::vector<game::runtime::ui_inventory_panel::HitRegion> hitRegions;
};

} // namespace game::runtime::shared_backend_debug_view_support
