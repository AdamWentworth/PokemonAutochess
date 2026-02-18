#include "game/runtime/BackendModelCache.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "engine/core/Environment.h"
#include "engine/render/ModelAnimationTypes.h"
#include "engine/render/ModelMeshTypes.h"

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
    if (hdr.vertexCount > kMaxVertices || hdr.indexCount > kMaxIndices) {
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
        mv.color = glm::vec4(v.r, v.g, v.b, v.a);
        out.vertices.push_back(mv);

        const bool nonWhite =
            std::fabs(v.r - 1.0f) > 0.001f ||
            std::fabs(v.g - 1.0f) > 0.001f ||
            std::fabs(v.b - 1.0f) > 0.001f;
        anyNonWhiteColor = anyNonWhiteColor || nonWhite;
    }
    out.hasVertexColor = anyNonWhiteColor;

    if (out.vertices.empty() || out.indices.empty()) {
        if (outError) *outError = "cache geometry empty";
        return false;
    }
    return true;
}

} // namespace game::runtime::backend_model
