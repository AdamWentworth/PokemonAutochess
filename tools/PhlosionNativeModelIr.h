#pragma once

#include "game/runtime/render_model_cache/RenderModelCache.h"

#include <string>

namespace tools::phlosion_native_model_ir {

bool load(
    const std::string& manifestPath,
    game::runtime::render_model::MeshData& out,
    std::string* outError = nullptr);

} // namespace tools::phlosion_native_model_ir
