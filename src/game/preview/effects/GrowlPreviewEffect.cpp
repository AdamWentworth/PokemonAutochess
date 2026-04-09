#include "game/preview/effects/GrowlPreviewEffect.h"

namespace game::preview {

namespace {

vfx::preview::shared::ControllerBackedPreviewEffectTraits makeGrowlPreviewTraits() {
    vfx::preview::shared::ControllerBackedPreviewEffectTraits traits;
    traits.name = "Growl";
    traits.casterAnimation = {
        .kind = "charged",
        .move = "growl",
        .phase = "one_shot",
    };
    traits.previewPokemonSpecies = {
        .attackerSpecies = "charmander",
        .targetSpecies = "bulbasaur",
    };
    traits.overlayLines = [](const engine::tools::vfx_preview::PreviewSceneState&) {
        return std::vector<std::string>{
        "Growl uses the shared/backend batch path and hot reloads its draw-pass manifest."
        };
    };
    return traits;
}

} // namespace

GrowlPreviewEffect::GrowlPreviewEffect()
    : GrowlPreviewEffectBase("[VfxPreviewer]", makeGrowlPreviewTraits()) {}

} // namespace game::preview
