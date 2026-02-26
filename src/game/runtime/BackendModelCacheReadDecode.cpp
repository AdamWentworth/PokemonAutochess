#include "game/runtime/BackendModelCacheReadDecode.h"
#include "game/runtime/BackendModelCacheReadScene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/runtime/BackendMaterialShading.h"
#include "game/runtime/BackendMeshNormals.h"
#include "engine/render/ModelMeshTypes.h"

namespace {
template <typename T>
bool readPod(std::istream& in, T& out) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
}

using game::runtime::backend_model::detail::CacheHeader;
using game::runtime::backend_model::detail::CacheTextureHeader;

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
} // namespace

namespace game::runtime::backend_model::detail {

bool decodeMeshFromValidatedCacheStream(std::istream& in,
                                        const CacheHeader& hdr,
                                        MeshData& out,
                                        std::string* outError) {
    if (!readSceneFromValidatedCacheStream(in, hdr, out, outError)) return false;

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
    out.submeshIndexOffset.reserve(hdr.submeshCount);
    out.submeshIndexCount.reserve(hdr.submeshCount);
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
        out.submeshIndexOffset.push_back(static_cast<std::uint32_t>(range.firstIndex));
        out.submeshIndexCount.push_back(static_cast<std::uint32_t>(range.indexCount));
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
            const glm::vec3 baseRgb(range.baseColor.r, range.baseColor.g, range.baseColor.b);
            const bool baseTexIsConstant =
                !range.baseTexture.hasPixels() ||
                (range.baseTexture.width <= 1 && range.baseTexture.height <= 1);
            const bool emissiveTexIsConstant =
                !range.emissiveTexture.hasPixels() ||
                (range.emissiveTexture.width <= 1 && range.emissiveTexture.height <= 1);
            const bool useConstantMaterialFastPath = baseTexIsConstant && emissiveTexIsConstant;
            glm::vec3 constantTriColor =
                backend_material::composeGltfLikeColor(baseRgb, glm::vec3(0.0f), range.emissiveFactor);
            float constantTriOpacity = 1.0f;
            if (useConstantMaterialFastPath) {
                const glm::vec4 texel = range.baseTexture.average;
                const glm::vec4 emixel = range.emissiveTexture.hasPixels()
                    ? range.emissiveTexture.average
                    : glm::vec4(0.0f);
                constantTriColor = backend_material::composeGltfLikeColor(
                    backend_material::modulateBaseAndTexture(baseRgb, glm::vec3(texel.r, texel.g, texel.b)),
                    glm::vec3(emixel.r, emixel.g, emixel.b),
                    range.emissiveFactor);
                constantTriOpacity = backend_material::opacityFromAlphaMode(
                    range.alphaMode,
                    texel.a,
                    range.alphaCutoff);
            }
            for (std::size_t ti = startTri; ti < endTri; ++ti) {
                out.triangleSubmesh[ti] = static_cast<std::uint16_t>(si);
                out.triangleDoubleSided[ti] = range.doubleSided ? 1u : 0u;
                out.triangleNodeIndex[ti] = resolvedNodeIndex;
                out.triangleSkinIndex[ti] = resolvedSkinIndex;
                glm::vec3 triColor = constantTriColor;
                float triOpacity = constantTriOpacity;

                const std::size_t i = ti * 3u;
                const std::uint32_t i0 = out.indices[i + 0u];
                const std::uint32_t i1 = out.indices[i + 1u];
                const std::uint32_t i2 = out.indices[i + 2u];
                if (i0 < out.vertices.size() && i1 < out.vertices.size() && i2 < out.vertices.size()) {
                    if (useConstantMaterialFastPath) {
                        vertexColorAccum[i0] += constantTriColor;
                        vertexColorAccum[i1] += constantTriColor;
                        vertexColorAccum[i2] += constantTriColor;
                    } else {
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
                    vertexColorWeight[i0] += 1.0f;
                    vertexColorWeight[i1] += 1.0f;
                    vertexColorWeight[i2] += 1.0f;
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

} // namespace game::runtime::backend_model::detail
