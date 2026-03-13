#include "game/runtime/shared/ui/SharedBackendDebugViewSupport.h"

#include <string>
#include <vector>

bool test_shared_backend_debug_view_support_contract(std::string& outFail) {
    namespace support = game::runtime::shared_backend_debug_view_support;

    if (support::trimDebugLine("abcdef", 4) != "a..." ||
        support::trimDebugLine("abc", 3) != "abc") {
        outFail = "Shared debug view support should trim long lines without truncating short lines.";
        return false;
    }

    {
        const auto* icon = support::findItemAtlasIcon("pokeball");
        if (!icon || icon->row != 1 || icon->col != 4 ||
            support::findItemAtlasIcon("missing") != nullptr) {
            outFail = "Shared debug view support should resolve known atlas icons and reject unknown ids.";
            return false;
        }
    }

    {
        const glm::vec2 uvMin = support::itemAtlasUvMin(2, 4);
        const glm::vec2 uvMax = support::itemAtlasUvMax(2, 4);
        if (!(uvMin.x < uvMax.x) || !(uvMin.y < uvMax.y)) {
            outFail = "Shared debug view support should produce increasing atlas UV bounds.";
            return false;
        }
    }

    if (support::toLowerCopy("OpenGL D3D12") != "opengl d3d12") {
        outFail = "Shared debug view support should lowercase mixed-case text consistently.";
        return false;
    }

    {
        support::OverlayHash a = support::kOverlayHashOffset;
        support::OverlayHash b = support::kOverlayHashOffset;
        support::hashString(a, "fps");
        support::hashVec3(a, glm::vec3(1.0f, 0.5f, 0.25f));
        support::hashString(b, "fps");
        support::hashVec3(b, glm::vec3(1.0f, 0.5f, 0.25f));
        if (a != b) {
            outFail = "Shared debug view support hashing should be deterministic for identical input.";
            return false;
        }
    }

    {
        std::vector<int> values{1, 2};
        const std::vector<int> extra{3, 4};
        support::appendCachedVector(values, extra);
        if (values.size() != 4u || values[2] != 3 || values[3] != 4) {
            outFail = "Shared debug view support should append cached vector contents without dropping entries.";
            return false;
        }
    }

    return true;
}
