#include "game/preview/PokemonAutochessVfxPreviewProject.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/core/Paths.h"
#include "engine/render/BoardRenderer.h"
#include "engine/render/Camera3D.h"
#include "engine/render/Model.h"
#include "game/PokemonInstance.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"
#include "game/config/PokemonConfigLoader.h"
#include "game/preview/effects/GrowlPreviewEffect.h"
#include "game/preview/effects/LeechSeedPreviewEffect.h"

namespace game::preview {

namespace {

constexpr int kPreviewBoardCols = 8;
constexpr int kPreviewBoardRows = 8;
constexpr float kPreviewBoardCellSize = 1.0f;

std::string lowerCopy(std::string value) {
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

float resolvePreviewModelScaleCorrection(const std::shared_ptr<Model>& model,
                                         const std::string& scaleModeRaw,
                                         const std::string& axisModeRaw) {
    if (!model) return 1.0f;

    const std::string scaleMode = lowerCopy(scaleModeRaw);
    if (scaleMode.empty() || scaleMode == "native" || scaleMode == "raw") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (scaleMode != "normalized") {
        const float importerScale = std::max(0.0f, model->getScaleFactor());
        if (importerScale <= 1e-6f) return 1.0f;
        return 1.0f / importerScale;
    }

    if (!model->hasBounds()) return 1.0f;

    const std::string axisMode = lowerCopy(axisModeRaw);
    if (axisMode.empty() || axisMode == "max") return 1.0f;

    const glm::vec3 ext = model->getBoundsMax() - model->getBoundsMin();
    const float ex = std::max(0.0f, ext.x);
    const float ey = std::max(0.0f, ext.y);
    const float ez = std::max(0.0f, ext.z);
    const float maxExtent = std::max(ex, std::max(ey, ez));
    if (maxExtent <= 1e-6f) return 1.0f;

    float chosenExtent = maxExtent;
    if (axisMode == "x") chosenExtent = ex;
    else if (axisMode == "y") chosenExtent = ey;
    else if (axisMode == "z") chosenExtent = ez;
    else if (axisMode == "median") {
        std::array<float, 3> arr{ex, ey, ez};
        std::sort(arr.begin(), arr.end());
        chosenExtent = arr[1];
    }

    if (chosenExtent <= 1e-6f) return 1.0f;
    return maxExtent / chosenExtent;
}

float computeYawDegreesFromForward(const glm::vec3& forward) {
    glm::vec3 safe(forward.x, 0.0f, forward.z);
    const float lenSq = glm::dot(safe, safe);
    if (lenSq <= 0.000001f) {
        safe = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        safe /= std::sqrt(lenSq);
    }
    return glm::degrees(std::atan2(safe.x, safe.z));
}

glm::mat4 makePreviewInstanceTransform(const glm::vec3& pos, float yawDeg, float scale) {
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 rotationY = glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0));
    glm::mat4 scaling = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    return translation * rotationY * scaling;
}

int resolveIdleAnimIndex(const std::shared_ptr<Model>& model, const std::string& modelPath) {
    if (!model) return -1;

    PokemonInstance inst;
    inst.model = model;
    inst.activeAnimIndex = -1;
    inst.animIdleIndex = -1;
    AnimSet::applyAnimSetOverrides(inst, modelPath, nullptr);
    if (inst.animIdleIndex >= 0) return inst.animIdleIndex;

    static constexpr const char* kFallbackIdleNames[] = {
        "battlewait", "defaultwait", "kw01_wait", "idle", "wait", "ba10_wait", "ba10"
    };
    for (const char* candidate : kFallbackIdleNames) {
        const int idx = model->findAnimationIndexByName(candidate);
        if (idx >= 0) return idx;
    }

    return model->getAnimationCount() > 0 ? 0 : -1;
}

float previewBoardOriginX() {
    return -((static_cast<float>(kPreviewBoardCols) * kPreviewBoardCellSize) / 2.0f) +
           kPreviewBoardCellSize * 0.5f;
}

float previewBoardOriginZ() {
    return -((static_cast<float>(kPreviewBoardRows) * kPreviewBoardCellSize) / 2.0f) +
           kPreviewBoardCellSize * 0.5f;
}

glm::ivec2 clampBoardCell(const glm::ivec2& cell) {
    return glm::ivec2(
        std::clamp(cell.x, 0, kPreviewBoardCols - 1),
        std::clamp(cell.y, 0, kPreviewBoardRows - 1));
}

glm::ivec2 previewWorldToBoardCell(const glm::vec3& pos) {
    const int col = static_cast<int>(std::round((pos.x - previewBoardOriginX()) / kPreviewBoardCellSize));
    const int row = static_cast<int>(std::round((pos.z - previewBoardOriginZ()) / kPreviewBoardCellSize));
    return clampBoardCell(glm::ivec2(col, row));
}

glm::vec3 previewBoardCellToWorld(int col, int row, float y) {
    const glm::ivec2 clamped = clampBoardCell(glm::ivec2(col, row));
    return glm::vec3(
        previewBoardOriginX() + static_cast<float>(clamped.x) * kPreviewBoardCellSize,
        y,
        previewBoardOriginZ() + static_cast<float>(clamped.y) * kPreviewBoardCellSize);
}

glm::ivec2 previewAdjacentCell(const glm::ivec2& origin, glm::ivec2 dir) {
    if (dir == glm::ivec2(0)) dir = glm::ivec2(0, 1);
    glm::ivec2 candidate = origin + dir;
    if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
        candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
        return candidate;
    }

