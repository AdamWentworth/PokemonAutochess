#pragma once

#include "engine/render/IRenderBackend.h"
#include "game/runtime/BackendModelCache.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace game::runtime::shared_capture_mesh_cache {

struct PreparedCaptureVertex {
    glm::vec3 bindPos{0.0f};
    int nodeIndex = -1;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct PreparedCaptureSubmesh {
    struct NodeChunk {
        int nodeIndex = -1;
        std::vector<std::uint32_t> indices;
        std::vector<IRenderBackend::WorldMeshVertex> compactVertices;
        std::vector<std::uint32_t> compactIndices;
    };

    std::vector<PreparedCaptureVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<NodeChunk> nodeChunks;
    std::vector<IRenderBackend::WorldMeshVertex> scratchVertices;
    bool scratchAtBindPose = false;
    std::uint8_t alphaMode = 0u;
    float alphaCutoff = 0.5f;
};

struct PreparedCaptureMeshCache {
    const runtime::backend_model::MeshData* sourceMesh = nullptr;
    std::size_t sourceVertexCount = 0u;
    std::size_t sourceIndexCount = 0u;
    std::vector<glm::mat4> bindNodeGlobalInv;
    std::vector<PreparedCaptureSubmesh> submeshes;
    std::vector<IRenderBackend::WorldMeshVertex> rigidCombinedVertices;
    std::vector<std::uint32_t> rigidCombinedIndices;
};

struct PrepareResult {
    PreparedCaptureMeshCache* cache = nullptr;
    bool validForRender = false;
    bool earlyReturnAfterPrewarm = false;
    int captureAnimIndex = -1;
    float captureAnimDurationSec = 0.0f;
    bool captureMeshLikelySkinned = false;
};

PrepareResult preparePokeballCaptureMeshCache(
    const runtime::backend_model::MeshData& mesh,
    bool captureSnapsEmpty,
    bool d3d12CapturePrewarmRequested,
    bool treatPokeballAsUntextured,
    bool enableNodeChunkPath,
    IRenderBackend* renderer);

} // namespace game::runtime::shared_capture_mesh_cache

