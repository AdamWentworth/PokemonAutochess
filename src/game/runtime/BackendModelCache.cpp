#include "game/runtime/BackendModelCache.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "engine/core/Environment.h"
#include "engine/render/ModelAnimationTypes.h"
#include "engine/render/ModelMeshTypes.h"
#include "game/runtime/BackendMaterialShading.h"

namespace {
namespace fs = std::filesystem;

template <typename T>
bool readPod(std::istream& in, T& out) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
}

bool readString(std::istream& in, std::string& out) {
    std::uint32_t n = 0;
    if (!readPod(in, n)) return false;
    out.clear();
    if (n == 0) return true;
    out.resize(n);
    return static_cast<bool>(in.read(out.data(), n));
}

std::uint64_t fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string hexHash64(std::uint64_t v) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << v;
    return out.str();
}

constexpr std::uint64_t kModelCacheMagic = 0x4C444D434150554FULL;
constexpr std::uint32_t kModelCacheVersion = 4;

#pragma pack(push, 1)
struct CacheHeader {
    std::uint64_t magic = kModelCacheMagic;
    std::uint32_t version = kModelCacheVersion;
    std::int64_t srcWriteTime = 0;
    std::uint64_t srcFileSize = 0;
    float modelScaleFactor = 1.0f;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t submeshCount = 0;
    std::uint32_t nodeCount = 0;
    std::uint32_t skinCount = 0;
    std::uint32_t animCount = 0;
};
#pragma pack(pop)

bool skipNodes(std::istream& in, std::uint32_t nodeCount) {
    using pac_model_types::NodeTRS;
    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        NodeTRS n{};
        std::uint8_t hasMatrix = 0;
        if (!readPod(in, n.t) || !readPod(in, n.r) || !readPod(in, n.s)) return false;
        if (!readPod(in, hasMatrix)) return false;
        if (!readPod(in, n.matrix)) return false;
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::uint32_t childCount = 0;
        if (!readPod(in, childCount)) return false;
        for (std::uint32_t k = 0; k < childCount; ++k) {
            std::int32_t ignored = 0;
            if (!readPod(in, ignored)) return false;
        }
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::int32_t ignored = 0;
        if (!readPod(in, ignored)) return false;
    }
    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::int32_t ignored = 0;
        if (!readPod(in, ignored)) return false;
    }

    std::uint32_t rootCount = 0;
    if (!readPod(in, rootCount)) return false;
    for (std::uint32_t i = 0; i < rootCount; ++i) {
        std::int32_t ignored = 0;
        if (!readPod(in, ignored)) return false;
    }
    return true;
}

bool skipSkins(std::istream& in, std::uint32_t skinCount) {
    for (std::uint32_t si = 0; si < skinCount; ++si) {
        std::uint32_t jointCount = 0;
        if (!readPod(in, jointCount)) return false;
        for (std::uint32_t j = 0; j < jointCount; ++j) {
            std::int32_t ignored = 0;
            if (!readPod(in, ignored)) return false;
        }
        for (std::uint32_t j = 0; j < jointCount; ++j) {
            glm::mat4 ignored(1.0f);
            if (!readPod(in, ignored)) return false;
        }
    }
    return true;
}

bool skipAnimations(std::istream& in, std::uint32_t animCount) {
    for (std::uint32_t ai = 0; ai < animCount; ++ai) {
        std::string name;
        float duration = 0.0f;
        if (!readString(in, name) || !readPod(in, duration)) return false;

        std::uint32_t samplerCount = 0;
        if (!readPod(in, samplerCount)) return false;
        for (std::uint32_t s = 0; s < samplerCount; ++s) {
            std::string interpolation;
            std::uint8_t isVec4 = 0;
            if (!readString(in, interpolation) || !readPod(in, isVec4)) return false;

            std::uint32_t inputCount = 0;
            if (!readPod(in, inputCount)) return false;
            for (std::uint32_t i = 0; i < inputCount; ++i) {
                float ignored = 0.0f;
                if (!readPod(in, ignored)) return false;
            }

            std::uint32_t outputCount = 0;
            if (!readPod(in, outputCount)) return false;
            for (std::uint32_t i = 0; i < outputCount; ++i) {
                glm::vec4 ignored(0.0f);
                if (!readPod(in, ignored)) return false;
            }
        }

        std::uint32_t channelCount = 0;
        if (!readPod(in, channelCount)) return false;
        for (std::uint32_t c = 0; c < channelCount; ++c) {
            std::int32_t ignoredSampler = -1;
            std::int32_t ignoredTarget = -1;
            std::uint8_t ignoredPath = 0;
            if (!readPod(in, ignoredSampler) ||
                !readPod(in, ignoredTarget) ||
                !readPod(in, ignoredPath)) {
                return false;
            }
        }
    }
    return true;
}

