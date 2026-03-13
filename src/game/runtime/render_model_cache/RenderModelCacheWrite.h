#pragma once

#include <string>

#include "game/runtime/render_model_cache/RenderModelCacheSourceBuild.h"

namespace game::runtime::render_model::detail {

bool writeRenderCacheFromSourceData(const std::string& filepath,
                                    const SourceCacheBuildData& data,
                                    std::string* outError);

} // namespace game::runtime::render_model::detail
