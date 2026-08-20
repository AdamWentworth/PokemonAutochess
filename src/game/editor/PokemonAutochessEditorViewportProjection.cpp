#include "game/editor/PokemonAutochessEditorViewportProjection.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace game::editor::viewport_projection {
namespace {

struct PreparedContext {
    glm::mat4 viewProjection{1.0f};
    glm::mat4 sourceToWorld{1.0f};
    int surfaceWidth = 0;
    int surfaceHeight = 0;
    bool transformsSource = false;
};

bool prepareContext(
    const Context& context,
    PreparedContext& out) noexcept {
    if (!context.viewProjectionMatrix4x4 ||
        context.surfaceWidth <= 0 ||
        context.surfaceHeight <= 0) {
        return false;
    }
    out.viewProjection =
        glm::make_mat4(context.viewProjectionMatrix4x4);
    if (context.sourceToWorldMatrix4x4) {
        out.sourceToWorld =
            glm::make_mat4(context.sourceToWorldMatrix4x4);
        out.transformsSource = true;
    }
    out.surfaceWidth = context.surfaceWidth;
    out.surfaceHeight = context.surfaceHeight;
    return true;
}

bool projectPreparedPoint(
    const PreparedContext& context,
    const std::array<float, 3>& sourcePoint,
    std::array<float, 2>& outViewportPoint) noexcept {
    glm::vec3 worldPoint{
        sourcePoint[0], sourcePoint[1], sourcePoint[2]};
    if (context.transformsSource) {
        worldPoint = glm::vec3(
            context.sourceToWorld * glm::vec4(worldPoint, 1.0f));
    }
    const glm::vec4 clip =
        context.viewProjection * glm::vec4(worldPoint, 1.0f);
    if (!std::isfinite(clip.x) ||
        !std::isfinite(clip.y) ||
        !std::isfinite(clip.z) ||
        !std::isfinite(clip.w) ||
        std::abs(clip.w) <= 1.0e-6f) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return false;
    }
    outViewportPoint = {
        (ndc.x * 0.5f + 0.5f) *
            static_cast<float>(context.surfaceWidth),
        (0.5f - ndc.y * 0.5f) *
            static_cast<float>(context.surfaceHeight)};
    return std::isfinite(outViewportPoint[0]) &&
        std::isfinite(outViewportPoint[1]);
}

bool projectsHigherRampCorner(
    std::string_view shape,
    float localX,
    float localZ) noexcept {
    return (shape == "ramp_east" && localX > 0.5f) ||
        (shape == "ramp_west" && localX < 0.5f) ||
        (shape == "ramp_north" && localZ > 0.5f) ||
        (shape == "ramp_south" && localZ < 0.5f);
}

} // namespace

bool projectPoint(
    const Context& context,
    const std::array<float, 3>& sourcePoint,
    std::array<float, 2>& outViewportPoint) noexcept {
    PreparedContext prepared;
    if (!prepareContext(context, prepared)) {
        return false;
    }
    return projectPreparedPoint(
        prepared, sourcePoint, outViewportPoint);
}

TransformProjection projectTransform(
    const Context& context,
    const TransformInput& input) noexcept {
    TransformProjection projection{
        .viewportAxisDirections =
            input.fallbackAxisDirections,
        .viewportSourceUnitsPerPixel =
            input.fallbackSourceUnitsPerPixel};
    PreparedContext prepared;
    if (!prepareContext(context, prepared) ||
        !projectPreparedPoint(
            prepared,
            input.sourcePosition,
            projection.viewportPosition)) {
        return projection;
    }
    projection.visible = true;
    for (std::size_t axis = 0u; axis < 3u; ++axis) {
        auto endpoint = input.sourcePosition;
        endpoint[axis] += input.sourceAxisLength;
        std::array<float, 2> viewportEndpoint{};
        if (!projectPreparedPoint(
                prepared, endpoint, viewportEndpoint)) {
            continue;
        }
        const float dx =
            viewportEndpoint[0] - projection.viewportPosition[0];
        const float dy =
            viewportEndpoint[1] - projection.viewportPosition[1];
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.001f) {
            continue;
        }
        projection.viewportAxisDirections[axis * 2u] =
            dx / length;
        projection.viewportAxisDirections[axis * 2u + 1u] =
            dy / length;
        projection.viewportSourceUnitsPerPixel[axis] =
            input.sourceAxisLength / length;
    }
    return projection;
}

TerrainTileProjection projectTerrainTile(
    const Context& context,
    const TerrainTileInput& input) noexcept {
    TerrainTileProjection projection;
    PreparedContext prepared;
    if (!prepareContext(context, prepared)) {
        return projection;
    }
    constexpr std::array<std::array<float, 2>, 4> corners{{
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
    }};
    for (std::size_t corner = 0u;
         corner < corners.size();
         ++corner) {
        const float localX = corners[corner][0];
        const float localZ = corners[corner][1];
        std::int32_t cornerLevel = input.elevationLevel;
        if (projectsHigherRampCorner(
                input.shape, localX, localZ)) {
            ++cornerLevel;
        }
        const std::array<float, 3> sourcePoint{
            (static_cast<float>(input.gridX) + localX) *
                input.tileSize,
            static_cast<float>(cornerLevel) *
                    input.elevationStep +
                input.surfaceLift,
            (static_cast<float>(input.gridZ) + localZ) *
                input.tileSize};
        std::array<float, 2> viewportCorner{};
        if (!projectPreparedPoint(
                prepared, sourcePoint, viewportCorner)) {
            return projection;
        }
        projection.viewportCorners[corner * 2u] =
            viewportCorner[0];
        projection.viewportCorners[corner * 2u + 1u] =
            viewportCorner[1];

        auto flatSourcePoint = sourcePoint;
        flatSourcePoint[1] =
            static_cast<float>(input.elevationLevel) *
                    input.elevationStep +
                input.surfaceLift;
        std::array<float, 2> viewportFlatCorner{};
        std::array<float, 2> viewportNextLevel{};
        auto nextLevelSourcePoint = flatSourcePoint;
        nextLevelSourcePoint[1] += input.elevationStep;
        if (!projectPreparedPoint(
                prepared,
                flatSourcePoint,
                viewportFlatCorner) ||
            !projectPreparedPoint(
                prepared,
                nextLevelSourcePoint,
                viewportNextLevel)) {
            return projection;
        }
        projection.viewportFlatCorners[corner * 2u] =
            viewportFlatCorner[0];
        projection.viewportFlatCorners[corner * 2u + 1u] =
            viewportFlatCorner[1];
        projection.viewportLevelStep[corner * 2u] =
            viewportNextLevel[0] - viewportFlatCorner[0];
        projection.viewportLevelStep[corner * 2u + 1u] =
            viewportNextLevel[1] - viewportFlatCorner[1];
    }
    projection.visible = true;
    return projection;
}

} // namespace game::editor::viewport_projection
