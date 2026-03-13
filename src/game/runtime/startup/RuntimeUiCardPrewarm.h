#pragma once

#include <cstddef>
#include <string>
#include <vector>

class IRenderBackend;

namespace game::runtime::ui_card_prewarm {

struct Summary {
    std::size_t portraitCount = 0u;
    std::size_t renderedCardCount = 0u;
    bool submittedFrame = false;
};

Summary run(IRenderBackend* renderer,
            int drawableW,
            int drawableH,
            const std::vector<std::string>& uiSpritePrewarmPaths);

} // namespace game::runtime::ui_card_prewarm
