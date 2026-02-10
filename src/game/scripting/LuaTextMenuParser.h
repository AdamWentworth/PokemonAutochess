#pragma once

#include <sol/sol.hpp>

#include <string>
#include <vector>

namespace game::scripting {

struct TextMenuEntryData {
    std::string id;
    std::string label;
    float scale = 1.0f;
    bool enabled = true;
    bool bold = false;
    bool underline = false;
    bool hasCustomX = false;
    bool hasCustomY = false;
    float xFrac = 0.5f;
    float yFrac = 0.5f;
    bool anchorCenter = true;
    bool hasColor = false;
    float colorR = 1.0f;
    float colorG = 1.0f;
    float colorB = 1.0f;
};

bool parseTextMenuEntries(sol::protected_function fn,
                          std::vector<TextMenuEntryData>& out,
                          std::string* outError = nullptr);

} // namespace game::scripting
