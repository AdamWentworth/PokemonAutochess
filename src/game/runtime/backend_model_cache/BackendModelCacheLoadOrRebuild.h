#pragma once

#include <fstream>
#include <string>

#include "game/runtime/backend_model_cache/BackendModelCacheFormat.h"

namespace game::runtime::backend_model::detail {

using CachePathForModelFn = std::string (*)(const std::string&);
using RebuildCacheFromSourceFn = bool (*)(const std::string&, std::string*);

bool openValidatedCacheStreamForModel(const std::string& modelPath,
                                      CachePathForModelFn cachePathForModelFn,
                                      RebuildCacheFromSourceFn rebuildCacheFromSourceFn,
                                      std::ifstream& outStream,
                                      CacheHeader& outHeader,
                                      std::string* outError);

} // namespace game::runtime::backend_model::detail
