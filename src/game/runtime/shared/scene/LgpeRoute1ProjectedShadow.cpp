#include "game/runtime/shared/scene/LgpeRoute1ProjectedShadow.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>

namespace game::runtime::lgpe_route1_projected_shadow {
namespace {

using I = IRenderBackend;

// frame1453, resource 190, c3.data[80..95]
// SHA-256:
// 8565bad1779c0946794d1abccaa4fb15df6be422dbb45ad648fd530344cb4fd8
constexpr std::array<float, 16> kCapturedProjection{
    0.00039853889029473066f,
    0.0002737410832196474f,
    0.00021101989841554314f,
    0.0f,
    2.5980146853288311e-12f,
    0.00027791073080152273f,
    -0.0004379898018669337f,
    0.0f,
    0.0003787542227655649f,
    -0.000288040260784328f,
    -0.00022204277047421783f,
    0.0f,
    -0.35835444927215576f,
    -0.851733922958374f,
    -0.6566449403762817f,
    1.0f};

// The world-space point mapped to the captured orthographic center. Solving
// the three captured projection rows yields the in-game player vicinity.
constexpr std::array<double, 3> kCapturedCenterCm{
    1949.0891138264114,
    -0.10019702066552061,
    -1104.7622391697978};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct ProjectedVertex {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 1.0f;
    float reciprocalW = 1.0f;
    float uOverW = 0.0f;
    float vOverW = 0.0f;
    bool valid = false;
};

Vec4 multiply(
    const std::array<float, 16>& matrix,
    const Vec4& value) {
    return {
        matrix[0] * value.x + matrix[4] * value.y +
            matrix[8] * value.z + matrix[12] * value.w,
        matrix[1] * value.x + matrix[5] * value.y +
            matrix[9] * value.z + matrix[13] * value.w,
        matrix[2] * value.x + matrix[6] * value.y +
            matrix[10] * value.z + matrix[14] * value.w,
        matrix[3] * value.x + matrix[7] * value.y +
            matrix[11] * value.z + matrix[15] * value.w};
}

Vec4 multiplyRaw(const float* matrix, const Vec4& value) {
    return {
        matrix[0] * value.x + matrix[4] * value.y +
            matrix[8] * value.z + matrix[12] * value.w,
        matrix[1] * value.x + matrix[5] * value.y +
            matrix[9] * value.z + matrix[13] * value.w,
        matrix[2] * value.x + matrix[6] * value.y +
            matrix[10] * value.z + matrix[14] * value.w,
        matrix[3] * value.x + matrix[7] * value.y +
            matrix[11] * value.z + matrix[15] * value.w};
}

Vec4 skinVertex(
    const I::WorldMeshVertex& vertex,
    const I::WorldSceneInstance& instance) {
    const Vec4 local{vertex.x, vertex.y, vertex.z, 1.0f};
    if (instance.gpuSkinning == 0u || !instance.skinMatrices ||
        instance.skinMatrixCount == 0u) {
        return local;
    }

    const std::array<float, 4> joints{
        vertex.joint0, vertex.joint1, vertex.joint2, vertex.joint3};
    const std::array<float, 4> weights{
        vertex.weight0, vertex.weight1, vertex.weight2, vertex.weight3};
    Vec4 blended{0.0f, 0.0f, 0.0f, 0.0f};
    float weightSum = 0.0f;
    for (std::size_t component = 0u; component < weights.size(); ++component) {
        const float weight = weights[component];
        const int joint = static_cast<int>(std::lround(joints[component]));
        if (weight <= 0.00001f || joint < 0 ||
            joint >= static_cast<int>(instance.skinMatrixCount)) {
            continue;
        }
        Vec4 transformed = local;
        if (instance.gpuSkinningMode != 0u) {
            transformed = multiplyRaw(
                instance.skinMatrices +
                    (static_cast<std::size_t>(joint) +
                     instance.skinMatrixCount) *
                        16u,
                transformed);
        }
        transformed = multiplyRaw(
            instance.skinMatrices + static_cast<std::size_t>(joint) * 16u,
            transformed);
        blended.x += transformed.x * weight;
        blended.y += transformed.y * weight;
        blended.z += transformed.z * weight;
        blended.w += transformed.w * weight;
        weightSum += weight;
    }
    return weightSum > 0.00001f ? blended : local;
}

float wrapCoordinate(float value, int mode) {
    if (mode == 33071) {
        return std::clamp(value, 0.0f, 1.0f);
    }
    if (mode == 33648) {
        const float integral = std::floor(value);
        const float fraction = value - integral;
        const int parity =
            static_cast<int>(std::abs(integral)) & 1;
        return parity != 0 ? 1.0f - fraction : fraction;
    }
    return value - std::floor(value);
}

float sampleAlpha(
    const I::WorldSceneMaterial& material,
    float sourceU,
    float sourceV) {
    if (!material.textureRgba || material.textureWidth <= 0 ||
        material.textureHeight <= 0) {
        return 1.0f;
    }
    const float u = wrapCoordinate(sourceU, material.textureWrapS);
    const float v = wrapCoordinate(1.0f - sourceV, material.textureWrapT);
    const float pixelX =
        u * static_cast<float>(std::max(material.textureWidth - 1, 0));
    const float pixelY =
        v * static_cast<float>(std::max(material.textureHeight - 1, 0));
    const int x0 = static_cast<int>(std::floor(pixelX));
    const int y0 = static_cast<int>(std::floor(pixelY));
    const int x1 = std::min(x0 + 1, material.textureWidth - 1);
    const int y1 = std::min(y0 + 1, material.textureHeight - 1);
    const float fx = pixelX - static_cast<float>(x0);
    const float fy = pixelY - static_cast<float>(y0);
    const auto alphaAt = [&](int x, int y) {
        const std::size_t pixel =
            static_cast<std::size_t>(y) *
                static_cast<std::size_t>(material.textureWidth) +
            static_cast<std::size_t>(x);
        return static_cast<float>(material.textureRgba[pixel * 4u + 3u]) /
               255.0f;
    };
    const float top =
        alphaAt(x0, y0) * (1.0f - fx) + alphaAt(x1, y0) * fx;
    const float bottom =
        alphaAt(x0, y1) * (1.0f - fx) + alphaAt(x1, y1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

float edge(
    float ax,
    float ay,
    float bx,
    float by,
    float px,
    float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

const I::WorldSceneRenderObject* renderObject(
    const shared_world_scene::WorldSceneRegistry& registry,
    I::WorldSceneRenderObjectHandle handle) {
    if (!handle || handle.id > registry.renderObjects.size()) return nullptr;
    return &registry.renderObjects[handle.id - 1u];
}

const I::WorldSceneGeometry* geometry(
    const shared_world_scene::WorldSceneRegistry& registry,
    I::WorldSceneGeometryHandle handle) {
    if (!handle || handle.id > registry.geometries.size()) return nullptr;
    return &registry.geometries[handle.id - 1u];
}

const I::WorldSceneMaterial* material(
    const shared_world_scene::WorldSceneRegistry& registry,
    I::WorldSceneMaterialHandle handle) {
    if (!handle || handle.id > registry.materials.size()) return nullptr;
    return &registry.materials[handle.id - 1u];
}

bool fail(std::string* outError, const std::string& message) {
    if (outError) *outError = message;
    return false;
}

} // namespace

std::array<float, 16> projectionForCenter(
    const std::array<float, 3>& sourceCenterCm) {
    std::array<float, 16> result = kCapturedProjection;
    for (std::size_t row = 0u; row < 3u; ++row) {
        double translation =
            static_cast<double>(kCapturedProjection[12u + row]);
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            translation +=
                static_cast<double>(
                    kCapturedProjection[axis * 4u + row]) *
                (kCapturedCenterCm[axis] -
                 static_cast<double>(sourceCenterCm[axis]));
        }
        result[12u + row] = static_cast<float>(translation);
    }
    return result;
}

bool Atlas::build(
    const std::vector<lgpe_world_scene::PreparedScene*>& scenes,
    const std::array<float, 3>& sourceCenterCm,
    std::string* outError) {
    if (scenes.empty()) {
        return fail(outError, "Route 1 projected-shadow build has no scenes");
    }

    projection_ = projectionForCenter(sourceCenterCm);
    const std::size_t pixelCount =
        static_cast<std::size_t>(kNativeAtlasWidth) *
        static_cast<std::size_t>(kNativeAtlasHeight);
    depth_.resize(pixelCount);
    std::fill(depth_.begin(), depth_.end(), 1.0f);
    stats_ = BuildStats{};

    std::vector<ProjectedVertex> projected;
    for (const auto* scene : scenes) {
        if (!scene) continue;
        const auto& registry = scene->registry;
        for (const auto& drawClass : scene->shadowFrame.drawClasses) {
            const auto* object =
                renderObject(registry, drawClass.objectHandle);
            if (!object) continue;
            const auto* mesh = geometry(registry, object->geometryHandle);
            const auto* surface = material(registry, object->materialHandle);
            if (!mesh || !surface || !mesh->vertices || !mesh->indices ||
                mesh->vertexCount == 0u || mesh->indexCount < 3u) {
                continue;
            }
            if ((surface->sourceEnabledSwitchMask &
                 engine::render::backend::
                     WorldSceneSourceMaterialSwitchCastShadow) == 0u) {
                continue;
            }
            ++stats_.drawCount;
            stats_.submittedTriangleCount += mesh->indexCount / 3u;
            const bool alphaCutout =
                surface->alphaMode != 0u && surface->textureRgba &&
                surface->textureWidth > 0 && surface->textureHeight > 0;

            for (const auto& instance : drawClass.instances) {
                ++stats_.instanceCount;
                projected.resize(mesh->vertexCount);
                for (std::size_t vertexIndex = 0u;
                     vertexIndex < mesh->vertexCount;
                     ++vertexIndex) {
                    const auto& vertex = mesh->vertices[vertexIndex];
                    Vec4 local = skinVertex(vertex, instance);
                    Vec4 world = multiply(instance.modelMatrix, local);
                    Vec4 clip = multiply(projection_, world);
                    auto& output = projected[vertexIndex];
                    if (std::abs(clip.w) <= 1.0e-8f) {
                        output.valid = false;
                        continue;
                    }
                    const float reciprocalW = 1.0f / clip.w;
                    const float ndcX = clip.x * reciprocalW;
                    const float ndcY = clip.y * reciprocalW;
                    const float ndcZ = clip.z * reciprocalW;
                    output.x =
                        (ndcX * 0.5f + 0.5f) *
                        static_cast<float>(kNativeAtlasWidth);
                    output.y =
                        (ndcY * 0.5f + 0.5f) *
                        static_cast<float>(kNativeAtlasHeight);
                    output.depth = ndcZ * 0.5f + 0.5f;
                    output.reciprocalW = reciprocalW;
                    output.uOverW = vertex.u * reciprocalW;
                    output.vOverW = vertex.v * reciprocalW;
                    output.valid = std::isfinite(output.x) &&
                        std::isfinite(output.y) &&
                        std::isfinite(output.depth);
                }

                for (std::size_t triangle = 0u;
                     triangle + 2u < mesh->indexCount;
                     triangle += 3u) {
                    const std::uint32_t index0 = mesh->indices[triangle + 0u];
                    const std::uint32_t index1 = mesh->indices[triangle + 1u];
                    const std::uint32_t index2 = mesh->indices[triangle + 2u];
                    if (index0 >= projected.size() ||
                        index1 >= projected.size() ||
                        index2 >= projected.size()) {
                        continue;
                    }
                    const auto& a = projected[index0];
                    const auto& b = projected[index1];
                    const auto& c = projected[index2];
                    if (!a.valid || !b.valid || !c.valid) continue;
                    const float area =
                        edge(a.x, a.y, b.x, b.y, c.x, c.y);
                    if (std::abs(area) <= 1.0e-8f) continue;

                    const int minX = std::max(
                        0,
                        static_cast<int>(std::floor(
                            std::min({a.x, b.x, c.x}) - 0.5f)));
                    const int maxX = std::min(
                        kNativeAtlasWidth - 1,
                        static_cast<int>(std::ceil(
                            std::max({a.x, b.x, c.x}) - 0.5f)));
                    const int minY = std::max(
                        0,
                        static_cast<int>(std::floor(
                            std::min({a.y, b.y, c.y}) - 0.5f)));
                    const int maxY = std::min(
                        kNativeAtlasHeight - 1,
                        static_cast<int>(std::ceil(
                            std::max({a.y, b.y, c.y}) - 0.5f)));
                    if (minX > maxX || minY > maxY) continue;
                    ++stats_.rasterizedTriangleCount;
                    const float inverseArea = 1.0f / area;

                    for (int y = minY; y <= maxY; ++y) {
                        const float pixelY = static_cast<float>(y) + 0.5f;
                        for (int x = minX; x <= maxX; ++x) {
                            const float pixelX =
                                static_cast<float>(x) + 0.5f;
                            const float weight0 =
                                edge(b.x, b.y, c.x, c.y, pixelX, pixelY) *
                                inverseArea;
                            const float weight1 =
                                edge(c.x, c.y, a.x, a.y, pixelX, pixelY) *
                                inverseArea;
                            const float weight2 = 1.0f - weight0 - weight1;
                            constexpr float kInsideTolerance = -1.0e-5f;
                            if (weight0 < kInsideTolerance ||
                                weight1 < kInsideTolerance ||
                                weight2 < kInsideTolerance) {
                                continue;
                            }
                            const float reciprocalW =
                                weight0 * a.reciprocalW +
                                weight1 * b.reciprocalW +
                                weight2 * c.reciprocalW;
                            if (reciprocalW <= 1.0e-8f) continue;
                            if (alphaCutout) {
                                const float u =
                                    (weight0 * a.uOverW +
                                     weight1 * b.uOverW +
                                     weight2 * c.uOverW) /
                                    reciprocalW;
                                const float v =
                                    (weight0 * a.vOverW +
                                     weight1 * b.vOverW +
                                     weight2 * c.vOverW) /
                                    reciprocalW;
                                if (sampleAlpha(*surface, u, v) <=
                                    surface->alphaCutoff) {
                                    continue;
                                }
                            }
                            const float depth =
                                weight0 * a.depth +
                                weight1 * b.depth +
                                weight2 * c.depth;
                            if (depth < 0.0f || depth > 1.0f) continue;
                            const std::size_t pixel =
                                static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(
                                        kNativeAtlasWidth) +
                                static_cast<std::size_t>(x);
                            if (depth < depth_[pixel]) {
                                depth_[pixel] = depth;
                                ++stats_.writtenPixelCount;
                            }
                        }
                    }
                }
            }
        }
    }

    rgba_.resize(pixelCount * 4u);
    constexpr double kDepthMaximum = 16777215.0;
    for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel) {
        const double normalized =
            std::clamp(static_cast<double>(depth_[pixel]), 0.0, 1.0);
        const std::uint32_t packed = static_cast<std::uint32_t>(
            std::llround(normalized * kDepthMaximum));
        rgba_[pixel * 4u + 0u] =
            static_cast<unsigned char>((packed >> 16u) & 0xffu);
        rgba_[pixel * 4u + 1u] =
            static_cast<unsigned char>((packed >> 8u) & 0xffu);
        rgba_[pixel * 4u + 2u] =
            static_cast<unsigned char>(packed & 0xffu);
        rgba_[pixel * 4u + 3u] = 255u;
    }

    std::ostringstream key;
    key << "lgpe:route1:projected-shadow:source-depth-v1:"
        << static_cast<int>(std::lround(sourceCenterCm[0])) << ':'
        << static_cast<int>(std::lround(sourceCenterCm[1])) << ':'
        << static_cast<int>(std::lround(sourceCenterCm[2]));
    textureKey_ = key.str();
    if (outError) outError->clear();
    return true;
}

void Atlas::attach(
    const std::vector<lgpe_world_scene::PreparedScene*>& scenes) const {
    if (rgba_.empty() || textureKey_.empty()) return;
    for (auto* scene : scenes) {
        if (!scene) continue;
        for (auto& surface : scene->registry.materials) {
            if (surface.projectedShadowEnabled == 0u) continue;
            surface.projectedShadowTextureKey = textureKey_;
            surface.projectedShadowTextureCacheKey = textureKey_;
            surface.projectedShadowTextureRgba = rgba_.data();
            surface.projectedShadowTextureWidth = kNativeAtlasWidth;
            surface.projectedShadowTextureHeight = kNativeAtlasHeight;
            surface.projectedShadowTextureWrapS = 33071;
            surface.projectedShadowTextureWrapT = 33071;
            surface.projectedShadowTextureSrgb = 0u;
            surface.projectedShadowMatrix = projection_;
        }
    }
}

} // namespace game::runtime::lgpe_route1_projected_shadow
