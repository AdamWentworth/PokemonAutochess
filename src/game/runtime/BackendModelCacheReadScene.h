#pragma once

#include <iosfwd>
#include <string>

#include "game/runtime/BackendModelCache.h"
#include "game/runtime/BackendModelCacheFormat.h"

namespace game::runtime::backend_model::detail {

bool readSceneFromValidatedCacheStream(std::istream& in,
                                       const CacheHeader& hdr,
                                       MeshData& out,
                                       std::string* outError);

} // namespace game::runtime::backend_model::detail

