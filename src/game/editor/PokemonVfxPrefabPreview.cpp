#include "game/editor/PokemonVfxPrefabPreview.h"

#include "engine/render/Camera3D.h"
#include "engine/render/IRenderBackend.h"
#include "engine/tools/vfx_preview/IVfxPreviewEffect.h"
#include "game/preview/PokemonAutochessVfxPreviewProject.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace game::editor {
namespace {

constexpr float kPreviewDurationSeconds = 2.4f;
constexpr float kFixedStepSeconds = 1.0f / 60.0f;

std::string slugify(std::string_view value) {
    std::string slug;
    slug.reserve(value.size());
    bool pendingDash = false;
    for (const unsigned char character : value) {
        if (std::isalnum(character)) {
            if (pendingDash && !slug.empty()) {
                slug.push_back('-');
            }
            slug.push_back(
                static_cast<char>(
                    std::tolower(character)));
            pendingDash = false;
        } else {
            pendingDash = true;
        }
    }
    return slug;
}

bool equals(
    const char* value,
    const std::string& expected) {
    return value &&
           std::string_view(value) ==
               std::string_view(expected);
}

bool projectLine(
    const glm::mat4& viewProjection,
    int width,
    int height,
    const glm::vec3& from,
    const glm::vec3& to,
    IRenderBackend::DebugLine& out) {
    const glm::vec4 clipFrom =
        viewProjection * glm::vec4(from, 1.0f);
    const glm::vec4 clipTo =
        viewProjection * glm::vec4(to, 1.0f);
    if (clipFrom.w <= 0.0001f ||
        clipTo.w <= 0.0001f) {
        return false;
    }
    const glm::vec3 ndcFrom =
        glm::vec3(clipFrom) / clipFrom.w;
    const glm::vec3 ndcTo =
        glm::vec3(clipTo) / clipTo.w;
    out.x1 =
        (ndcFrom.x * 0.5f + 0.5f) *
        static_cast<float>(width);
    out.y1 =
        (0.5f - ndcFrom.y * 0.5f) *
        static_cast<float>(height);
    out.x2 =
        (ndcTo.x * 0.5f + 0.5f) *
        static_cast<float>(width);
    out.y2 =
        (0.5f - ndcTo.y * 0.5f) *
        static_cast<float>(height);
    return true;
}

void drawReferenceGrid(
    IRenderBackend& renderer,
    const Camera3D& camera,
    int width,
    int height) {
    const glm::mat4 viewProjection =
        camera.getProjectionMatrix() *
        camera.getViewMatrix();
    std::vector<IRenderBackend::DebugLine> lines;
    lines.reserve(18u);
    constexpr int divisions = 4;
    constexpr float extent = 1.5f;
    for (int index = -divisions;
         index <= divisions;
         ++index) {
        const float coordinate =
            extent *
            static_cast<float>(index) /
            static_cast<float>(divisions);
        const bool axis = index == 0;
        IRenderBackend::DebugLine xLine{};
        if (projectLine(
                viewProjection,
                width,
                height,
                glm::vec3(-extent, 0.0f, coordinate),
                glm::vec3(extent, 0.0f, coordinate),
                xLine)) {
            xLine.thickness = axis ? 1.5f : 1.0f;
            xLine.r = axis ? 0.28f : 0.42f;
            xLine.g = axis ? 0.78f : 0.48f;
            xLine.b = axis ? 0.58f : 0.52f;
            xLine.a = axis ? 0.52f : 0.18f;
            lines.push_back(xLine);
        }
        IRenderBackend::DebugLine zLine{};
        if (projectLine(
                viewProjection,
                width,
                height,
                glm::vec3(coordinate, 0.0f, -extent),
                glm::vec3(coordinate, 0.0f, extent),
                zLine)) {
            zLine.thickness = axis ? 1.5f : 1.0f;
            zLine.r = axis ? 0.28f : 0.42f;
            zLine.g = axis ? 0.58f : 0.48f;
            zLine.b = axis ? 0.90f : 0.52f;
            zLine.a = axis ? 0.52f : 0.18f;
            lines.push_back(zLine);
        }
    }
    if (!lines.empty()) {
        renderer.drawDebugLines(
            lines.data(),
            lines.size(),
            width,
            height);
    }
}

} // namespace

