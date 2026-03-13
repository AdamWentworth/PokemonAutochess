#pragma once

#include "engine/core/EngineServices.h"
#include "game/runtime/renderer/RuntimeRendererActivation.h"

#include <iosfwd>
#include <string>

class IRenderBackend;

namespace game::runtime::renderer_startup_state {

struct OpenGlStrings {
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string glslVersion;
};

game::runtime::renderer_activation::Inputs makeActivationInputs(const EngineServices& services,
                                                                const IRenderBackend& renderer,
                                                                const OpenGlStrings& openGlStrings);

game::runtime::renderer_activation::Outputs applyAndLog(EngineServices& services,
                                                        const game::runtime::renderer_activation::Inputs& inputs,
                                                        std::ostream& logOut);

} // namespace game::runtime::renderer_startup_state