struct CacheTextureHeader {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t wrapS = 0;
    std::int32_t wrapT = 0;
    std::int32_t minF = 0;
    std::int32_t magF = 0;
    std::uint32_t bytes = 0;
};

struct DecodedTexture {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
    glm::vec4 average{1.0f};

    bool hasPixels() const {
        if (width <= 0 || height <= 0) return false;
        const std::uint64_t pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        const std::uint64_t requiredBytes = pixels * 4ull;
        return requiredBytes > 0ull && requiredBytes <= static_cast<std::uint64_t>(rgba.size());
    }
};

bool readTexture(std::istream& in, DecodedTexture& out, bool keepPixels) {
    out = DecodedTexture{};

    CacheTextureHeader h{};
    if (!readPod(in, h.width) ||
        !readPod(in, h.height) ||
        !readPod(in, h.wrapS) ||
        !readPod(in, h.wrapT) ||
        !readPod(in, h.minF) ||
        !readPod(in, h.magF) ||
        !readPod(in, h.bytes)) {
        return false;
    }

    out.width = h.width;
    out.height = h.height;

    constexpr std::uint32_t kMaxTextureBytes = 64u * 1024u * 1024u;
    if (h.bytes > kMaxTextureBytes) return false;
    if (h.bytes == 0) {
        out.average = glm::vec4(1.0f);
        return true;
    }

    std::vector<unsigned char> rgba(h.bytes, 0u);
    if (!in.read(reinterpret_cast<char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()))) {
        return false;
    }

    const std::size_t pixelCount = rgba.size() / 4u;
    if (pixelCount == 0u) return true;

    const std::size_t maxSamples = 4096u;
    const std::size_t step = std::max<std::size_t>(1u, pixelCount / maxSamples);
    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    double sumA = 0.0;
    std::size_t samples = 0u;
    for (std::size_t i = 0; i < pixelCount; i += step) {
        const std::size_t b = i * 4u;
        sumR += static_cast<double>(rgba[b + 0u]) / 255.0;
        sumG += static_cast<double>(rgba[b + 1u]) / 255.0;
        sumB += static_cast<double>(rgba[b + 2u]) / 255.0;
        sumA += static_cast<double>(rgba[b + 3u]) / 255.0;
        ++samples;
    }
    if (samples == 0u) return true;

    out.average = glm::vec4(
        static_cast<float>(sumR / static_cast<double>(samples)),
        static_cast<float>(sumG / static_cast<double>(samples)),
        static_cast<float>(sumB / static_cast<double>(samples)),
        static_cast<float>(sumA / static_cast<double>(samples)));

    if (keepPixels) {
        out.rgba = std::move(rgba);
    }
    return true;
}

glm::vec4 sampleTextureNearest(const DecodedTexture& tex, const glm::vec2& uv) {
    if (!tex.hasPixels()) return tex.average;

    float u = uv.x - std::floor(uv.x);
    float v = uv.y - std::floor(uv.y);
    if (u < 0.0f) u += 1.0f;
    if (v < 0.0f) v += 1.0f;

    const int w = std::max(1, tex.width);
    const int h = std::max(1, tex.height);
    const int x = std::clamp(static_cast<int>(std::floor(u * static_cast<float>(w))), 0, w - 1);
    const int y = std::clamp(static_cast<int>(std::floor((1.0f - v) * static_cast<float>(h))), 0, h - 1);
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
    if (idx + 3u >= tex.rgba.size()) return tex.average;

    return glm::vec4(
        static_cast<float>(tex.rgba[idx + 0u]) / 255.0f,
        static_cast<float>(tex.rgba[idx + 1u]) / 255.0f,
        static_cast<float>(tex.rgba[idx + 2u]) / 255.0f,
        static_cast<float>(tex.rgba[idx + 3u]) / 255.0f);
}

} // namespace