PokemonVfxPrefabPreview::PokemonVfxPrefabPreview()
    : project_(
          std::make_unique<
              game::preview::
                  PokemonAutochessVfxPreviewProject>()) {
    const std::size_t effectCount =
        project_->effectCount();
    assets_.reserve(effectCount);
    for (std::size_t index = 0u;
         index < effectCount;
         ++index) {
        const std::string displayName(
            project_->effectAt(index).name());
        const std::string slug =
            slugify(displayName);
        assets_.push_back(
            AssetDefinition{
                .id = "vfx/" + slug,
                .displayName = displayName,
                .path = "vfx://" + slug,
                .description =
                    "Authored " + displayName +
                    " effect with live particles, "
                    "materials, timing, and replay."});
    }
    options_.animationIndex = 0;
}

PokemonVfxPrefabPreview::~PokemonVfxPrefabPreview() =
    default;

std::size_t
PokemonVfxPrefabPreview::assetCount() const noexcept {
    return assets_.size();
}

engine::editor::EditorProjectAsset
PokemonVfxPrefabPreview::asset(
    std::size_t index) const noexcept {
    if (index >= assets_.size()) {
        return {};
    }
    const auto& definition = assets_[index];
    return {
        .id = definition.id.c_str(),
        .displayName =
            definition.displayName.c_str(),
        .typeName = "VFX Prefab",
        .category = "VFX Prefabs",
        .path = definition.path.c_str(),
        .description =
            definition.description.c_str(),
        .previewable = true,
    };
}

