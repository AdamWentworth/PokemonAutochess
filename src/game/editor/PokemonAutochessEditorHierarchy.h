#pragma once

#include "engine/editor/EditorProjectPlugin.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace game::editor::hierarchy {

enum class ObjectDomain : std::uint8_t {
    None = 0u,
    GameplayBoard,
    Environment,
    GameplayPreviewUnit,
};

struct ObjectAddress {
    ObjectDomain domain = ObjectDomain::None;
    std::size_t index = 0u;
};

std::size_t objectCount(
    bool sceneViewReady,
    std::size_t environmentObjectCount,
    std::size_t previewUnitCount) noexcept;
ObjectAddress resolveObjectAddress(
    bool sceneViewReady,
    std::size_t environmentObjectCount,
    std::size_t previewUnitCount,
    std::size_t hierarchyIndex) noexcept;

class Selection {
public:
    void select(const char* stableId);
    void select(std::string_view stableId);
    void clear() noexcept;

    const std::string& id() const noexcept;
    bool empty() const noexcept;
    bool matches(std::string_view stableId) const noexcept;

private:
    std::string stableId_;
};

engine::editor::EditorProjectLayoutObject environmentObjectView(
    const game::runtime::route1_environment::LayoutObject& object) noexcept;

struct GameplayBoardViewConfig {
    const char* stableId = nullptr;
    std::array<float, 3> defaultSourceAnchorCm{};
    std::array<std::int32_t, 2> defaultTerrainGridOrigin{};
    float defaultBoardCellSizeWorld = 1.0f;
    float terrainTileSizeCm = 100.0f;
    float terrainElevationStepCm = 50.0f;
};

engine::editor::EditorProjectLayoutObject gameplayBoardView(
    const game::runtime::route1_environment::BoardLayoutTransform& layout,
    const GameplayBoardViewConfig& config) noexcept;

} // namespace game::editor::hierarchy
