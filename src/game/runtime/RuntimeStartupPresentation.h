#pragma once

#include "engine/render/Camera3D.h"

#include <functional>
#include <memory>
#include <string>

class IRenderBackend;

namespace game::runtime::startup_presentation {

struct FontInitResult {
    bool succeeded = true;
    std::string error;
};

FontInitResult initializeFonts(const std::function<int()>& initFonts,
                               const std::function<std::string()>& readError);

std::unique_ptr<Camera3D> createDefaultCamera(int drawableW, int drawableH);

bool primeInitialLoadingFrame(IRenderBackend* renderer,
                              int drawableW,
                              int drawableH,
                              const std::function<void(float)>& renderBootLoading);

} // namespace game::runtime::startup_presentation
