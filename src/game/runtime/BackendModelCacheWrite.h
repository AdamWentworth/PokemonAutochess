#pragma once

#include <string>

#include "game/runtime/BackendModelCacheSourceBuild.h"

namespace game::runtime::backend_model::detail {

bool writeBackendCacheFromSourceData(const std::string& filepath,
                                     const SourceCacheBuildData& data,
                                     std::string* outError);

} // namespace game::runtime::backend_model::detail
