#include "game/runtime/shared/scene/AuthoredEnvironmentPatch.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <limits>

namespace game::runtime::authored_environment {
namespace {
using CanonicalScene = game::assets::published_environment::CanonicalScene;
using PreparedScene = published_environment_scene::PreparedScene;
bool fail(std::string *outError, std::string message) {
    if (outError) *outError = std::move(message);
    return false;
}
struct SourceBounds {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

SourceBounds transformSourceBounds(
    const std::array<float, 3> &minimum,
    const std::array<float, 3> &maximum,
    const glm::mat4 &transform) {
    glm::vec3 transformedMinimum(
        std::numeric_limits<float>::max());
    glm::vec3 transformedMaximum(
        std::numeric_limits<float>::lowest());
    for (std::uint32_t corner = 0u;
         corner < 8u;
         ++corner) {
        const glm::vec4 transformed =
            transform * glm::vec4(
                            (corner & 1u) != 0u
                                ? maximum[0]
                                : minimum[0],
                            (corner & 2u) != 0u
                                ? maximum[1]
                                : minimum[1],
                            (corner & 4u) != 0u
                                ? maximum[2]
                                : minimum[2],
                            1.0f);
        transformedMinimum = glm::min(
            transformedMinimum,
            glm::vec3(transformed));
        transformedMaximum = glm::max(
            transformedMaximum,
            glm::vec3(transformed));
    }
    return {
        {transformedMinimum.x,
         transformedMinimum.y,
         transformedMinimum.z},
        {transformedMaximum.x,
         transformedMaximum.y,
         transformedMaximum.z}};
}

std::array<float, 16> toArray(const glm::mat4 &matrix) {
    std::array<float, 16> out{};
    std::copy(
        glm::value_ptr(matrix),
        glm::value_ptr(matrix) + out.size(),
        out.begin());
    return out;
}

std::array<float, 16> sourcePlacementMatrix(
    const std::array<float, 3> &translation,
    const std::array<float, 3> &rotation,
    const std::array<float, 3> &scale) {
    return toArray(
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(translation[0], translation[1], translation[2])) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[0]),
            glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[1]),
            glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[2]),
            glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(scale[0], scale[1], scale[2])));
}