    candidate = origin - dir;
    if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
        candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
        return candidate;
    }

    static constexpr glm::ivec2 kFallbackDirs[] = {
        {0, 1}, {1, 0}, {0, -1}, {-1, 0}
    };
    for (const glm::ivec2 fallback : kFallbackDirs) {
        candidate = origin + fallback;
        if (candidate.x >= 0 && candidate.x < kPreviewBoardCols &&
            candidate.y >= 0 && candidate.y < kPreviewBoardRows) {
            return candidate;
        }
    }

    return origin;
}

struct PreviewPokemonVisual {
    std::string speciesName;
    std::shared_ptr<Model> model;
    std::string loadError;
    std::string modelPath;
    float finalScale = 1.0f;
    float animTimeSec = 0.0f;
    int idleAnimIndex = -1;
    int previewAnimIndex = -1;
    float previewAnimTimeSec = 0.0f;
    bool previewAnimLoop = false;
    bool attemptedLoad = false;
    bool valid = false;

    void ensureLoaded(PokemonConfigLoader& pokemonConfig) {
        if (attemptedLoad) return;
        attemptedLoad = true;

        const PokemonStats* stats = pokemonConfig.getStats(speciesName);
        if (!stats) {
            loadError = "No Pokemon config entry for '" + speciesName + "'";
            return;
        }

        modelPath = engine::paths::asset(std::string("models/") + stats->model);

        try {
            model = std::make_shared<Model>(modelPath);
        } catch (const std::exception& ex) {
            loadError = ex.what();
            model.reset();
            return;
        }

        idleAnimIndex = resolveIdleAnimIndex(model, modelPath);
        finalScale = std::max(
            0.05f,
            model->getScaleFactor() *
                resolvePreviewModelScaleCorrection(model, stats->modelScaleMode, stats->modelScaleAxis) *
                std::max(0.05f, stats->visualScale));
        valid = true;
    }

    int resolveClipAnimIndex(const std::string& clipName) const {
        if (!model || clipName.empty()) return -1;
        return AnimSet::resolveAnimIndex(model.get(), clipName);
    }

    void setPreviewAnimation(int animIndex, bool loop, bool restart) {
        if (animIndex < 0) {
            clearPreviewAnimation();
            return;
        }
        if (restart || previewAnimIndex != animIndex || previewAnimLoop != loop) {
            previewAnimTimeSec = 0.0f;
        }
        previewAnimIndex = animIndex;
        previewAnimLoop = loop;
    }

    void clearPreviewAnimation() {
        previewAnimIndex = -1;
        previewAnimTimeSec = 0.0f;
        previewAnimLoop = false;
    }

