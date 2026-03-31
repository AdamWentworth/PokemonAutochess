#include "engine/render/IRenderBackend.h"

#include <string>
#include <type_traits>

bool test_render_backend_interface_split_contract(std::string& outFail) {
    if (!std::is_base_of_v<IRenderBackendFrame, IRenderBackend>) {
        outFail = "IRenderBackend no longer derives from IRenderBackendFrame";
        return false;
    }
    if (!std::is_base_of_v<IRenderBackendWorld, IRenderBackend>) {
        outFail = "IRenderBackend no longer derives from IRenderBackendWorld";
        return false;
    }
    if (!std::is_base_of_v<IRenderBackendDebug, IRenderBackend>) {
        outFail = "IRenderBackend no longer derives from IRenderBackendDebug";
        return false;
    }
    if (!std::is_same_v<IRenderBackend::WorldTextureData,
                        engine::render::backend::WorldTextureData>) {
        outFail = "IRenderBackend::WorldTextureData drifted from shared backend types";
        return false;
    }
    if (!std::is_same_v<IRenderBackend::DebugQuad,
                        engine::render::backend::DebugQuad>) {
        outFail = "IRenderBackend::DebugQuad drifted from shared backend types";
        return false;
    }

    return true;
}