bool prepareScene(
    const CanonicalScene &source,
    const PreparedScene &sourceScene,
    PreparedPatch &prepared,
    bool independentTerrain,
    std::string *outError) {
    auto &geometrySource = prepared.geometry;
    auto &patchScene = prepared.scene;
    shared_world_scene::resetWorldSceneRegistry(
        patchScene.registry);
    patchScene.meshVertexStorage.resize(
        geometrySource.meshes.size());
    patchScene.materialStorage.resize(source.materials.size());
    std::size_t polygonGroupCount = 0u;
    for (const auto &mesh : geometrySource.meshes) {
        polygonGroupCount += mesh.polygonGroups.size();
    }
    patchScene.polygonGroupStorage.reserve(polygonGroupCount);

    std::vector<IRenderBackend::WorldSceneMaterialHandle>
        materialHandles(source.materials.size());
    const auto ensurePatchMaterial = [&](std::uint32_t materialIndex)
        -> IRenderBackend::WorldSceneMaterialHandle {
        if (materialIndex >= materialHandles.size() ||
            materialIndex >= sourceScene.registry.materials.size()) {
            return {};
        }
        auto &handle = materialHandles[materialIndex];
        if (handle) return handle;
        patchScene.materialStorage[materialIndex]
            .sourceMaterialIndex = materialIndex;
        auto material = sourceScene.registry.materials[materialIndex];
        // Rebuilt arenas have no source terrain to supply occlusion.
        // Their solid terrain and rocks must cast shadows even when the
        // recovered material disabled casting in the original map.
        using MaterialFamily = IRenderBackend::WorldSceneSourceMaterialFamily;
        if (independentTerrain &&
            (material.sourceMaterialFamily == MaterialFamily::Ground ||
             material.sourceMaterialFamily == MaterialFamily::Cliff ||
             material.sourceMaterialFamily == MaterialFamily::Rock)) {
            material.sourceEnabledSwitchMask |= engine::render::backend::
                WorldSceneSourceMaterialSwitchCastShadow;
        }
        handle = shared_world_scene::ensureMaterial(
            patchScene.registry,
            &patchScene.materialStorage[materialIndex],
            material);
        return handle;
    };

    std::uint32_t instanceId = 1u;
    std::uint32_t drawSlot = 0u;
    constexpr std::uint32_t sourceSemanticMask =
        engine::render::backend::WorldSceneSourceVertexSemanticTexCoord1 |
        engine::render::backend::WorldSceneSourceVertexSemanticTexCoord2 |
        engine::render::backend::WorldSceneSourceVertexSemanticTexCoord3 |
        engine::render::backend::WorldSceneSourceVertexSemanticColor1 |
        engine::render::backend::WorldSceneSourceVertexSemanticColor2 |
        engine::render::backend::WorldSceneSourceVertexSemanticColor3 |
        engine::render::backend::WorldSceneSourceVertexSemanticNormalW |
        engine::render::backend::WorldSceneSourceVertexSemanticBitangent;
    for (std::size_t meshIndex = 0u;
         meshIndex < geometrySource.meshes.size();
         ++meshIndex) {
        const auto &sourceMesh = geometrySource.meshes[meshIndex];
        auto &storage = patchScene.meshVertexStorage[meshIndex];
        storage.vertices.reserve(sourceMesh.vertices.size());
        storage.sourceVertices.reserve(sourceMesh.vertices.size());
        for (const auto &vertex : sourceMesh.vertices) {
            IRenderBackend::WorldMeshVertex compact{};
            compact.x = vertex.position[0];
            compact.y = vertex.position[1];
            compact.z = vertex.position[2];
            compact.nx = vertex.normal[0];
            compact.ny = vertex.normal[1];
            compact.nz = vertex.normal[2];
            compact.tx = vertex.tangent[0];
            compact.ty = vertex.tangent[1];
            compact.tz = vertex.tangent[2];
            compact.tw = vertex.tangent[3];
            compact.u = vertex.texcoords[0][0];
            compact.v = vertex.texcoords[0][1];
            compact.sourceUv1U = vertex.texcoords[1][0];
            compact.sourceUv1V = vertex.texcoords[1][1];
            compact.sourceUv2U = vertex.texcoords[2][0];
            compact.sourceUv2V = vertex.texcoords[2][1];
            compact.r = vertex.colors[0][0];
            compact.g = vertex.colors[0][1];
            compact.b = vertex.colors[0][2];
            compact.a = vertex.colors[0][3];
            compact.joint0 = static_cast<float>(vertex.joints[0]);
            compact.joint1 = static_cast<float>(vertex.joints[1]);
            compact.joint2 = static_cast<float>(vertex.joints[2]);
            compact.joint3 = static_cast<float>(vertex.joints[3]);
            compact.weight0 = vertex.weights[0];
            compact.weight1 = vertex.weights[1];
            compact.weight2 = vertex.weights[2];
            compact.weight3 = vertex.weights[3];
            storage.vertices.push_back(compact);

            IRenderBackend::WorldSceneSourceVertex extended{};
            for (std::size_t channel = 0u;
                 channel < 3u;
                 ++channel) {
                extended.texcoords[channel] =
                    vertex.texcoords[channel + 1u];
                extended.colors[channel] =
                    vertex.colors[channel + 1u];
            }
            extended.normalW = vertex.normalW;
            extended.bitangent = vertex.bitangent;
            storage.sourceVertices.push_back(extended);
        }
        for (std::size_t groupIndex = 0u;
             groupIndex < sourceMesh.polygonGroups.size();
             ++groupIndex, ++drawSlot) {
            const auto &sourceGroup =
                sourceMesh.polygonGroups[groupIndex];
            const auto materialHandle =
                ensurePatchMaterial(sourceGroup.materialIndex);
            if (!materialHandle) {
                return fail(
                    outError,
                    "Environment patch references an unavailable prepared source material.");
            }
            patchScene.polygonGroupStorage.emplace_back();
            auto &groupStorage =
                patchScene.polygonGroupStorage.back();
            groupStorage.geometryCacheKey =
                "published-environment:" +
                geometrySource.profileId + ":mesh:" +
                std::to_string(sourceMesh.sourceIndex) +
                ":group:" + std::to_string(groupIndex);
            groupStorage.indices = sourceGroup.indices;
            const auto geometryHandle =
                shared_world_scene::ensureRigidGeometry(
                    patchScene.registry,
                    &groupStorage,
                    groupStorage.geometryCacheKey.c_str(),
                    storage.vertices.data(),
                    storage.vertices.size(),
                    groupStorage.indices.data(),
                    groupStorage.indices.size(),
                    storage.sourceVertices.data(),
                    storage.sourceVertices.size(),
                    sourceSemanticMask,
                    sourceMesh.sourceIndex,
                    static_cast<std::uint32_t>(groupIndex));
            const auto &material =
                patchScene.registry.materials[materialHandle.id - 1u];
            const auto objectHandle =
                shared_world_scene::ensureRenderObject(
                    patchScene.registry,
                    geometryHandle,
                    materialHandle,
                    shared_world_scene::PipelineVariant::OpaqueLit,
                    drawSlot,
                    false);
            IRenderBackend::WorldSceneRenderInstanceHandle
                instanceHandle{instanceId++};
            const bool skipMainRendering =
                source.materials[sourceGroup.materialIndex]
                    .skipMainRendering ||
                material.sourceMaterialFamily ==
                    IRenderBackend::WorldSceneSourceMaterialFamily::
                        ShadowOnly;
            if (!skipMainRendering) {
                shared_world_scene::appendRigidInstance(
                    patchScene.frame,
                    objectHandle,
                    instanceHandle,
                    sourceMesh.transform,
                    1.0f, 1.0f, 1.0f, 1.0f, 0.0f);
            }
            const bool castsShadow =
                (material.sourceEnabledSwitchMask &
                 engine::render::backend::
                     WorldSceneSourceMaterialSwitchCastShadow) != 0u;
            if (castsShadow) {
                shared_world_scene::appendRigidInstance(
                    patchScene.shadowFrame,
                    objectHandle,
                    instanceHandle,
                    sourceMesh.transform,
                    1.0f, 1.0f, 1.0f, 1.0f, 0.0f);
            }
        }
    }
    patchScene.stats.sourceMeshCount =
        static_cast<std::uint32_t>(geometrySource.meshes.size());
    patchScene.stats.sourcePolygonGroupCount =
        static_cast<std::uint32_t>(polygonGroupCount);
    patchScene.stats.mainPassTriangleCount =
        geometrySource.triangleRecordCount;
    if (outError) outError->clear();
    return true;
}

} // namespace