namespace game::runtime::backend_model {

std::string cachePathForModel(const std::string& modelPath) {
    const fs::path out = fs::path("cache") / "models" / (hexHash64(fnv1a64(modelPath)) + ".pacmdl");
    return out.string();
}

bool loadMeshFromCache(const std::string& modelPath, MeshData& out, std::string* outError) {
    out = MeshData{};
    if (engine::env::truthyNonZero("PAC_DISABLE_MODELCACHE")) {
        if (outError) *outError = "model cache disabled by PAC_DISABLE_MODELCACHE";
        return false;
    }
    if (modelPath.empty()) {
        if (outError) *outError = "empty model path";
        return false;
    }

    const std::string cachePath = cachePathForModel(modelPath);
    std::ifstream in(cachePath, std::ios::binary);
    if (!in.is_open()) {
        if (outError) *outError = "cache file not found: " + cachePath;
        return false;
    }

    CacheHeader hdr{};
    if (!readPod(in, hdr)) {
        if (outError) *outError = "failed to read cache header";
        return false;
    }
    if (hdr.magic != kModelCacheMagic || hdr.version != kModelCacheVersion) {
        if (outError) *outError = "cache format mismatch";
        return false;
    }

    if (!skipNodes(in, hdr.nodeCount) ||
        !skipSkins(in, hdr.skinCount) ||
        !skipAnimations(in, hdr.animCount)) {
        if (outError) *outError = "failed while skipping cache scene/animation sections";
        return false;
    }

    constexpr std::uint32_t kMaxVertices = 2'000'000;
    constexpr std::uint32_t kMaxIndices = 6'000'000;
    constexpr std::uint32_t kMaxSubmeshes = std::numeric_limits<std::uint16_t>::max();
    if (hdr.vertexCount > kMaxVertices || hdr.indexCount > kMaxIndices || hdr.submeshCount > kMaxSubmeshes) {
        if (outError) *outError = "cache geometry exceeds safety limits";
        return false;
    }

    std::vector<pac_model_types::Vertex> cpuVertices(hdr.vertexCount);
    std::vector<std::uint32_t> cpuIndices(hdr.indexCount);
    if (hdr.vertexCount > 0 &&
        !in.read(reinterpret_cast<char*>(cpuVertices.data()),
                 static_cast<std::streamsize>(cpuVertices.size() * sizeof(pac_model_types::Vertex)))) {
        if (outError) *outError = "failed to read cache vertices";
        return false;
    }
    if (hdr.indexCount > 0 &&
        !in.read(reinterpret_cast<char*>(cpuIndices.data()),
                 static_cast<std::streamsize>(cpuIndices.size() * sizeof(std::uint32_t)))) {
        if (outError) *outError = "failed to read cache indices";
        return false;
    }

    out.modelScaleFactor = hdr.modelScaleFactor;
    out.vertices.reserve(cpuVertices.size());
    out.indices = std::move(cpuIndices);

    bool anyNonWhiteColor = false;
    for (const auto& v : cpuVertices) {
        MeshVertex mv;
        mv.position = glm::vec3(v.px, v.py, v.pz);
        mv.uv = glm::vec2(v.u, v.v);
        mv.color = glm::vec4(v.r, v.g, v.b, v.a);
        out.vertices.push_back(mv);

        const bool nonWhite =
            std::fabs(v.r - 1.0f) > 0.001f ||
            std::fabs(v.g - 1.0f) > 0.001f ||
            std::fabs(v.b - 1.0f) > 0.001f;
        anyNonWhiteColor = anyNonWhiteColor || nonWhite;
    }
    out.hasVertexColor = anyNonWhiteColor;

    struct SubmeshRange {
        std::size_t firstIndex = 0u;
        std::size_t indexCount = 0u;
        glm::vec4 baseColor{1.0f};
        backend_material::AlphaMode alphaMode = backend_material::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        DecodedTexture baseTexture;
    };
    std::vector<SubmeshRange> submeshRanges;
    submeshRanges.reserve(hdr.submeshCount);
    out.submeshBaseColors.reserve(hdr.submeshCount);
    for (std::uint32_t si = 0; si < hdr.submeshCount; ++si) {
        std::uint64_t off = 0u;
        std::uint64_t cnt = 0u;
        std::int32_t meshIdx = -1;
        float emissiveX = 0.0f;
        float emissiveY = 0.0f;
        float emissiveZ = 0.0f;
        std::uint8_t alphaModeRaw = 0u;
        float alphaCutoff = 0.0f;
        std::uint8_t doubleSided = 0u;
        if (!readPod(in, off) ||
            !readPod(in, cnt) ||
            !readPod(in, meshIdx) ||
            !readPod(in, emissiveX) ||
            !readPod(in, emissiveY) ||
            !readPod(in, emissiveZ) ||
            !readPod(in, alphaModeRaw) ||
            !readPod(in, alphaCutoff) ||
            !readPod(in, doubleSided)) {
            if (outError) *outError = "failed to read cache submesh header";
            return false;
        }

        DecodedTexture baseTexture;
        DecodedTexture emissiveTexture;
        if (!readTexture(in, baseTexture, /*keepPixels=*/true) ||
            !readTexture(in, emissiveTexture, /*keepPixels=*/false)) {
            if (outError) *outError = "failed to read cache submesh textures";
            return false;
        }

        SubmeshRange range;
        range.firstIndex = static_cast<std::size_t>(std::min<std::uint64_t>(off, out.indices.size()));
        const std::size_t maxRemaining =
            (range.firstIndex < out.indices.size()) ? (out.indices.size() - range.firstIndex) : 0u;
        range.indexCount = static_cast<std::size_t>(std::min<std::uint64_t>(cnt, maxRemaining));
        range.baseColor = baseTexture.average;
        range.alphaMode = backend_material::alphaModeFromByte(alphaModeRaw);
        range.alphaCutoff = alphaCutoff;
        range.baseTexture = std::move(baseTexture);
        submeshRanges.push_back(range);
        out.submeshBaseColors.push_back(range.baseColor);
    }

    const std::size_t triangleCount = out.indices.size() / 3u;
    out.triangleSubmesh.assign(triangleCount, 0u);
    out.triangleBaseColors.assign(triangleCount, glm::vec3(1.0f, 1.0f, 1.0f));
    out.triangleOpacity.assign(triangleCount, 1.0f);
    if (!submeshRanges.empty()) {
        for (std::size_t si = 0; si < submeshRanges.size(); ++si) {
            const SubmeshRange& range = submeshRanges[si];
            const std::size_t startTri = std::min(triangleCount, range.firstIndex / 3u);
            const std::size_t endTri = std::min(triangleCount, (range.firstIndex + range.indexCount) / 3u);
            for (std::size_t ti = startTri; ti < endTri; ++ti) {
                out.triangleSubmesh[ti] = static_cast<std::uint16_t>(si);
                glm::vec3 triColor(range.baseColor.r, range.baseColor.g, range.baseColor.b);
                float triOpacity = 1.0f;
                if (range.baseTexture.hasPixels()) {
                    const std::size_t i = ti * 3u;
                    const std::uint32_t i0 = out.indices[i + 0u];
                    const std::uint32_t i1 = out.indices[i + 1u];
                    const std::uint32_t i2 = out.indices[i + 2u];
                    if (i0 < out.vertices.size() && i1 < out.vertices.size() && i2 < out.vertices.size()) {
                        const glm::vec2 uv =
                            (out.vertices[i0].uv + out.vertices[i1].uv + out.vertices[i2].uv) * (1.0f / 3.0f);
                        const glm::vec4 texel = sampleTextureNearest(range.baseTexture, uv);
                        const glm::vec3 texelRgb(texel.r, texel.g, texel.b);
                        triColor = backend_material::blendBaseAndTexture(triColor, texelRgb, texel.a);
                        triOpacity = backend_material::opacityFromAlphaMode(
                            range.alphaMode,
                            texel.a,
                            range.alphaCutoff);
                    }
                }
                out.triangleBaseColors[ti] = glm::clamp(triColor, 0.0f, 1.0f);
                out.triangleOpacity[ti] = std::clamp(triOpacity, 0.0f, 1.0f);
            }
        }
    } else if (triangleCount > 0u) {
        out.submeshBaseColors.push_back(glm::vec4(1.0f));
        std::fill(out.triangleBaseColors.begin(), out.triangleBaseColors.end(), glm::vec3(1.0f, 1.0f, 1.0f));
        std::fill(out.triangleOpacity.begin(), out.triangleOpacity.end(), 1.0f);
    }

    if (out.vertices.empty() || out.indices.empty()) {
        if (outError) *outError = "cache geometry empty";
        return false;
    }
    return true;
}

} // namespace game::runtime::backend_model
