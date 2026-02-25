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
#include <unordered_map>
#include <vector>

#include "engine/core/Environment.h"
#include "engine/render/Model.h"
#include "engine/render/ModelAnimationTypes.h"
#include "engine/render/ModelMeshTypes.h"
#include "engine/render/gltf/FastGLTFLoader.h"
#include "engine/render/gltf/ModelFastGltfLoaderHelpers.h"
#include "engine/render/gltf/ModelFastGltfMaterial.h"
#include "engine/render/gltf/ModelFastGltfSceneData.h"
#include "engine/render/gltf/ModelFastGltfTextures.h"
#include "game/runtime/BackendMaterialShading.h"
#include "game/runtime/BackendMeshNormals.h"
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {
namespace fs = std::filesystem;

template <typename T>
bool readPod(std::istream& in, T& out) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
}

template <typename T>
bool writePod(std::ostream& out, const T& value) {
    return static_cast<bool>(out.write(reinterpret_cast<const char*>(&value), sizeof(T)));
}

bool readString(std::istream& in, std::string& out) {
    std::uint32_t n = 0;
    if (!readPod(in, n)) return false;
    out.clear();
    if (n == 0) return true;
    out.resize(n);
    return static_cast<bool>(in.read(out.data(), n));
}

bool writeString(std::ostream& out, const std::string& s) {
    const std::uint32_t n = static_cast<std::uint32_t>(s.size());
    if (!writePod(out, n)) return false;
    if (n == 0u) return true;
    return static_cast<bool>(out.write(s.data(), n));
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

glm::mat4 trsToMat4(const pac_model_types::NodeTRS& n) {
    if (n.hasMatrix) return n.matrix;
    const glm::mat4 t = glm::translate(glm::mat4(1.0f), n.t);
    const glm::mat4 r = glm::mat4_cast(glm::normalize(n.r));
    const glm::mat4 s = glm::scale(glm::mat4(1.0f), n.s);
    return t * r * s;
}

void buildNodeParentTable(const std::vector<std::vector<int>>& nodeChildren,
                          std::vector<int>& outParent) {
    outParent.assign(nodeChildren.size(), -1);
    for (std::size_t p = 0; p < nodeChildren.size(); ++p) {
        for (int c : nodeChildren[p]) {
            if (c < 0 || static_cast<std::size_t>(c) >= outParent.size()) continue;
            if (outParent[static_cast<std::size_t>(c)] < 0) {
                outParent[static_cast<std::size_t>(c)] = static_cast<int>(p);
            }
        }
    }
}

void buildNodeGlobals(const std::vector<pac_model_types::NodeTRS>& nodesDefault,
                      const std::vector<std::vector<int>>& nodeChildren,
                      const std::vector<int>& sceneRoots,
                      std::vector<glm::mat4>& outGlobals) {
    outGlobals.assign(nodesDefault.size(), glm::mat4(1.0f));
    if (nodesDefault.empty()) return;

    std::vector<int> parent;
    buildNodeParentTable(nodeChildren, parent);

    const auto dfs = [&](const auto& self, int node, const glm::mat4& parentM) -> void {
        if (node < 0 || static_cast<std::size_t>(node) >= nodesDefault.size()) return;
        const glm::mat4 global = parentM * trsToMat4(nodesDefault[static_cast<std::size_t>(node)]);
        outGlobals[static_cast<std::size_t>(node)] = global;
        if (static_cast<std::size_t>(node) >= nodeChildren.size()) return;
        for (int child : nodeChildren[static_cast<std::size_t>(node)]) {
            self(self, child, global);
        }
    };

    if (!sceneRoots.empty()) {
        for (int root : sceneRoots) {
            dfs(dfs, root, glm::mat4(1.0f));
        }
        return;
    }

    bool drewAny = false;
    for (std::size_t i = 0; i < parent.size(); ++i) {
        if (parent[i] >= 0) continue;
        dfs(dfs, static_cast<int>(i), glm::mat4(1.0f));
        drewAny = true;
    }
    if (!drewAny) {
        dfs(dfs, 0, glm::mat4(1.0f));
    }
}

bool readSceneData(std::istream& in,
                   std::uint32_t nodeCount,
                   std::uint32_t skinCount,
                   std::uint32_t animCount,
                   std::vector<pac_model_types::NodeTRS>& outNodesDefault,
                   std::vector<std::vector<int>>& outNodeChildren,
                   std::vector<int>& outNodeParent,
                   std::vector<int>& outNodeMesh,
                   std::vector<int>& outNodeSkin,
                   std::vector<int>& outSceneRoots,
                   std::vector<pac_model_types::SkinData>& outSkins,
                   std::vector<pac_model_types::AnimationClip>& outAnimations) {
    outNodesDefault.assign(nodeCount, pac_model_types::NodeTRS{});
    outNodeChildren.assign(nodeCount, {});
    outNodeMesh.assign(nodeCount, -1);
    outNodeSkin.assign(nodeCount, -1);

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        auto& n = outNodesDefault[static_cast<std::size_t>(i)];
        std::uint8_t hasMatrix = 0u;
        if (!readPod(in, n.t) ||
            !readPod(in, n.r) ||
            !readPod(in, n.s) ||
            !readPod(in, hasMatrix) ||
            !readPod(in, n.matrix)) {
            return false;
        }
        n.r = glm::normalize(n.r);
        n.hasMatrix = (hasMatrix != 0u);
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::uint32_t childCount = 0u;
        if (!readPod(in, childCount)) return false;
        auto& children = outNodeChildren[static_cast<std::size_t>(i)];
        children.assign(childCount, -1);
        for (std::uint32_t k = 0; k < childCount; ++k) {
            std::int32_t v = -1;
            if (!readPod(in, v)) return false;
            children[static_cast<std::size_t>(k)] = static_cast<int>(v);
        }
    }

    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::int32_t v = -1;
        if (!readPod(in, v)) return false;
        outNodeMesh[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }
    for (std::uint32_t i = 0; i < nodeCount; ++i) {
        std::int32_t v = -1;
        if (!readPod(in, v)) return false;
        outNodeSkin[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }

    std::uint32_t rootCount = 0u;
    if (!readPod(in, rootCount)) return false;
    outSceneRoots.assign(rootCount, -1);
    for (std::uint32_t i = 0; i < rootCount; ++i) {
        std::int32_t v = -1;
        if (!readPod(in, v)) return false;
        outSceneRoots[static_cast<std::size_t>(i)] = static_cast<int>(v);
    }

    outSkins.assign(skinCount, pac_model_types::SkinData{});
    for (std::uint32_t si = 0; si < skinCount; ++si) {
        std::uint32_t jointCount = 0u;
        if (!readPod(in, jointCount)) return false;
        auto& skin = outSkins[static_cast<std::size_t>(si)];
        skin.joints.assign(jointCount, -1);
        skin.inverseBind.assign(jointCount, glm::mat4(1.0f));
        for (std::uint32_t j = 0; j < jointCount; ++j) {
            std::int32_t v = -1;
            if (!readPod(in, v)) return false;
            skin.joints[static_cast<std::size_t>(j)] = static_cast<int>(v);
        }
        for (std::uint32_t j = 0; j < jointCount; ++j) {
            if (!readPod(in, skin.inverseBind[static_cast<std::size_t>(j)])) return false;
        }
    }

    outAnimations.assign(animCount, pac_model_types::AnimationClip{});
    for (std::uint32_t ai = 0; ai < animCount; ++ai) {
        auto& clip = outAnimations[static_cast<std::size_t>(ai)];
        if (!readString(in, clip.name) || !readPod(in, clip.durationSec)) return false;

        std::uint32_t samplerCount = 0u;
        if (!readPod(in, samplerCount)) return false;
        clip.samplers.assign(samplerCount, pac_model_types::AnimationSampler{});
        for (std::uint32_t s = 0; s < samplerCount; ++s) {
            auto& samp = clip.samplers[static_cast<std::size_t>(s)];
            std::uint8_t isVec4 = 0u;
            if (!readString(in, samp.interpolation) || !readPod(in, isVec4)) return false;
            samp.isVec4 = (isVec4 != 0u);

            std::uint32_t inputCount = 0u;
            if (!readPod(in, inputCount)) return false;
            samp.inputs.assign(inputCount, 0.0f);
            for (std::uint32_t i = 0; i < inputCount; ++i) {
                if (!readPod(in, samp.inputs[static_cast<std::size_t>(i)])) return false;
            }

            std::uint32_t outputCount = 0u;
            if (!readPod(in, outputCount)) return false;
            samp.outputs.assign(outputCount, glm::vec4(0.0f));
            for (std::uint32_t i = 0; i < outputCount; ++i) {
                if (!readPod(in, samp.outputs[static_cast<std::size_t>(i)])) return false;
            }
        }

        std::uint32_t channelCount = 0u;
        if (!readPod(in, channelCount)) return false;
        clip.channels.assign(channelCount, pac_model_types::AnimationChannel{});
        for (std::uint32_t c = 0; c < channelCount; ++c) {
            std::int32_t samplerIndex = -1;
            std::int32_t targetNode = -1;
            std::uint8_t path = 0u;
            if (!readPod(in, samplerIndex) ||
                !readPod(in, targetNode) ||
                !readPod(in, path)) {
                return false;
            }
            auto& ch = clip.channels[static_cast<std::size_t>(c)];
            ch.samplerIndex = static_cast<int>(samplerIndex);
            ch.targetNode = static_cast<int>(targetNode);
            ch.path = (path == 1u) ? pac_model_types::ChannelPath::Rotation
                    : (path == 2u) ? pac_model_types::ChannelPath::Scale
                                   : pac_model_types::ChannelPath::Translation;
        }
    }

    buildNodeParentTable(outNodeChildren, outNodeParent);
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
    int wrapS = 10497; // GL_REPEAT
    int wrapT = 10497; // GL_REPEAT
    int minF = 9729;   // GL_LINEAR
    int magF = 9729;   // GL_LINEAR
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
    out.wrapS = h.wrapS;
    out.wrapT = h.wrapT;
    out.minF = h.minF;
    out.magF = h.magF;

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

    constexpr int kWrapRepeat = 10497;
    constexpr int kWrapClampToEdge = 33071;
    constexpr int kWrapMirroredRepeat = 33648;

    const auto wrapCoord = [](float coord, int mode) {
        switch (mode) {
            case kWrapClampToEdge:
                return std::clamp(coord, 0.0f, 1.0f);
            case kWrapMirroredRepeat: {
                const float x = std::fmod(coord, 2.0f);
                const float y = (x < 0.0f) ? (x + 2.0f) : x;
                return (y <= 1.0f) ? y : (2.0f - y);
            }
            case kWrapRepeat:
            default: {
                const float x = std::fmod(coord, 1.0f);
                return (x < 0.0f) ? (x + 1.0f) : x;
            }
        }
    };

    const float u = wrapCoord(uv.x, tex.wrapS);
    const float v = wrapCoord(uv.y, tex.wrapT);

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

glm::vec4 sampleTextureBilinear(const DecodedTexture& tex, const glm::vec2& uv) {
    if (!tex.hasPixels()) return tex.average;

    constexpr int kWrapRepeat = 10497;
    constexpr int kWrapClampToEdge = 33071;
    constexpr int kWrapMirroredRepeat = 33648;

    const auto wrapCoord = [](float coord, int mode) {
        switch (mode) {
            case kWrapClampToEdge:
                return std::clamp(coord, 0.0f, 1.0f);
            case kWrapMirroredRepeat: {
                const float x = std::fmod(coord, 2.0f);
                const float y = (x < 0.0f) ? (x + 2.0f) : x;
                return (y <= 1.0f) ? y : (2.0f - y);
            }
            case kWrapRepeat:
            default: {
                const float x = std::fmod(coord, 1.0f);
                return (x < 0.0f) ? (x + 1.0f) : x;
            }
        }
    };

    const float u = wrapCoord(uv.x, tex.wrapS);
    const float v = wrapCoord(uv.y, tex.wrapT);

    const int w = std::max(1, tex.width);
    const int h = std::max(1, tex.height);
    const float fx = u * static_cast<float>(w - 1);
    const float fy = (1.0f - v) * static_cast<float>(h - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, h - 1);
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);

    const auto sampleAt = [&](int x, int y) -> glm::vec4 {
        const std::size_t idx =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
        if (idx + 3u >= tex.rgba.size()) return tex.average;
        return glm::vec4(
            static_cast<float>(tex.rgba[idx + 0u]) / 255.0f,
            static_cast<float>(tex.rgba[idx + 1u]) / 255.0f,
            static_cast<float>(tex.rgba[idx + 2u]) / 255.0f,
            static_cast<float>(tex.rgba[idx + 3u]) / 255.0f);
    };

    const glm::vec4 c00 = sampleAt(x0, y0);
    const glm::vec4 c10 = sampleAt(x1, y0);
    const glm::vec4 c01 = sampleAt(x0, y1);
    const glm::vec4 c11 = sampleAt(x1, y1);
    const glm::vec4 cx0 = c00 * (1.0f - tx) + c10 * tx;
    const glm::vec4 cx1 = c01 * (1.0f - tx) + c11 * tx;
    return cx0 * (1.0f - ty) + cx1 * ty;
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

bool cacheHeaderMatchesSource(const std::string& modelPath, const CacheHeader& hdr) {
    std::uint64_t srcFileSize = 0u;
    std::int64_t srcWriteTime = 0;
    if (!sourceMetadataForModel(modelPath, srcFileSize, srcWriteTime)) return false;
    return hdr.srcFileSize == srcFileSize && hdr.srcWriteTime == srcWriteTime;
}

bool readCacheHeaderOnly(const std::string& cachePath, CacheHeader& outHeader) {
    outHeader = CacheHeader{};
    std::ifstream in(cachePath, std::ios::binary);
    if (!in.is_open()) return false;
    return readPod(in, outHeader);
}

struct SourceSubmeshRecord {
    std::size_t indexOffset = 0u;
    std::size_t indexCount = 0u;
    int meshIndex = -1;
    glm::vec3 emissiveFactor{0.0f};
    std::uint8_t alphaMode = 0u;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    Model::CPUTexture baseTexture;
    Model::CPUTexture emissiveTexture;
};

struct SourceCacheBuildData {
    float modelScaleFactor = 1.0f;
    std::vector<pac_model_types::NodeTRS> nodesDefault;
    std::vector<std::vector<int>> nodeChildren;
    std::vector<int> nodeMesh;
    std::vector<int> nodeSkin;
    std::vector<int> sceneRoots;
    std::vector<pac_model_types::SkinData> skins;
    std::vector<pac_model_types::AnimationClip> animations;
    std::vector<pac_model_types::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<SourceSubmeshRecord> submeshes;
};

bool buildBackendCacheSourceData(const std::string& filepath,
                                 SourceCacheBuildData& outData,
                                 std::string* outError) {
    outData = SourceCacheBuildData{};
    auto fg = pac::fastgltf_loader::tryLoad(filepath);
    if (!fg.has_value()) {
        if (outError) *outError = "fastgltf parse failed for source model";
        return false;
    }

    const fastgltf::Asset& asset = fg->asset;
    fastgltf::DefaultBufferDataAdapter adapter{};
    pac::model_fastgltf::buildSceneData(asset,
                                        adapter,
                                        outData.nodesDefault,
                                        outData.nodeChildren,
                                        outData.nodeMesh,
                                        outData.nodeSkin,
                                        outData.sceneRoots,
                                        outData.skins,
                                        outData.animations);

    outData.vertices.reserve(20000);
    outData.indices.reserve(60000);
    outData.submeshes.reserve(64);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float minZ = std::numeric_limits<float>::max();
    float maxX = -minX;
    float maxY = -minY;
    float maxZ = -minZ;

    const bool dbgThisModel = pac::model_fastgltf::envTruthy("PAC_GLTF_DEBUG");

    for (std::size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
        const auto& mesh = asset.meshes[meshIdx];
        for (const auto& p : mesh.primitives) {
            if (p.type != fastgltf::PrimitiveType::Triangles) continue;

            int materialIndex = -1;
            if (p.materialIndex.has_value()) {
                materialIndex = static_cast<int>(p.materialIndex.value());
            }

            auto itPos = p.findAttribute("POSITION");
            if (itPos == p.attributes.end()) continue;

            int requiredTexCoord = pac::model_fastgltf::requiredTexCoordForMaterial(asset, materialIndex);
            std::string uvAttr = "TEXCOORD_" + std::to_string(requiredTexCoord);
            auto itUv = p.findAttribute(uvAttr);
            if (itUv == p.attributes.end()) {
                requiredTexCoord = 0;
                itUv = p.findAttribute("TEXCOORD_0");
            }
            const bool hasUv = (itUv != p.attributes.end());

            const std::size_t posAcc = itPos->accessorIndex;
            const std::size_t uvAcc = hasUv ? itUv->accessorIndex : 0u;

            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> uv;
            pos.reserve(asset.accessors[posAcc].count);
            uv.reserve(hasUv ? asset.accessors[uvAcc].count : asset.accessors[posAcc].count);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, asset.accessors[posAcc],
                [&](glm::vec3 v, std::size_t) { pos.push_back(v); }, adapter);

            if (hasUv) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset, asset.accessors[uvAcc],
                    [&](glm::vec2 v, std::size_t) { uv.push_back(v); }, adapter);
            } else {
                glm::vec3 pMin(std::numeric_limits<float>::max());
                glm::vec3 pMax(-std::numeric_limits<float>::max());
                for (const auto& p3 : pos) {
                    pMin = glm::min(pMin, p3);
                    pMax = glm::max(pMax, p3);
                }
                const float r[3] = {pMax.x - pMin.x, pMax.y - pMin.y, pMax.z - pMin.z};
                int a = 0;
                if (r[1] > r[a]) a = 1;
                if (r[2] > r[a]) a = 2;
                int b = (a == 0) ? 1 : 0;
                for (int i = 0; i < 3; ++i) {
                    if (i == a) continue;
                    if (r[i] > r[b]) b = i;
                }
                const float minA = (a == 0) ? pMin.x : ((a == 1) ? pMin.y : pMin.z);
                const float minB = (b == 0) ? pMin.x : ((b == 1) ? pMin.y : pMin.z);
                const float denA = std::max(1e-6f, (a == 0) ? r[0] : ((a == 1) ? r[1] : r[2]));
                const float denB = std::max(1e-6f, (b == 0) ? r[0] : ((b == 1) ? r[1] : r[2]));
                uv.reserve(pos.size());
                for (const auto& p3 : pos) {
                    const float ca = (a == 0) ? p3.x : ((a == 1) ? p3.y : p3.z);
                    const float cb = (b == 0) ? p3.x : ((b == 1) ? p3.y : p3.z);
                    uv.emplace_back((ca - minA) / denA, (cb - minB) / denB);
                }
            }

            std::vector<glm::vec4> color(pos.size(), glm::vec4(1.0f));
            auto itC = p.findAttribute("COLOR_0");
            if (itC != p.attributes.end()) {
                const std::size_t colAcc = itC->accessorIndex;
                const auto& acc = asset.accessors[colAcc];
                color.clear();
                color.reserve(acc.count);
                if (acc.type == fastgltf::AccessorType::Vec3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset, acc,
                        [&](glm::vec3 v, std::size_t) { color.emplace_back(v.x, v.y, v.z, 1.0f); },
                        adapter);
                } else {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset, acc,
                        [&](glm::vec4 v, std::size_t) { color.push_back(v); },
                        adapter);
                }
                if (color.size() != pos.size()) {
                    color.assign(pos.size(), glm::vec4(1.0f));
                }
            }

            auto applyKHRTextureTransform = [&](const fastgltf::TextureInfo* ti) {
                if (!ti || !ti->transform) return;
                const auto& tr = *ti->transform;
                glm::vec2 offset(tr.uvOffset[0], tr.uvOffset[1]);
                glm::vec2 scale(tr.uvScale[0], tr.uvScale[1]);
                const float rot = static_cast<float>(tr.rotation);
                const float c = std::cos(rot);
                const float s = std::sin(rot);
                for (auto& t : uv) {
                    glm::vec2 p2 = t;
                    p2 *= scale;
                    p2 = glm::vec2(c * p2.x - s * p2.y, s * p2.x + c * p2.y);
                    p2 += offset;
                    t = p2;
                }
            };

            const fastgltf::TextureInfo* baseTI = nullptr;
            if (materialIndex >= 0 && materialIndex < static_cast<int>(asset.materials.size())) {
                const auto& mat = asset.materials[static_cast<std::size_t>(materialIndex)];
                if (mat.pbrData.baseColorTexture.has_value()) {
                    const auto& ti = mat.pbrData.baseColorTexture.value();
                    if (static_cast<int>(ti.texCoordIndex) == requiredTexCoord) {
                        baseTI = &ti;
                    }
                }
            }
            applyKHRTextureTransform(baseTI);

            if (pos.empty() || uv.empty() || pos.size() != uv.size()) {
                continue;
            }

            std::vector<glm::u16vec4> joints;
            std::vector<glm::vec4> weights;
            auto itJ = p.findAttribute("JOINTS_0");
            auto itW = p.findAttribute("WEIGHTS_0");
            if (itJ != p.attributes.end() && itW != p.attributes.end()) {
                joints.reserve(asset.accessors[itJ->accessorIndex].count);
                weights.reserve(asset.accessors[itW->accessorIndex].count);
                fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                    asset, asset.accessors[itJ->accessorIndex],
                    [&](glm::u16vec4 v, std::size_t) { joints.push_back(v); }, adapter);
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset, asset.accessors[itW->accessorIndex],
                    [&](glm::vec4 v, std::size_t) { weights.push_back(v); }, adapter);
                if (joints.size() != pos.size() || weights.size() != pos.size()) {
                    joints.clear();
                    weights.clear();
                }
            }

            std::vector<std::uint32_t> primIdxU32;
            if (p.indicesAccessor.has_value()) {
                const auto& idxAcc = asset.accessors[p.indicesAccessor.value()];
                primIdxU32.reserve(idxAcc.count);
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset, idxAcc,
                    [&](std::uint32_t v, std::size_t) { primIdxU32.push_back(v); }, adapter);
            }
            if (primIdxU32.empty()) {
                primIdxU32.resize(pos.size());
                for (std::size_t i = 0; i < pos.size(); ++i) primIdxU32[i] = static_cast<std::uint32_t>(i);
            }

            const std::size_t baseVertex = outData.vertices.size();
            const std::size_t subIndexOffset = outData.indices.size();

            for (std::size_t i = 0; i < pos.size(); ++i) {
                pac_model_types::Vertex v{};
                v.px = pos[i].x; v.py = pos[i].y; v.pz = pos[i].z;
                v.u = uv[i].x; v.v = uv[i].y;
                v.j0 = v.j1 = v.j2 = v.j3 = 0u;
                v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;
                v.r = color[i].r; v.g = color[i].g; v.b = color[i].b; v.a = color[i].a;
                if (!joints.empty() && !weights.empty()) {
                    auto j = joints[i];
                    auto w = weights[i];
                    float sum = w.x + w.y + w.z + w.w;
                    if (sum <= 0.0001f) {
                        w = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                    } else {
                        w /= sum;
                    }
                    v.j0 = j.x; v.j1 = j.y; v.j2 = j.z; v.j3 = j.w;
                    v.w0 = w.x; v.w1 = w.y; v.w2 = w.z; v.w3 = w.w;
                }
                outData.vertices.push_back(v);
                minX = std::min(minX, v.px); minY = std::min(minY, v.py); minZ = std::min(minZ, v.pz);
                maxX = std::max(maxX, v.px); maxY = std::max(maxY, v.py); maxZ = std::max(maxZ, v.pz);
            }
            for (const std::uint32_t idx : primIdxU32) {
                outData.indices.push_back(static_cast<std::uint32_t>(baseVertex + idx));
            }

            int baseTexCoordUsed = 0;
            int emissiveTexCoordUsed = 0;
            auto baseCPU = pac::model_fastgltf::decodeBaseColorTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &baseTexCoordUsed);
            auto emissiveCPU = pac::model_fastgltf::decodeEmissiveTextureFast(
                asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &emissiveTexCoordUsed);
            const pac::model_fastgltf::MaterialRenderInfo materialInfo =
                pac::model_fastgltf::resolveMaterialRenderInfo(asset, materialIndex, baseCPU, dbgThisModel);

            SourceSubmeshRecord sm{};
            sm.indexOffset = subIndexOffset;
            sm.indexCount = primIdxU32.size();
            sm.meshIndex = static_cast<int>(meshIdx);
            sm.emissiveFactor = materialInfo.emissiveFactor;
            sm.alphaMode = static_cast<std::uint8_t>(materialInfo.alphaMode);
            sm.alphaCutoff = materialInfo.alphaCutoff;
            sm.doubleSided = materialInfo.doubleSided;
            sm.baseTexture = std::move(baseCPU);
            sm.emissiveTexture = std::move(emissiveCPU);
            outData.submeshes.push_back(std::move(sm));
        }
    }

    if (outData.vertices.empty() || outData.indices.empty()) {
        if (outError) *outError = "source model produced empty geometry";
        return false;
    }

    const float desiredHeight = 0.8f;
    const float ex = std::max(0.0f, maxX - minX);
    const float ey = std::max(0.0f, maxY - minY);
    const float ez = std::max(0.0f, maxZ - minZ);
    float denom = std::max(ex, std::max(ey, ez));
    if (std::abs(denom) < 1e-6f) denom = 1.0f;
    outData.modelScaleFactor = desiredHeight / denom;
    return true;
}