    void update(float dt) {
        if (!valid || !model) return;

        const int animIndex = previewAnimIndex >= 0 ? previewAnimIndex : idleAnimIndex;
        if (animIndex < 0) return;
        const float dur = model->getAnimationDurationSec(animIndex);
        if (dur <= 0.0001f) return;

        if (previewAnimIndex >= 0) {
            previewAnimTimeSec = std::max(0.0f, previewAnimTimeSec + std::max(0.0f, dt));
            if (previewAnimLoop) {
                previewAnimTimeSec = std::fmod(previewAnimTimeSec, dur);
                if (previewAnimTimeSec < 0.0f) previewAnimTimeSec += dur;
            } else {
                previewAnimTimeSec = std::min(previewAnimTimeSec, dur);
            }
            return;
        }

        animTimeSec = std::fmod(animTimeSec + std::max(0.0f, dt), dur);
        if (animTimeSec < 0.0f) animTimeSec += dur;
    }

    void draw(const Camera3D& camera, const glm::vec3& worldPos, float yawDeg) const {
        if (!valid || !model) return;
        const int animIndex = previewAnimIndex >= 0 ? previewAnimIndex : idleAnimIndex;
        const float sampleTime = previewAnimIndex >= 0 ? previewAnimTimeSec : animTimeSec;
        if (animIndex < 0) return;
        model->drawAnimated(
            camera,
            makePreviewInstanceTransform(worldPos, yawDeg, finalScale),
            sampleTime,
            animIndex);
    }
};

} // namespace

struct PokemonAutochessVfxPreviewProject::Impl {
    PokemonConfigLoader pokemonConfig;
    bool pokemonConfigLoaded = false;
    AttackAnimConfigLoader attackAnimConfig;
    bool attackAnimConfigLoaded = false;
    PreviewPokemonVisual attackerVisual{"charmander"};
    PreviewPokemonVisual targetVisual{"bulbasaur"};
    std::size_t activeEffectIndex = 0u;
    bool activeEffectWasPlaying = false;

    void ensurePokemonConfigLoaded() {
        if (pokemonConfigLoaded) return;
        pokemonConfigLoaded = pokemonConfig.loadConfig(engine::paths::data("config/pokemon_config.json"));
    }

    void ensureAttackAnimConfigLoaded() {
        if (attackAnimConfigLoaded) return;
        attackAnimConfigLoaded =
            attackAnimConfig.loadConfig(engine::paths::data("config/attack_anim_config.json"));
    }
};

PokemonAutochessVfxPreviewProject::PokemonAutochessVfxPreviewProject()
    : board_(nullptr)
    , impl_(std::make_unique<Impl>()) {
    effects_.push_back(std::make_unique<GrowlPreviewEffect>());
    effects_.push_back(std::make_unique<LeechSeedPreviewEffect>());
}

PokemonAutochessVfxPreviewProject::~PokemonAutochessVfxPreviewProject() = default;

std::string_view PokemonAutochessVfxPreviewProject::projectName() const {
    return "PokemonAutochess";
}

std::size_t PokemonAutochessVfxPreviewProject::effectCount() const {
    return effects_.size();
}

engine::tools::vfx_preview::IVfxPreviewEffect&
PokemonAutochessVfxPreviewProject::effectAt(std::size_t index) {
    if (index >= effects_.size()) throw std::out_of_range("Invalid preview effect index");
    return *effects_[index];
}

const engine::tools::vfx_preview::IVfxPreviewEffect&
PokemonAutochessVfxPreviewProject::effectAt(std::size_t index) const {
    if (index >= effects_.size()) throw std::out_of_range("Invalid preview effect index");
    return *effects_[index];
}

std::size_t PokemonAutochessVfxPreviewProject::rigCount() const {
    return 3u;
}

std::string_view PokemonAutochessVfxPreviewProject::rigName(std::size_t index) const {
    switch (static_cast<RigKind>(index)) {
    case RigKind::FreeScene: return "VFX Only";
    case RigKind::AdjacentBoard: return "Board Preview";
    case RigKind::PokemonModels: return "Pokemon Models";
    }
    return "Unknown";
}

