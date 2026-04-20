#pragma once

#include "vfx/preview/leer/LeerPreviewController.h"
#include "vfx/preview/shared/ControllerBackedPreviewEffect.h"

namespace vfx::preview {

using LeerLabPreviewEffectBase = vfx::preview::shared::ControllerBackedPreviewEffect<
    vfx::preview::leer::LeerPreviewController>;

class LeerLabPreviewEffect final : public LeerLabPreviewEffectBase {
public:
    LeerLabPreviewEffect();
};

} // namespace vfx::preview