int PokemonVfxPrefabPreview::findAsset(
    const char* assetId,
    const char* assetPath) const noexcept {
    for (std::size_t index = 0u;
         index < assets_.size();
         ++index) {
        if (equals(assetId, assets_[index].id) ||
            equals(assetPath, assets_[index].path)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool PokemonVfxPrefabPreview::owns(
    const char* assetId,
    const char* assetPath) const noexcept {
    return findAsset(assetId, assetPath) >= 0;
}

bool PokemonVfxPrefabPreview::select(
    const char* assetId,
    const char* assetPath,
    std::string* outError) {
    const int index =
        findAsset(assetId, assetPath);
    if (index < 0) {
        if (outError) {
            *outError =
                "Unknown Pokemon Autochess VFX prefab.";
        }
        return false;
    }
    activeIndex_ = index;
    options_.animationIndex = 0;
    options_.animationPlaying = true;
    options_.playbackSpeed = 1.0f;
    rebuildProjectAndReplay(0.0f);
    if (outError) {
        outError->clear();
    }
    return true;
}

void PokemonVfxPrefabPreview::
    rebuildProjectAndReplay(float seekSeconds) {
    if (activeIndex_ < 0 ||
        static_cast<std::size_t>(activeIndex_) >=
            assets_.size()) {
        return;
    }
    project_ =
        std::make_unique<
            game::preview::
                PokemonAutochessVfxPreviewProject>();
    auto& effect =
        project_->effectAt(
            static_cast<std::size_t>(
                activeIndex_));
    engine::tools::vfx_preview::PreviewSceneState
        scene;
    effect.onActivated(scene);
    scene.emitter.x = 0.0f;
    scene.emitter.z = -0.60f;
    scene.target.x = 0.0f;
    scene.target.z = 0.60f;
    scene.impactPoint = scene.target;
    scene.useCustomImpactPoint = false;
    project_->onEffectActivated(
        static_cast<std::size_t>(activeIndex_));
    project_->requestReplay(
        static_cast<std::size_t>(activeIndex_),
        scene);
    if (seekSeconds > 0.0f) {
        effect.stepFrames(
            static_cast<int>(
                std::floor(
                    seekSeconds /
                    kFixedStepSeconds)),
            scene);
    }
    playheadSeconds_ =
        std::clamp(
            seekSeconds,
            0.0f,
            kPreviewDurationSeconds);
    emptyCooldownSeconds_ = 0.0f;
    status_ =
        "Live authored " +
        assets_[static_cast<std::size_t>(
                    activeIndex_)]
            .displayName +
        " preview through the editor's active "
        "render backend.";
}

void PokemonVfxPrefabPreview::replay() {
    rebuildProjectAndReplay(0.0f);
}

engine::editor::EditorProjectAssetPreviewInfo
PokemonVfxPrefabPreview::info() const noexcept {
    if (!project_ || activeIndex_ < 0 ||
        static_cast<std::size_t>(activeIndex_) >=
            assets_.size()) {
        return {};
    }
    const std::size_t index =
        static_cast<std::size_t>(activeIndex_);
    return {
        .kind =
            engine::editor::
                EditorProjectAssetPreviewKind::
                    VisualEffect,
        .assetId = assets_[index].id.c_str(),
        .status = status_.c_str(),
        .activeElementCount =
            project_->effectAt(index)
                .activeCount(),
        .animationCount = 1u,
        .animationIndex = 0,
        .animationTimeSeconds =
            playheadSeconds_,
        .animationDurationSeconds =
            kPreviewDurationSeconds,
        .boundsRadius = 1.05f,
        .boundsCenterY = 0.42f,
        .ready = true,
    };
}

engine::editor::EditorProjectAssetAnimation
PokemonVfxPrefabPreview::animation(
    std::size_t index) const noexcept {
    if (index != 0u || activeIndex_ < 0) {
        return {};
    }
    return {
        .name = "Effect loop",
        .durationSeconds =
            kPreviewDurationSeconds,
    };
}

void PokemonVfxPrefabPreview::setOptions(
    const engine::editor::
        EditorProjectAssetPreviewOptions& options) {
    options_ = options;
    options_.animationIndex = 0;
    if (options.seekRequested) {
        rebuildProjectAndReplay(
            std::clamp(
                options.seekTimeSeconds,
                0.0f,
                kPreviewDurationSeconds));
    }
}

void PokemonVfxPrefabPreview::update(
    float deltaSeconds) {
    if (!project_ || activeIndex_ < 0 ||
        !options_.animationPlaying) {
        return;
    }
    const std::size_t index =
        static_cast<std::size_t>(activeIndex_);
    auto& effect = project_->effectAt(index);
    const float scaledDelta =
        std::max(0.0f, deltaSeconds) *
        std::max(0.0f, options_.playbackSpeed);
    engine::tools::vfx_preview::PreviewSceneState
        scene;
    scene.emitter =
        glm::vec3(0.0f, 0.45f, -0.60f);
    scene.target =
        glm::vec3(0.0f, 0.35f, 0.60f);
    scene.impactPoint = scene.target;
    effect.update(scaledDelta, scene);
    playheadSeconds_ += scaledDelta;

    if (effect.activeCount() == 0u) {
        emptyCooldownSeconds_ += scaledDelta;
    } else {
        emptyCooldownSeconds_ = 0.0f;
    }
    if (playheadSeconds_ >=
            kPreviewDurationSeconds ||
        emptyCooldownSeconds_ >=
            effect.loopCooldownSec()) {
        replay();
    }
}

void PokemonVfxPrefabPreview::render(
    const engine::editor::
        EditorProjectRenderContext& context) {
    if (!project_ || activeIndex_ < 0 ||
        !context.renderer ||
        !context.cameraWorldPosition3 ||
        !context.cameraTarget3) {
        return;
    }
    const int width =
        std::max(1, context.surfaceWidth);
    const int height =
        std::max(1, context.surfaceHeight);
    Camera3D camera(
        36.0f,
        static_cast<float>(width) /
            static_cast<float>(height),
        0.01f,
        100.0f);
    camera.setPosition(
        glm::make_vec3(
            context.cameraWorldPosition3));
    camera.lookAt(
        glm::make_vec3(context.cameraTarget3));

    IRenderBackend& renderer =
        *context.renderer;
    renderer.beginWorldSceneColorPass(
        width,
        height);
    drawReferenceGrid(
        renderer,
        camera,
        width,
        height);
    const engine::tools::vfx_preview::
        PreviewFrameContext frame{
            camera,
            width,
            height,
            nullptr,
            &renderer};
    project_->effectAt(
        static_cast<std::size_t>(
            activeIndex_))
        .render(frame);
    renderer.endWorldSceneColorPass();
}

} // namespace game::editor
