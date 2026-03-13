#include "game/runtime/render_model_cache/RenderModelCacheWrite.h"

#include "game/runtime/render_model_cache/RenderModelCache.h"
#include "game/runtime/render_model_cache/RenderModelCacheFormat.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {
namespace fs = std::filesystem;

using game::runtime::render_model::detail::CacheHeader;
using game::runtime::render_model::detail::CacheTextureHeader;

template <typename T>
bool writePod(std::ostream& out, const T& value) {
    return static_cast<bool>(out.write(reinterpret_cast<const char*>(&value), sizeof(T)));
}

bool writeString(std::ostream& out, const std::string& s) {
    const std::uint32_t n = static_cast<std::uint32_t>(s.size());
    if (!writePod(out, n)) return false;
    if (n == 0u) return true;
    return static_cast<bool>(out.write(s.data(), n));
}

bool writeTexture(std::ostream& out, const Model::CPUTexture& tex) {
    const CacheTextureHeader h{
        static_cast<std::int32_t>(tex.width),
        static_cast<std::int32_t>(tex.height),
        static_cast<std::int32_t>(tex.wrapS),
        static_cast<std::int32_t>(tex.wrapT),
        static_cast<std::int32_t>(tex.minF),
        static_cast<std::int32_t>(tex.magF),
        static_cast<std::uint32_t>(tex.rgba.size()),
    };
    if (!writePod(out, h.width) ||
        !writePod(out, h.height) ||
        !writePod(out, h.wrapS) ||
        !writePod(out, h.wrapT) ||
        !writePod(out, h.minF) ||
        !writePod(out, h.magF) ||
        !writePod(out, h.bytes)) {
        return false;
    }
    if (h.bytes == 0u) return true;
    return static_cast<bool>(out.write(reinterpret_cast<const char*>(tex.rgba.data()),
                                       static_cast<std::streamsize>(tex.rgba.size())));
}

bool sourceMetadataForModel(const std::string& modelPath,
                            std::uint64_t& outFileSize,
                            std::int64_t& outWriteTime) {
    std::error_code ec;
    if (!fs::exists(modelPath, ec) || ec) return false;
    outFileSize = static_cast<std::uint64_t>(fs::file_size(modelPath, ec));
    if (ec) return false;
    outWriteTime = static_cast<std::int64_t>(fs::last_write_time(modelPath, ec).time_since_epoch().count());
    return !ec;
}

} // namespace

