#include "game/runtime/ui/UiScale.h"

#include <cmath>
#include <string>

namespace {

bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

bool test_ui_scale_contract(std::string& outFail) {
    using game::runtime::ui_scale::edgePad;
    using game::runtime::ui_scale::lineStep;
    using game::runtime::ui_scale::scaled;
    using game::runtime::ui_scale::viewportScale;

    if (!approx(viewportScale(1280, 720), 1.0f)) {
        outFail = "viewportScale should be 1.0 at 1280x720 baseline";
        return false;
    }
    if (!approx(viewportScale(2560, 1440), 1.45f)) {
        outFail = "viewportScale should clamp to max bound";
        return false;
    }
    if (!approx(viewportScale(640, 360), 0.72f)) {
        outFail = "viewportScale should clamp to min bound";
        return false;
    }
    if (!approx(viewportScale(0, 720), 1.0f)) {
        outFail = "viewportScale should return fallback scale for invalid dimensions";
        return false;
    }

    const float s = viewportScale(1920, 1080);
    const float p = scaled(24.0f, s, 10.0f, 48.0f);
    if (p < 10.0f || p > 48.0f) {
        outFail = "scaled should clamp into requested range";
        return false;
    }

    const float e = edgePad(1920, 1080);
    const float ls = lineStep(1920, 1080);
    if (!(e >= 10.0f && e <= 48.0f)) {
        outFail = "edgePad should remain inside safe bounds";
        return false;
    }
    if (!(ls >= 12.0f && ls <= 28.0f)) {
        outFail = "lineStep should remain inside safe bounds";
        return false;
    }

    return true;
}




