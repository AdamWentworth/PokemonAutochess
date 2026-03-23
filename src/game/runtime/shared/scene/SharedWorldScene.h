#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/shared/world/SharedWorldIndexedBatches.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::runtime::shared_world_scene {

enum class PipelineVariant : std::uint8_t {
    OpaqueLit = 0u,
};

struct WorldSceneRegistry {
    std::vector<IRenderBackend::WorldSceneGeometry> geometries;
    std::vector<IRenderBackend::WorldSceneMaterial> materials;
    std::vector<IRenderBackend::WorldSceneSkeletonLayout> skeletonLayouts;
    std::vector<IRenderBackend::WorldSceneAnimationClip> animationClips;
    std::vector<IRenderBackend::WorldSceneRenderObject> renderObjects;
    std::unordered_map<const void*, IRenderBackend::WorldSceneGeometryHandle> geometryByIdentity;
    std::unordered_map<const void*, IRenderBackend::WorldSceneMaterialHandle> materialByIdentity;
    std::uint32_t generation = 1u;
};

void resetWorldSceneRegistry(WorldSceneRegistry& registry);
void beginWorldSceneFrame(IRenderBackend::WorldSceneFrame& frame);

IRenderBackend::WorldSceneGeometryHandle ensureRigidGeometry(
    WorldSceneRegistry& registry,
    const void* identity,
    const char* geometryCacheKey,
    const IRenderBackend::WorldMeshVertex* vertices,
    std::size_t vertexCount,
    const std::uint32_t* indices,
    std::size_t indexCount);

IRenderBackend::WorldSceneMaterialHandle ensureMaterialFromBatchTemplate(
    WorldSceneRegistry& registry,
    const void* identity,
    const shared_world_batches::WorldIndexedBatch& batchTemplate);

IRenderBackend::WorldSceneRenderObjectHandle ensureRenderObject(
    WorldSceneRegistry& registry,
    IRenderBackend::WorldSceneGeometryHandle geometryHandle,
    IRenderBackend::WorldSceneMaterialHandle materialHandle,
    PipelineVariant pipelineVariant,
    std::uint32_t cookedDrawSlot = 0u,
    bool skinned = false);

void appendRigidInstance(IRenderBackend::WorldSceneFrame& frame,
                         IRenderBackend::WorldSceneRenderObjectHandle objectHandle,
                         IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle,
                         const std::array<float, 16>& modelMatrix,
                         float vertexColorMulR,
                         float vertexColorMulG,
                         float vertexColorMulB,
                         float vertexColorMulA,
                         float sortDepth);

void appendSkinnedInstance(IRenderBackend::WorldSceneFrame& frame,
                           IRenderBackend::WorldSceneRenderObjectHandle objectHandle,
                           IRenderBackend::WorldSceneRenderInstanceHandle instanceHandle,
                           const std::array<float, 16>& modelMatrix,
                           float vertexColorMulR,
                           float vertexColorMulG,
                           float vertexColorMulB,
                           float vertexColorMulA,
                           float sortDepth,
                           std::uint8_t gpuSkinningMode,
                           std::uint32_t skinMatrixCount,
                           const float* skinMatrices);

IRenderBackend::WorldSceneView buildWorldSceneView(
    const WorldSceneRegistry& registry,
    const float* viewProjectionMatrix4x4,
    int surfaceWidth,
    int surfaceHeight,
    const float* cameraWorldPos3 = nullptr,
    const float* cameraForward3 = nullptr,
    const float* cameraTarget3 = nullptr);

IRenderBackend::WorldTextureData makeWorldSceneTextureData(
    const IRenderBackend::WorldSceneMaterial& material,
    const float* cameraWorldPos3 = nullptr,
    const float* cameraForward3 = nullptr,
    const float* cameraTarget3 = nullptr);

} // namespace game::runtime::shared_world_scene