namespace game::runtime::render_model::detail {

bool writeRenderCacheFromSourceData(const std::string& filepath,
                                    const SourceCacheBuildData& data,
                                    std::string* outError) {
    if (data.submeshes.size() > std::numeric_limits<std::uint32_t>::max()) {
        if (outError) *outError = "too many submeshes for cache";
        return false;
    }
    if (data.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        data.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        if (outError) *outError = "too much geometry for cache";
        return false;
    }

    std::uint64_t srcFileSize = 0u;
    std::int64_t srcWriteTime = 0;
    if (!sourceMetadataForModel(filepath, srcFileSize, srcWriteTime)) {
        if (outError) *outError = "failed to read source file metadata";
        return false;
    }

    const fs::path cpath = cachePathForModel(filepath);
    std::error_code ec;
    fs::create_directories(fs::path(cpath).parent_path(), ec);
    if (ec) {
        if (outError) *outError = "failed to create cache directory";
        return false;
    }

    CacheHeader hdr{};
    hdr.srcFileSize = srcFileSize;
    hdr.srcWriteTime = srcWriteTime;
    hdr.modelScaleFactor = data.modelScaleFactor;
    hdr.vertexCount = static_cast<std::uint32_t>(data.vertices.size());
    hdr.indexCount = static_cast<std::uint32_t>(data.indices.size());
    hdr.submeshCount = static_cast<std::uint32_t>(data.submeshes.size());
    hdr.nodeCount = static_cast<std::uint32_t>(data.nodesDefault.size());
    hdr.skinCount = static_cast<std::uint32_t>(data.skins.size());
    hdr.animCount = static_cast<std::uint32_t>(data.animations.size());

    std::ofstream out(cpath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (outError) *outError = "failed to open cache file for write";
        return false;
    }

    if (!writePod(out, hdr)) {
        if (outError) *outError = "failed to write cache header";
        return false;
    }

    for (const auto& n : data.nodesDefault) {
        std::uint8_t hm = n.hasMatrix ? 1u : 0u;
        if (!writePod(out, n.t) ||
            !writePod(out, n.r) ||
            !writePod(out, n.s) ||
            !writePod(out, hm) ||
            !writePod(out, n.matrix)) {
            if (outError) *outError = "failed to write cache nodes";
            return false;
        }
    }
    for (std::uint32_t i = 0; i < hdr.nodeCount; ++i) {
        const std::string& nodeName =
            (i < data.nodeNames.size()) ? data.nodeNames[static_cast<std::size_t>(i)] : std::string{};
        if (!writeString(out, nodeName)) {
            if (outError) *outError = "failed to write cache node names";
            return false;
        }
    }
    for (const auto& children : data.nodeChildren) {
        const std::uint32_t cc = static_cast<std::uint32_t>(children.size());
        if (!writePod(out, cc)) {
            if (outError) *outError = "failed to write cache node children";
            return false;
        }
        for (int v : children) {
            const std::int32_t vv = static_cast<std::int32_t>(v);
            if (!writePod(out, vv)) {
                if (outError) *outError = "failed to write cache node child";
                return false;
            }
        }
    }
    for (int v : data.nodeMesh) {
        const std::int32_t vv = static_cast<std::int32_t>(v);
        if (!writePod(out, vv)) {
            if (outError) *outError = "failed to write cache nodeMesh";
            return false;
        }
    }
    for (int v : data.nodeSkin) {
        const std::int32_t vv = static_cast<std::int32_t>(v);
        if (!writePod(out, vv)) {
            if (outError) *outError = "failed to write cache nodeSkin";
            return false;
        }
    }
    {
        const std::uint32_t rc = static_cast<std::uint32_t>(data.sceneRoots.size());
        if (!writePod(out, rc)) {
            if (outError) *outError = "failed to write cache scene roots";
            return false;
        }
        for (int v : data.sceneRoots) {
            const std::int32_t vv = static_cast<std::int32_t>(v);
            if (!writePod(out, vv)) {
                if (outError) *outError = "failed to write cache scene root";
                return false;
            }
        }
    }
    for (const auto& s : data.skins) {
        const std::uint32_t jc = static_cast<std::uint32_t>(s.joints.size());
        if (!writePod(out, jc)) {
            if (outError) *outError = "failed to write cache skin joint count";
            return false;
        }
        for (int v : s.joints) {
            const std::int32_t vv = static_cast<std::int32_t>(v);
            if (!writePod(out, vv)) {
                if (outError) *outError = "failed to write cache skin joints";
                return false;
            }
        }
        for (const auto& m : s.inverseBind) {
            if (!writePod(out, m)) {
                if (outError) *outError = "failed to write cache inverse bind";
                return false;
            }
        }
    }
    for (const auto& a : data.animations) {
        if (!writeString(out, a.name) || !writePod(out, a.durationSec)) {
            if (outError) *outError = "failed to write cache animation header";
            return false;
        }
        const std::uint32_t sc = static_cast<std::uint32_t>(a.samplers.size());
        if (!writePod(out, sc)) {
            if (outError) *outError = "failed to write cache sampler count";
            return false;
        }
        for (const auto& s : a.samplers) {
            const std::uint8_t iv = s.isVec4 ? 1u : 0u;
            if (!writeString(out, s.interpolation) || !writePod(out, iv)) {
                if (outError) *outError = "failed to write cache sampler";
                return false;
            }
            const std::uint32_t ic = static_cast<std::uint32_t>(s.inputs.size());
            if (!writePod(out, ic)) {
                if (outError) *outError = "failed to write cache sampler inputs count";
                return false;
            }
            for (float v : s.inputs) {
                if (!writePod(out, v)) {
                    if (outError) *outError = "failed to write cache sampler input";
                    return false;
                }
            }
            const std::uint32_t oc = static_cast<std::uint32_t>(s.outputs.size());
            if (!writePod(out, oc)) {
                if (outError) *outError = "failed to write cache sampler outputs count";
                return false;
            }
            for (const auto& v : s.outputs) {
                if (!writePod(out, v)) {
                    if (outError) *outError = "failed to write cache sampler output";
                    return false;
                }
            }
        }
        const std::uint32_t cc = static_cast<std::uint32_t>(a.channels.size());
        if (!writePod(out, cc)) {
            if (outError) *outError = "failed to write cache channel count";
            return false;
        }
        for (const auto& ch : a.channels) {
            const std::int32_t si = static_cast<std::int32_t>(ch.samplerIndex);
            const std::int32_t tn = static_cast<std::int32_t>(ch.targetNode);
            const std::uint8_t path =
                (ch.path == pac_model_types::ChannelPath::Rotation) ? 1u :
                (ch.path == pac_model_types::ChannelPath::Scale) ? 2u : 0u;
            if (!writePod(out, si) || !writePod(out, tn) || !writePod(out, path)) {
                if (outError) *outError = "failed to write cache animation channel";
                return false;
            }
        }
    }

    if (!data.vertices.empty() &&
        !out.write(reinterpret_cast<const char*>(data.vertices.data()),
                   static_cast<std::streamsize>(data.vertices.size() * sizeof(pac_model_types::Vertex)))) {
        if (outError) *outError = "failed to write cache vertices";
        return false;
    }
    if (!data.indices.empty() &&
        !out.write(reinterpret_cast<const char*>(data.indices.data()),
                   static_cast<std::streamsize>(data.indices.size() * sizeof(std::uint32_t)))) {
        if (outError) *outError = "failed to write cache indices";
        return false;
    }

    for (const auto& sm : data.submeshes) {
        const std::uint64_t off = static_cast<std::uint64_t>(sm.indexOffset);
        const std::uint64_t cnt = static_cast<std::uint64_t>(sm.indexCount);
        const std::int32_t meshIdx = static_cast<std::int32_t>(sm.meshIndex);
        const std::uint8_t doubleSided = sm.doubleSided ? 1u : 0u;
        if (!writePod(out, off) ||
            !writePod(out, cnt) ||
            !writePod(out, meshIdx) ||
            !writePod(out, sm.emissiveFactor.x) ||
            !writePod(out, sm.emissiveFactor.y) ||
            !writePod(out, sm.emissiveFactor.z) ||
            !writePod(out, sm.normalScale) ||
            !writePod(out, sm.metallicFactor) ||
            !writePod(out, sm.roughnessFactor) ||
            !writePod(out, sm.occlusionStrength) ||
            !writePod(out, sm.alphaMode) ||
            !writePod(out, sm.alphaCutoff) ||
            !writePod(out, doubleSided) ||
            !writeTexture(out, sm.baseTexture) ||
            !writeTexture(out, sm.normalTexture) ||
            !writeTexture(out, sm.metallicRoughnessTexture) ||
            !writeTexture(out, sm.occlusionTexture) ||
            !writeTexture(out, sm.emissiveTexture)) {
            if (outError) *outError = "failed to write cache submesh/texture";
            return false;
        }
    }

    return true;
}

} // namespace game::runtime::render_model::detail
