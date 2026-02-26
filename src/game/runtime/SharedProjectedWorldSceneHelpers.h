#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/PokemonInstance.h"
#include "game/config/GameDataDb.h"
#include "game/runtime/BackendModelCache.h"
#include "game/runtime/SharedBackendTextureCache.h"
#include "game/runtime/SharedBoardGridBatches.h"
#include "game/runtime/SharedCaptureModelBridge.h"
#include "game/runtime/SharedProjectedDebugVfx.h"
#include "game/runtime/SharedTailFireFallbackEmitter.h"
#include "game/runtime/SharedWorldIndexedBatches.h"
#include "game/vfx/TailFireVFX.h"
#include "game/world/GameWorld.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_projected_scene {

struct DepthTri {
    IRenderBackend::DebugTriangle tri;
    float depth = 0.0f;
};

struct DepthWorldTri {
    IRenderBackend::WorldTriangle tri;
    float depth = 0.0f;
};

struct ModelDepthBuffers {
    std::vector<DepthTri>& modelDepthTris;
    std::vector<DepthWorldTri>& modelDepthWorldTris;
};

ModelDepthBuffers acquireModelDepthBuffers(std::size_t reserveCount = 12000u);

void flushModelDepthBuffers(std::vector<DepthTri>& modelDepthTris,
                            std::vector<DepthWorldTri>& modelDepthWorldTris,
                            std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                            std::vector<IRenderBackend::WorldTriangle>& world3DTriangles);

shared_board_grid::Config makeBoardGridConfig(bool supportsWorldTriangles3D,
                                              int rows,
                                              int cols,
                                              int benchSlots,
                                              float worldCellSize,
                                              float boardMinX,
                                              float boardMinZ,
                                              float boardMaxX,
                                              float boardMaxZ,
                                              float boardX,
                                              float boardY,
                                              float boardW,
                                              float boardH,
                                              float cellW,
                                              float cellH,
                                              float line);

void appendBoardAndBench(const shared_board_grid::Config& cfg,
                         std::vector<IRenderBackend::DebugTriangle>& worldTriangles,
                         std::vector<IRenderBackend::WorldTriangle>& world3DTriangles,
                         std::vector<IRenderBackend::DebugQuad>& worldBackgroundQuads,
                         std::vector<IRenderBackend::DebugLine>& lines,
                         shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug);

const TailFireVFX::Config& getTailFireFallbackCfg();

const runtime::backend_model::MeshData* resolveModelMesh(
    const PokemonInstance& unit,
    const ::GameDataDb& dataDb,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded);

struct GrowlWaveVfxArgs {
    bool useLegacyGrowlWaveVfx = false;
    bool supportsWorldIndexedMeshes = false;
    bool hasWorldViewProj = false;
    GameWorld* gameWorld = nullptr;
    glm::vec3 cameraWorldPos{0.0f};
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::function<runtime::backend_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

void appendSharedGrowlWaveVfx(const GrowlWaveVfxArgs& args);

void appendSharedGrowlWaveVfxSession(
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    GameWorld* gameWorld,
    const glm::vec3& cameraWorldPos,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded);

struct ParticleVfxArgs {
    bool useLegacyParticleVfxSnapshotBridge = false;
    bool supportsWorldIndexedMeshes = false;
    bool hasWorldViewProj = false;
    bool useExactTailFireCpuPath = false;
    GameWorld* gameWorld = nullptr;
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec3 cameraWorldPos{0.0f};
    int drawableW = 0;
    int drawableH = 0;
    float worldCellSize = 1.0f;
    double simNowSec = 0.0;
    float lineThickness = 1.0f;
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>* sharedTailFireAnchors = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    shared_projected_debug::ProjectedDebugVfxBuilder* projectedDebug = nullptr;
    std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)> ensureBackendTextureLoaded;
};

void appendSharedParticleVfx(const ParticleVfxArgs& args);

void appendSharedParticleVfxSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    bool useExactTailFireCpuPath,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    float worldCellSize,
    double simNowSec,
    float lineThickness,
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>& sharedTailFireAnchors,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded);

void appendSharedProjectedVfxBridgesSession(
    bool useLegacyParticleVfxSnapshotBridge,
    bool useLegacyGrowlWaveVfx,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    bool useExactTailFireCpuPath,
    GameWorld* gameWorld,
    const glm::mat4& viewProj,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraWorldPos,
    int drawableW,
    int drawableH,
    float worldCellSize,
    double simNowSec,
    float lineThickness,
    std::unordered_map<int, shared_tail_fire_fallback::Anchor>& sharedTailFireAnchors,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    shared_projected_debug::ProjectedDebugVfxBuilder& projectedDebug,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&, bool)>& ensureBackendTextureLoaded);

runtime::shared_capture::SnapshotCache makeSharedCaptureSnapshotCache(std::size_t reserveCount = 8u);

struct CaptureModelBridgeArgs {
    GameWorld* gameWorld = nullptr;
    bool supportsWorldIndexedMeshes = false;
    bool hasWorldViewProj = false;
    int drawableW = 0;
    int drawableH = 0;
    float worldCellSize = 1.0f;
    const float* worldViewProj = nullptr;
    glm::vec3 cameraWorldPos{0.0f};
    runtime::shared_capture::SnapshotCache* sharedCaptureAttemptCache = nullptr;
    IRenderBackend* renderer = nullptr;
    std::vector<shared_world_batches::WorldIndexedBatch>* worldIndexedBatches = nullptr;
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>* backendTextureByPath = nullptr;
    std::function<runtime::backend_model::MeshData*(const std::string&)> ensureBackendMeshLoaded;
    std::function<SharedBackendTextureCacheEntry*(const std::string&)> ensureBackendTextureLoaded;
};

bool appendSharedCaptureAttemptModels(const CaptureModelBridgeArgs& args);

bool appendSharedCaptureAttemptModelsSession(
    GameWorld* gameWorld,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    int drawableW,
    int drawableH,
    float worldCellSize,
    const float* worldViewProj,
    const glm::vec3& cameraWorldPos,
    runtime::shared_capture::SnapshotCache& sharedCaptureAttemptCache,
    IRenderBackend* renderer,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&)>& ensureBackendTextureLoaded);

bool appendSharedCaptureAttemptModelsIfNeededForProjectedWorld(
    IRenderBackend* renderer,
    GameWorld* gameWorld,
    bool supportsWorldIndexedMeshes,
    bool hasWorldViewProj,
    int drawableW,
    int drawableH,
    float worldCellSize,
    const float* worldViewProj,
    const glm::vec3& cameraWorldPos,
    runtime::shared_capture::SnapshotCache& sharedCaptureAttemptCache,
    std::vector<shared_world_batches::WorldIndexedBatch>& worldIndexedBatches,
    std::unordered_map<std::string, SharedBackendTextureCacheEntry>& backendTextureByPath,
    const std::function<runtime::backend_model::MeshData*(const std::string&)>& ensureBackendMeshLoaded,
    const std::function<SharedBackendTextureCacheEntry*(const std::string&)>& ensureBackendTextureLoaded);

} // namespace game::runtime::shared_projected_scene
