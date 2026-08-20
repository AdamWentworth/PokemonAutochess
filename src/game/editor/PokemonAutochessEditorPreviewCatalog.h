#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace game::editor::preview_catalog {

struct Definition {
    const char* id = "";
    const char* displayName = "";
    const char* group = "";
    const char* description = "";
    const char* state = "";
    const char* gameMode = "";
    const char* snapshot = "";
    const char* sceneId = "";
};

inline constexpr std::size_t kDefinitionCount = 28u;
using Definitions = std::array<Definition, kDefinitionCount>;

const Definitions& all() noexcept;
const Definition* find(std::string_view id) noexcept;

} // namespace game::editor::preview_catalog