bool preparePatch(
    const game::assets::published_environment::CanonicalScene &source,
    const published_environment_scene::PreparedScene &sourceScene,
    const engine::assets::phlosion::EnvironmentPatchDocument &patch,
    const engine::assets::phlosion::AuthoredSceneNode &node,
    bool independentTerrain,
    std::uint32_t groundMaterialIndex,
    PreparedPatch &prepared,
    std::string *outError) {
    prepared.nodeId = node.id;
    prepared.geometry.schemaVersion = source.schemaVersion;
    prepared.geometry.profileId =
        source.profileId + ":patch:" + node.id;
    prepared.geometry.sourceModelSha256 = source.sourceModelSha256;
    prepared.geometry.geometrySha256 = source.geometrySha256;
    prepared.geometry.meshes.reserve(patch.meshes.size());
    std::uint64_t triangleCount = 0u;
    SourceBounds patchBounds{
        .minimum = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()},
        .maximum = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()}};
    for (std::size_t meshIndex = 0u;
         meshIndex < patch.meshes.size();
         ++meshIndex) {
        const auto &patchMesh = patch.meshes[meshIndex];
        game::assets::published_environment::Mesh mesh;
        mesh.sourceIndex = static_cast<std::uint32_t>(meshIndex);
        mesh.name = "environment_patch:" + node.id + ":" +
                    patchMesh.id;
        mesh.transform = sourcePlacementMatrix(
            node.transform->translation,
            node.transform->rotationDegrees,
            node.transform->scale);
        mesh.boundsMinimum = {
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        mesh.boundsMaximum = {
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        mesh.attributes = {
            {.semanticHint = "TEXCOORD_1"},
            {.semanticHint = "TEXCOORD_2"},
            {.semanticHint = "TEXCOORD_3"},
            {.semanticHint = "COLOR_1"},
            {.semanticHint = "COLOR_2"},
            {.semanticHint = "COLOR_3"}};
        mesh.vertices.reserve(patchMesh.vertices.size());
        for (const auto &patchVertex : patchMesh.vertices) {
            game::assets::published_environment::CanonicalVertex vertex{
                .position = patchVertex.position,
                .normal = patchVertex.normal,
                .tangent = patchVertex.tangent,
                .bitangent = patchVertex.bitangent,
                .texcoords = patchVertex.texcoords,
                .colors = patchVertex.colors,
                .normalW = patchVertex.normalW,
                .joints = patchVertex.joints,
                .weights = patchVertex.weights};
            mesh.vertices.push_back(vertex);
            for (std::size_t axis = 0u; axis < 3u; ++axis) {
                mesh.boundsMinimum[axis] = std::min(
                    mesh.boundsMinimum[axis], vertex.position[axis]);
                mesh.boundsMaximum[axis] = std::max(
                    mesh.boundsMaximum[axis], vertex.position[axis]);
            }
        }
        mesh.polygonGroups.reserve(patchMesh.materialGroups.size());
        for (const auto &patchGroup : patchMesh.materialGroups) {
            mesh.polygonGroups.push_back(
                game::assets::published_environment::PolygonGroup{
                    .materialIndex = patchGroup.materialIndex,
                    .primitiveType = "Triangles",
                    .indices = patchGroup.indices});
            triangleCount += patchGroup.indices.size() / 3u;
            if (patchGroup.materialIndex == groundMaterialIndex) {
                const auto transform = glm::make_mat4(mesh.transform.data());
                for (std::size_t i = 0; i + 2 < patchGroup.indices.size(); i += 3) {
                    std::array<glm::vec3, 3> triangle;
                    for (std::size_t corner = 0; corner < 3; ++corner) {
                        const auto &p = patchMesh.vertices[patchGroup.indices[i + corner]].position;
                        triangle[corner] = glm::vec3(transform * glm::vec4(p[0], p[1], p[2], 1.0f));
                    }
                    if (!prepared.ground.add(triangle)) {
                        return fail(outError, "Authored floor triangle exceeds supported spatial limits.");
                    }
                }
            }
        }
        const auto transformedBounds = transformSourceBounds(
            mesh.boundsMinimum,
            mesh.boundsMaximum,
            glm::make_mat4(mesh.transform.data()));
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            patchBounds.minimum[axis] = std::min(
                patchBounds.minimum[axis],
                transformedBounds.minimum[axis]);
            patchBounds.maximum[axis] = std::max(
                patchBounds.maximum[axis],
                transformedBounds.maximum[axis]);
        }
        prepared.geometry.meshes.push_back(std::move(mesh));
    }
    prepared.geometry.triangleRecordCount = triangleCount;
    prepared.geometry.uniqueMaterialIndexedTriangleCount =
        triangleCount;
    prepared.boundsMinimum = patchBounds.minimum;
    prepared.boundsMaximum = patchBounds.maximum;
    return prepareScene(source, sourceScene, prepared, independentTerrain, outError);
}

} // namespace game::runtime::authored_environment
