#pragma once

#include <iosfwd>
#include <string>

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/render_model_cache/RenderModelCacheFormat.h"

namespace game::runtime::render_model::detail {

bool decodeMeshFromValidatedCacheStream(std::istream& in,
                                        const CacheHeader& hdr,
                                        MeshData& out,
                                        std::string* outError);

} // namespace game::runtime::render_model::detail
