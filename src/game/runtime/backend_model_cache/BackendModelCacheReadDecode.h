#pragma once

#include <iosfwd>
#include <string>

#include "game/runtime/backend_model_cache/BackendModelCache.h"
#include "game/runtime/backend_model_cache/BackendModelCacheFormat.h"

namespace game::runtime::backend_model::detail {

bool decodeMeshFromValidatedCacheStream(std::istream& in,
                                        const CacheHeader& hdr,
                                        MeshData& out,
                                        std::string* outError);

} // namespace game::runtime::backend_model::detail