bool PokemonAutochessVfxPreviewProject::defaultPrimaryBackdropEnabled(std::size_t rigIndex) const {
    return static_cast<RigKind>(rigIndex) != RigKind::FreeScene;
}

bool PokemonAutochessVfxPreviewProject::defaultSecondaryBackdropEnabled(std::size_t rigIndex) const {
    (void)rigIndex;
    return false;
}

void PokemonAutochessVfxPreviewProject::onEffectActivated(std::size_t effectIndex) {
    impl_->activeEffectIndex = effectIndex;
    impl_->activeEffectWasPlaying = false;
    impl_->attackerVisual.clearPreviewAnimation();
    impl_->targetVisual.clearPreviewAnimation();
}

void PokemonAutochessVfxPreviewProject::applyRigDefaults(
    std::size_t rigIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) const {
    switch (static_cast<RigKind>(rigIndex)) {
    case RigKind::FreeScene:
        scene.emitter = glm::vec3(0.0f, 0.42f, 0.0f);
        scene.target = glm::vec3(0.0f, 0.35f, 4.2f);
        break;
    case RigKind::AdjacentBoard:
        scene.emitter = previewBoardCellToWorld(3, 3, 0.42f);
        scene.target = previewBoardCellToWorld(3, 4, 0.35f);
        break;
    case RigKind::PokemonModels:
        scene.emitter = previewBoardCellToWorld(3, 3, 0.62f);
        scene.target = previewBoardCellToWorld(3, 4, 0.35f);
        break;
    }
}

void PokemonAutochessVfxPreviewProject::constrainScene(
    std::size_t rigIndex,
    engine::tools::vfx_preview::PreviewSceneState& scene) const {
    const RigKind rig = static_cast<RigKind>(rigIndex);
    if (rig != RigKind::AdjacentBoard && rig != RigKind::PokemonModels) return;

    const glm::ivec2 emitterCell = previewWorldToBoardCell(scene.emitter);
    const glm::ivec2 targetCellGuess = previewWorldToBoardCell(scene.target);
    glm::ivec2 delta = targetCellGuess - emitterCell;
    if (delta == glm::ivec2(0)) {
        delta = glm::ivec2(0, 1);
    } else if (std::abs(delta.x) > std::abs(delta.y)) {
        delta = glm::ivec2(delta.x >= 0 ? 1 : -1, 0);
    } else {
        delta = glm::ivec2(0, delta.y >= 0 ? 1 : -1);
    }

    const glm::ivec2 targetCell = previewAdjacentCell(emitterCell, delta);
    scene.emitter = previewBoardCellToWorld(emitterCell.x, emitterCell.y, scene.emitter.y);
    scene.target = previewBoardCellToWorld(targetCell.x, targetCell.y, 0.35f);
}

void PokemonAutochessVfxPreviewProject::update(
    float dt,
    std::size_t rigIndex,
    const engine::tools::vfx_preview::PreviewSceneState& scene) {
    (void)scene;
    if (static_cast<RigKind>(rigIndex) != RigKind::PokemonModels) return;

    impl_->ensurePokemonConfigLoaded();
    impl_->ensureAttackAnimConfigLoaded();
    impl_->attackerVisual.ensureLoaded(impl_->pokemonConfig);
    impl_->targetVisual.ensureLoaded(impl_->pokemonConfig);

    const engine::tools::vfx_preview::IVfxPreviewEffect* activeEffect =
        impl_->activeEffectIndex < effects_.size() ? effects_[impl_->activeEffectIndex].get() : nullptr;
    const bool effectPlaying = activeEffect && activeEffect->activeCount() > 0u;
    const auto casterAnimRequest =
        activeEffect ? activeEffect->casterAnimationRequest()
                     : engine::tools::vfx_preview::PreviewCasterAnimationRequest{};

    if (casterAnimRequest.valid() && effectPlaying && !impl_->activeEffectWasPlaying) {
        const std::string clipName = impl_->attackAnimConfig.getClipName(
            impl_->attackerVisual.speciesName,
            std::string(casterAnimRequest.kind),
            std::string(casterAnimRequest.move),
            std::string(casterAnimRequest.phase));
        const int clipIndex = impl_->attackerVisual.resolveClipAnimIndex(clipName);
        impl_->attackerVisual.setPreviewAnimation(clipIndex, false, true);
    } else if ((!casterAnimRequest.valid() || !effectPlaying) && impl_->attackerVisual.previewAnimIndex >= 0) {
        impl_->attackerVisual.clearPreviewAnimation();
    }

    impl_->targetVisual.clearPreviewAnimation();
    impl_->activeEffectWasPlaying = effectPlaying;

    impl_->attackerVisual.update(dt);
    impl_->targetVisual.update(dt);
}

