#pragma once

#include "game/vfx/preview/leer/LeerPreviewController.h"
#include "engine/vfx/preview/shared/ControllerBackedPreviewEffect.h"

namespace vfx::preview {

using LeerLabPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::leer::LeerPreviewController>;

class LeerLabPreviewEffect final : public LeerLabPreviewEffectBase {
public:
    LeerLabPreviewEffect();
};

} // namespace vfx::preview