bool writeBackendCacheFromSourceData(const std::string& filepath,
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

    const fs::path cpath = fs::path("cache") / "models" / (hexHash64(fnv1a64(filepath)) + ".pacmdl");
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
            !writePod(out, sm.alphaMode) ||
            !writePod(out, sm.alphaCutoff) ||
            !writePod(out, doubleSided) ||
            !writeTexture(out, sm.baseTexture) ||
            !writeTexture(out, sm.emissiveTexture)) {
            if (outError) *outError = "failed to write cache submesh/texture";
            return false;
        }
    }

    return true;
}

bool rebuildCacheFromSource(const std::string& modelPath, std::string* outError) {
    SourceCacheBuildData data;
    std::string err;
    if (!buildBackendCacheSourceData(modelPath, data, &err)) {
        if (outError) *outError = err;
        return false;
    }
    if (!writeBackendCacheFromSourceData(modelPath, data, &err)) {
        if (outError) *outError = err;
        return false;
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

    bool attemptedRebuild = false;
retry_read:
    const std::string cachePath = cachePathForModel(modelPath);
    std::ifstream in(cachePath, std::ios::binary);
    if (!in.is_open()) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSource(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "cache file not found: " + cachePath + " (rebuild failed: " + rebuildErr + ")";
            return false;
        }
        if (outError) *outError = "cache file not found: " + cachePath;
        return false;
    }

    CacheHeader hdr{};
    if (!readPod(in, hdr)) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSource(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "failed to read cache header (rebuild failed: " + rebuildErr + ")";
            return false;
        }
        if (outError) *outError = "failed to read cache header";
        return false;
    }
    if (hdr.magic != kModelCacheMagic || hdr.version != kModelCacheVersion) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSource(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "cache format mismatch (rebuild failed: " + rebuildErr + ")";
            return false;
        }
        if (outError) *outError = "cache format mismatch";
        return false;
    }
    if (!cacheHeaderMatchesSource(modelPath, hdr)) {
        if (!attemptedRebuild) {
            std::string rebuildErr;
            if (rebuildCacheFromSource(modelPath, &rebuildErr)) {
                attemptedRebuild = true;
                goto retry_read;
            }
            if (outError) *outError = "cache stale and rebuild failed: " + rebuildErr;
            return false;
        }
        if (outError) *outError = "cache stale after rebuild";
        return false;
    }

    constexpr std::uint32_t kMaxNodes = 4096u;
    constexpr std::uint32_t kMaxSkins = 512u;
    constexpr std::uint32_t kMaxAnimations = 512u;
    if (hdr.nodeCount > kMaxNodes || hdr.skinCount > kMaxSkins || hdr.animCount > kMaxAnimations) {
        if (outError) *outError = "cache scene metadata exceeds safety limits";
        return false;
    }

    if (!readSceneData(in,
                       hdr.nodeCount,
                       hdr.skinCount,
                       hdr.animCount,
                       out.nodesDefault,
                       out.nodeChildren,
                       out.nodeParent,
                       out.nodeMesh,
                       out.nodeSkin,
                       out.sceneRoots,
                       out.skins,
                       out.animations)) {
        if (outError) *outError = "failed to read cache scene/animation sections";
        return false;
    }
    buildNodeGlobals(out.nodesDefault, out.nodeChildren, out.sceneRoots, out.bindNodeGlobals);

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
    out.boundsMin = glm::vec3(0.0f);
    out.boundsMax = glm::vec3(0.0f);

    bool anyNonWhiteColor = false;
    bool initializedBounds = false;
    for (const auto& v : cpuVertices) {
        MeshVertex mv;
        mv.position = glm::vec3(v.px, v.py, v.pz);
        mv.uv = glm::vec2(v.u, v.v);
        mv.color = glm::vec4(v.r, v.g, v.b, v.a);
        mv.j0 = v.j0;
        mv.j1 = v.j1;
        mv.j2 = v.j2;
        mv.j3 = v.j3;
        mv.w0 = v.w0;
        mv.w1 = v.w1;
        mv.w2 = v.w2;
        mv.w3 = v.w3;
        out.vertices.push_back(mv);

        if (!initializedBounds) {
            out.boundsMin = mv.position;
            out.boundsMax = mv.position;
            initializedBounds = true;
        } else {
            out.boundsMin = glm::min(out.boundsMin, mv.position);
            out.boundsMax = glm::max(out.boundsMax, mv.position);
        }

        const bool nonWhite =
            std::fabs(v.r - 1.0f) > 0.001f ||
            std::fabs(v.g - 1.0f) > 0.001f ||
            std::fabs(v.b - 1.0f) > 0.001f;
        anyNonWhiteColor = anyNonWhiteColor || nonWhite;
    }
    out.hasVertexColor = anyNonWhiteColor;
    computeVertexNormals(out.vertices, out.indices);

    struct SubmeshRange {
        std::size_t firstIndex = 0u;
        std::size_t indexCount = 0u;
        int meshIndex = -1;
        glm::vec4 baseColor{1.0f};
        glm::vec3 emissiveFactor{0.0f};
        backend_material::AlphaMode alphaMode = backend_material::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        DecodedTexture baseTexture;
        DecodedTexture emissiveTexture;
    };
    std::vector<SubmeshRange> submeshRanges;
    submeshRanges.reserve(hdr.submeshCount);
    out.submeshBaseColors.reserve(hdr.submeshCount);
    out.submeshMeshIndex.reserve(hdr.submeshCount);
    out.submeshBaseTextures.reserve(hdr.submeshCount);
    out.submeshAlphaMode.reserve(hdr.submeshCount);
    out.submeshAlphaCutoff.reserve(hdr.submeshCount);
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
            !readTexture(in, emissiveTexture, /*keepPixels=*/true)) {
            if (outError) *outError = "failed to read cache submesh textures";
            return false;
        }

        SubmeshRange range;
        range.firstIndex = static_cast<std::size_t>(std::min<std::uint64_t>(off, out.indices.size()));
        const std::size_t maxRemaining =
            (range.firstIndex < out.indices.size()) ? (out.indices.size() - range.firstIndex) : 0u;
        range.indexCount = static_cast<std::size_t>(std::min<std::uint64_t>(cnt, maxRemaining));
        range.meshIndex = static_cast<int>(meshIdx);
        // Base-color texture data is already multiplied by glTF baseColorFactor during decode.
        // Keep a neutral factor when pixels are present to preserve per-texel detail.
        range.baseColor = baseTexture.hasPixels() ? glm::vec4(1.0f) : baseTexture.average;
        range.emissiveFactor = glm::vec3(emissiveX, emissiveY, emissiveZ);
        range.alphaMode = backend_material::alphaModeFromByte(alphaModeRaw);
        range.alphaCutoff = alphaCutoff;
        range.doubleSided = (doubleSided != 0u);
        range.baseTexture = std::move(baseTexture);
        range.emissiveTexture = std::move(emissiveTexture);
        submeshRanges.push_back(range);
        out.submeshBaseColors.push_back(range.baseColor);
        out.submeshMeshIndex.push_back(range.meshIndex);
        out.submeshAlphaMode.push_back(static_cast<std::uint8_t>(range.alphaMode));
        out.submeshAlphaCutoff.push_back(std::clamp(range.alphaCutoff, 0.0f, 1.0f));
        CachedTextureRgba cachedTex;
        cachedTex.width = range.baseTexture.width;
        cachedTex.height = range.baseTexture.height;
        cachedTex.wrapS = range.baseTexture.wrapS;
        cachedTex.wrapT = range.baseTexture.wrapT;
        cachedTex.minF = range.baseTexture.minF;
        cachedTex.magF = range.baseTexture.magF;
        cachedTex.rgba = range.baseTexture.rgba;
        out.submeshBaseTextures.push_back(std::move(cachedTex));
    }

    const std::size_t triangleCount = out.indices.size() / 3u;
    out.triangleSubmesh.assign(triangleCount, 0u);
    out.triangleBaseColors.assign(triangleCount, glm::vec3(1.0f, 1.0f, 1.0f));
    out.triangleOpacity.assign(triangleCount, 1.0f);
    out.triangleDoubleSided.assign(triangleCount, 0u);
    out.triangleNodeIndex.assign(triangleCount, -1);
    out.triangleSkinIndex.assign(triangleCount, -1);
    out.vertexBaseColors.assign(out.vertices.size(), glm::vec3(1.0f, 1.0f, 1.0f));
    out.hasVertexBaseColor = false;
    std::vector<glm::vec3> vertexColorAccum(out.vertices.size(), glm::vec3(0.0f));
    std::vector<float> vertexColorWeight(out.vertices.size(), 0.0f);
    std::unordered_map<int, int> meshToNode;
    meshToNode.reserve(out.nodeMesh.size());
    int maxMeshIndex = -1;
    for (std::size_t ni = 0; ni < out.nodeMesh.size(); ++ni) {
        const int meshIndex = out.nodeMesh[ni];
        if (meshIndex < 0) continue;
        maxMeshIndex = std::max(maxMeshIndex, meshIndex);
        if (meshToNode.find(meshIndex) == meshToNode.end()) {
            meshToNode.emplace(meshIndex, static_cast<int>(ni));
        }
    }
    out.meshIndexToNode.clear();
    if (maxMeshIndex >= 0) {
        out.meshIndexToNode.assign(static_cast<std::size_t>(maxMeshIndex) + 1u, -1);
        for (const auto& kv : meshToNode) {
            if (kv.first < 0) continue;
            const std::size_t idx = static_cast<std::size_t>(kv.first);
            if (idx < out.meshIndexToNode.size()) out.meshIndexToNode[idx] = kv.second;
        }
    }
    if (!submeshRanges.empty()) {
        for (std::size_t si = 0; si < submeshRanges.size(); ++si) {
            const SubmeshRange& range = submeshRanges[si];
            const std::size_t startTri = std::min(triangleCount, range.firstIndex / 3u);
            const std::size_t endTri = std::min(triangleCount, (range.firstIndex + range.indexCount) / 3u);
            int resolvedNodeIndex = -1;
            if (range.meshIndex >= 0) {
                const auto it = meshToNode.find(range.meshIndex);
                if (it != meshToNode.end()) resolvedNodeIndex = it->second;
            }
            if (resolvedNodeIndex < 0 && !out.sceneRoots.empty()) {
                resolvedNodeIndex = out.sceneRoots.front();
            }
            int resolvedSkinIndex = -1;
            if (resolvedNodeIndex >= 0 &&
                static_cast<std::size_t>(resolvedNodeIndex) < out.nodeSkin.size()) {
                resolvedSkinIndex = out.nodeSkin[static_cast<std::size_t>(resolvedNodeIndex)];
            }
            for (std::size_t ti = startTri; ti < endTri; ++ti) {
                out.triangleSubmesh[ti] = static_cast<std::uint16_t>(si);
                out.triangleDoubleSided[ti] = range.doubleSided ? 1u : 0u;
                out.triangleNodeIndex[ti] = resolvedNodeIndex;
                out.triangleSkinIndex[ti] = resolvedSkinIndex;
                const glm::vec3 baseRgb(range.baseColor.r, range.baseColor.g, range.baseColor.b);
                glm::vec3 triColor =
                    backend_material::composeGltfLikeColor(baseRgb, glm::vec3(0.0f), range.emissiveFactor);
                float triOpacity = 1.0f;

                const std::size_t i = ti * 3u;
                const std::uint32_t i0 = out.indices[i + 0u];
                const std::uint32_t i1 = out.indices[i + 1u];
                const std::uint32_t i2 = out.indices[i + 2u];
                if (i0 < out.vertices.size() && i1 < out.vertices.size() && i2 < out.vertices.size()) {
                    const glm::vec2 uv0 = out.vertices[i0].uv;
                    const glm::vec2 uv1 = out.vertices[i1].uv;
                    const glm::vec2 uv2 = out.vertices[i2].uv;
                    const glm::vec2 uvc = (uv0 + uv1 + uv2) * (1.0f / 3.0f);

                    const glm::vec4 tex0 = range.baseTexture.hasPixels()
                        ? sampleTextureBilinear(range.baseTexture, uv0)
                        : range.baseTexture.average;
                    const glm::vec4 tex1 = range.baseTexture.hasPixels()
                        ? sampleTextureBilinear(range.baseTexture, uv1)
                        : range.baseTexture.average;
                    const glm::vec4 tex2 = range.baseTexture.hasPixels()
                        ? sampleTextureBilinear(range.baseTexture, uv2)
                        : range.baseTexture.average;
                    const glm::vec4 texc = range.baseTexture.hasPixels()
                        ? sampleTextureBilinear(range.baseTexture, uvc)
                        : range.baseTexture.average;

                    const glm::vec4 emi0 = range.emissiveTexture.hasPixels()
                        ? sampleTextureBilinear(range.emissiveTexture, uv0)
                        : glm::vec4(0.0f);
                    const glm::vec4 emi1 = range.emissiveTexture.hasPixels()
                        ? sampleTextureBilinear(range.emissiveTexture, uv1)
                        : glm::vec4(0.0f);
                    const glm::vec4 emi2 = range.emissiveTexture.hasPixels()
                        ? sampleTextureBilinear(range.emissiveTexture, uv2)
                        : glm::vec4(0.0f);
                    const glm::vec4 emic = range.emissiveTexture.hasPixels()
                        ? sampleTextureBilinear(range.emissiveTexture, uvc)
                        : glm::vec4(0.0f);

                    const glm::vec3 base0 = backend_material::modulateBaseAndTexture(
                        baseRgb, glm::vec3(tex0.r, tex0.g, tex0.b));
                    const glm::vec3 base1 = backend_material::modulateBaseAndTexture(
                        baseRgb, glm::vec3(tex1.r, tex1.g, tex1.b));
                    const glm::vec3 base2 = backend_material::modulateBaseAndTexture(
                        baseRgb, glm::vec3(tex2.r, tex2.g, tex2.b));

                    const glm::vec3 c0 = backend_material::composeGltfLikeColor(
                        base0, glm::vec3(emi0.r, emi0.g, emi0.b), range.emissiveFactor);
                    const glm::vec3 c1 = backend_material::composeGltfLikeColor(
                        base1, glm::vec3(emi1.r, emi1.g, emi1.b), range.emissiveFactor);
                    const glm::vec3 c2 = backend_material::composeGltfLikeColor(
                        base2, glm::vec3(emi2.r, emi2.g, emi2.b), range.emissiveFactor);
                    vertexColorAccum[i0] += c0;
                    vertexColorAccum[i1] += c1;
                    vertexColorAccum[i2] += c2;
                    vertexColorWeight[i0] += 1.0f;
                    vertexColorWeight[i1] += 1.0f;
                    vertexColorWeight[i2] += 1.0f;

                    const glm::vec4 texel = (tex0 + tex1 + tex2 + texc) * 0.25f;
                    const glm::vec4 emixel = (emi0 + emi1 + emi2 + emic) * 0.25f;
                    const glm::vec3 texelRgb(texel.r, texel.g, texel.b);
                    triColor = backend_material::composeGltfLikeColor(
                        backend_material::modulateBaseAndTexture(baseRgb, texelRgb),
                        glm::vec3(emixel.r, emixel.g, emixel.b),
                        range.emissiveFactor);
                    triOpacity = backend_material::opacityFromAlphaMode(
                        range.alphaMode,
                        texel.a,
                        range.alphaCutoff);
                }
                out.triangleBaseColors[ti] = glm::clamp(triColor, 0.0f, 1.0f);
                out.triangleOpacity[ti] = std::clamp(triOpacity, 0.0f, 1.0f);
            }
        }
    } else if (triangleCount > 0u) {
        out.submeshBaseColors.push_back(glm::vec4(1.0f));
        out.submeshBaseTextures.push_back(CachedTextureRgba{});
        out.submeshAlphaMode.push_back(static_cast<std::uint8_t>(backend_material::AlphaMode::Opaque));
        out.submeshAlphaCutoff.push_back(0.5f);
        std::fill(out.triangleBaseColors.begin(), out.triangleBaseColors.end(), glm::vec3(1.0f, 1.0f, 1.0f));
        std::fill(out.triangleOpacity.begin(), out.triangleOpacity.end(), 1.0f);
        std::fill(out.triangleDoubleSided.begin(), out.triangleDoubleSided.end(), 1u);
        const int fallbackNode = !out.sceneRoots.empty() ? out.sceneRoots.front() : -1;
        std::fill(out.triangleNodeIndex.begin(), out.triangleNodeIndex.end(), fallbackNode);
        if (fallbackNode >= 0 && static_cast<std::size_t>(fallbackNode) < out.nodeSkin.size()) {
            const int fallbackSkin = out.nodeSkin[static_cast<std::size_t>(fallbackNode)];
            std::fill(out.triangleSkinIndex.begin(), out.triangleSkinIndex.end(), fallbackSkin);
        }
    }
    for (std::size_t vi = 0; vi < out.vertices.size(); ++vi) {
        if (vertexColorWeight[vi] > 0.0f) {
            out.vertexBaseColors[vi] =
                glm::clamp(vertexColorAccum[vi] / vertexColorWeight[vi], 0.0f, 1.0f);
            out.hasVertexBaseColor = true;
        } else if (out.hasVertexColor) {
            const glm::vec4 c = out.vertices[vi].color;
            out.vertexBaseColors[vi] = glm::clamp(glm::vec3(c.r, c.g, c.b), 0.0f, 1.0f);
        }
    }

    if (out.vertices.empty() || out.indices.empty()) {
        if (outError) *outError = "cache geometry empty";
        return false;
    }
    return true;
}

} // namespace game::runtime::backend_model