void PokemonAutochessVfxPreviewProject::renderBackdrop(
    const engine::tools::vfx_preview::PreviewFrameContext& frame,
    std::size_t rigIndex,
    const engine::tools::vfx_preview::PreviewSceneState& scene,
    bool primaryBackdropEnabled,
    bool secondaryBackdropEnabled) {
    if (!board_) {
        board_ = std::make_unique<BoardRenderer>(kPreviewBoardRows, kPreviewBoardCols, kPreviewBoardCellSize, nullptr);
    }
    if (primaryBackdropEnabled && board_) board_->draw(frame.camera);
    if (secondaryBackdropEnabled && board_) board_->drawBench(frame.camera);

    if (static_cast<RigKind>(rigIndex) != RigKind::PokemonModels) return;

    impl_->ensurePokemonConfigLoaded();
    impl_->attackerVisual.ensureLoaded(impl_->pokemonConfig);
    impl_->targetVisual.ensureLoaded(impl_->pokemonConfig);

    const glm::vec3 casterPos(scene.emitter.x, 0.0f, scene.emitter.z);
    const glm::vec3 targetPos(scene.target.x, 0.0f, scene.target.z);

    impl_->attackerVisual.draw(
        frame.camera,
        casterPos,
        computeYawDegreesFromForward(targetPos - casterPos));
    impl_->targetVisual.draw(
        frame.camera,
        targetPos,
        computeYawDegreesFromForward(casterPos - targetPos));
}

void PokemonAutochessVfxPreviewProject::appendDebugMarkers(
    engine::tools::vfx_preview::IPreviewDebugDraw& draw,
    const engine::tools::vfx_preview::PreviewSceneState& scene) const {
    const glm::vec3 emitterColor(1.0f, 0.52f, 0.16f);
    const glm::vec3 targetColor(0.28f, 0.95f, 0.55f);
    const glm::vec3 guideColor(0.95f, 0.90f, 0.35f);

    draw.addCross(scene.emitter, 0.16f, emitterColor);
    draw.addCircleXZ(glm::vec3(scene.emitter.x, 0.015f, scene.emitter.z), 0.20f, emitterColor, 28);

    draw.addCross(scene.target, 0.18f, targetColor);
    draw.addCircleXZ(glm::vec3(scene.target.x, 0.015f, scene.target.z), 0.24f, targetColor, 28);

    const glm::vec3 guideStart = scene.emitter + glm::vec3(0.0f, 0.02f, 0.0f);
    const glm::vec3 guideEnd = glm::vec3(scene.target.x, guideStart.y, scene.target.z);
    draw.addArrow(guideStart, guideEnd, guideColor);
}

std::vector<std::string> PokemonAutochessVfxPreviewProject::overlayLines(
    const engine::tools::vfx_preview::PreviewSceneState& scene,
    std::size_t rigIndex) const {
    (void)scene;
    switch (static_cast<RigKind>(rigIndex)) {
    case RigKind::AdjacentBoard:
        return {
            "Board Preview keeps the target one board cell away from the emitter.",
            "Use this to judge timing and spacing in gameplay-like staging."
        };
    case RigKind::FreeScene:
        return {
            "VFX Only removes board constraints so you can tune the effect in isolation.",
            "Backdrop defaults off in this mode, but G/B can still toggle guides back on."
        };
    case RigKind::PokemonModels:
        return {
            "Pokemon Models shows Charmander as caster and Bulbasaur as target.",
            "This mode still uses the effect marker positions, with the models as visual staging references."
        };
    }
    return {};
}

} // namespace game::preview
