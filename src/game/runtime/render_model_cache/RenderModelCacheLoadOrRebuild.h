#pragma once

#include <fstream>
#include <string>

#include "game/runtime/render_model_cache/RenderModelCacheFormat.h"

namespace game::runtime::render_model::detail {

using CachePathForModelFn = std::string (*)(const std::string&);
using RebuildCacheFromSourceFn = bool (*)(const std::string&, std::string*);

bool openValidatedCacheStreamForModel(const std::string& modelPath,
                                      CachePathForModelFn cachePathForModelFn,
                                      RebuildCacheFromSourceFn rebuildCacheFromSourceFn,
                                      std::ifstream& outStream,
                                      CacheHeader& outHeader,
                                      std::string* outError);

} // namespace game::runtime::render_model::detail
