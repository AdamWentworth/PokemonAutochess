#include "game/runtime/shared/ui/SharedBackendDebugViewOverlay.h"

#include "engine/core/EngineServices.h"
#include "engine/core/ecs/World.h"
#include "engine/render/Camera3D.h"
#include "game/GameServices.h"
#include "game/GameWorld.h"
#include "game/ecs/CombatActive.h"
#include "game/ecs/RoundState.h"
#include "game/logging/LogBus.h"
#include "game/runtime/BackendDebugText.h"
#include "game/runtime/BackendHudFormatting.h"
#include "game/runtime/BackendInventoryOverlay.h"
#include "game/runtime/BackendStatusText.h"
#include "game/runtime/shared/capture/SharedCapturePresentation.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace {
std::string trimDebugLine(std::string s, std::size_t maxChars) {
    if (s.size() <= maxChars) return s;
    if (maxChars <= 3) return s.substr(0, maxChars);
    return s.substr(0, maxChars - 3) + "...";
}

struct BackendItemAtlasIcon {
    const char* id;
    int row;
    int col;
};

const BackendItemAtlasIcon* findBackendItemAtlasIcon(const std::string& id) {
    static const BackendItemAtlasIcon kIcons[] = {
        {"pokeball", 1, 4},
        {"potion", 2, 4},
        {"burn_heal", 2, 6},
        {"antidote", 2, 5},
        {"paralyze_heal", 2, 9},
    };
    for (const auto& icon : kIcons) {
        if (id == icon.id) return &icon;
    }
    return nullptr;
}

glm::vec2 backendItemAtlasUvMin(int row, int col) {
    constexpr int kCols = 13;
    constexpr int kRows = 14;
    constexpr float kPadU = 0.08f;
    constexpr float kPadV = 0.08f;
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u0 = static_cast<float>(c - 1) / static_cast<float>(kCols);
    float v0 = static_cast<float>(r - 1) / static_cast<float>(kRows);
    u0 += (kPadU / static_cast<float>(kCols));
    v0 += (kPadV / static_cast<float>(kRows));
    return {u0, v0};
}

glm::vec2 backendItemAtlasUvMax(int row, int col) {
    constexpr int kCols = 13;
    constexpr int kRows = 14;
    constexpr float kPadURight = 0.06f;
    constexpr float kPadVBottom = 0.06f;
    const int c = std::max(1, col);
    const int r = std::max(1, row);
    float u1 = static_cast<float>(c) / static_cast<float>(kCols);
    float v1 = static_cast<float>(r) / static_cast<float>(kRows);
    u1 -= (kPadURight / static_cast<float>(kCols));
    v1 -= (kPadVBottom / static_cast<float>(kRows));
    return {u1, v1};
}

std::string toLowerCopy(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

namespace game::runtime::shared_backend_debug_view {

void composeAndSubmit(const ComposeAndSubmitArgs& args) {
    if (!args.renderer || !args.ecsWorld || !args.log ||
        !args.backendInventoryPanel || !args.worldBackgroundQuads || !args.worldQuads ||
        !args.worldTriangles || !args.world3DTriangles || !args.worldIndexedBatches ||
        !args.overlayQuads || !args.lines || !args.textLines || !args.sprites) {
        return;
    }

    auto* renderer = args.renderer;
    auto* engineServices = args.engineServices;
    auto* services = args.services;
    auto* gameWorld = args.gameWorld;
    auto* camera = args.camera;
    auto& ecsWorld = *args.ecsWorld;
    auto roundPhaseEntity = args.roundPhaseEntity;
    auto& log = *args.log;
    auto& backendInventoryPanel = *args.backendInventoryPanel;
    const auto& refreshBackendInventoryFromWorld = args.refreshBackendInventoryFromWorld;
    const bool showPerfOverlay = args.showPerfOverlay;
    const bool renderWorld = args.renderWorld;
    const bool hasWorldViewProj = args.hasWorldViewProj;
    const bool supportsWorldTriangles3D = args.supportsWorldTriangles3D;
    const bool supportsWorldIndexedMeshes = args.supportsWorldIndexedMeshes;
    const int drawableW = args.drawableW;
    const int drawableH = args.drawableH;
    const float edgePad = args.edgePad;
    const float lineStep = args.lineStep;
    const float uiScale = args.uiScale;
    const float* worldViewProj = args.worldViewProj;
    auto& worldBackgroundQuads = *args.worldBackgroundQuads;
    auto& worldQuads = *args.worldQuads;
    auto& worldTriangles = *args.worldTriangles;
    auto& world3DTriangles = *args.world3DTriangles;
    auto& worldIndexedBatches = *args.worldIndexedBatches;
    auto& overlayQuads = *args.overlayQuads;
    auto& lines = *args.lines;
    auto& textLines = *args.textLines;
    auto& sprites = *args.sprites;
        if (showPerfOverlay && engineServices) {
            const EngineFramePerfStats& perf = engineServices->framePerf;
            if (perf.fps > 0.0f) {
                const float fpsNorm = std::clamp(perf.fps / 120.0f, 0.0f, 1.0f);
                IRenderBackend::DebugQuad fpsBarBg;
                fpsBarBg.x = edgePad;
                fpsBarBg.y = std::max(8.0f, edgePad - lineStep * 0.2f);
                fpsBarBg.w = std::clamp(220.0f * uiScale, 140.0f, 320.0f);
                fpsBarBg.h = std::clamp(10.0f * uiScale, 8.0f, 16.0f);
                fpsBarBg.r = 0.15f;
                fpsBarBg.g = 0.15f;
                fpsBarBg.b = 0.18f;
                fpsBarBg.a = 1.0f;
                overlayQuads.push_back(fpsBarBg);

                IRenderBackend::DebugQuad fpsBar = fpsBarBg;
                fpsBar.w *= fpsNorm;
                fpsBar.r = (fpsNorm < 0.5f) ? 0.85f : 0.30f;
                fpsBar.g = (fpsNorm < 0.5f) ? 0.28f : 0.88f;
                fpsBar.b = 0.30f;
                overlayQuads.push_back(fpsBar);
            }
        }

        const auto appendText = [&](float x,
                                    float y,
                                    const std::string& text,
                                    float scale,
                                    const glm::vec3& color) {
            runtime::backend_text::appendTextLines(
                textLines, x, y, text, scale, color.r, color.g, color.b, 1.0f, 0.88f);
        };
        const auto appendRightText = [&](float y,
                                         const std::string& text,
                                         float scale,
                                         const glm::vec3& color) {
            const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
            const float x = std::max(edgePad, static_cast<float>(drawableW) - textW - edgePad);
            appendText(x, y, text, scale, color);
        };

        if (engineServices) {
            const EngineFramePerfStats& perf = engineServices->framePerf;
            if (perf.fps > 0.0f) {
                std::ostringstream perfLine;
                perfLine << std::fixed << std::setprecision(1)
                         << "FPS: " << perf.fps
                         << " | CPU: " << perf.frameMs << "ms";
                if (perf.gpuFrameValid) {
                    perfLine << " | GPU: " << perf.gpuFrameMs << "ms";
                } else {
                    perfLine << " | GPU: n/a";
                }
                appendRightText(edgePad,
                                perfLine.str(),
                                std::clamp(1.0f * uiScale, 0.82f, 1.35f),
                                glm::vec3(0.84f, 0.95f, 0.90f));

                std::ostringstream buildLine;
                buildLine << std::fixed << std::setprecision(1)
                          << "Build: " << perf.renderBuildMs << "ms"
                          << " | Submit: " << perf.renderSubmitMs << "ms";
                if (perf.projectedUnitsMs > 0.01f || perf.projectedUnitsProcessed > 0u) {
                    buildLine << " | Proj: " << perf.projectedUnitsMs << "ms"
                              << " (pose " << perf.projectedPoseEvalMs
                              << ", model " << perf.projectedModelMs
                              << ", overlay " << perf.projectedOverlayMs << ")";
                }
                appendRightText(edgePad + lineStep * 0.55f,
                                buildLine.str(),
                                std::clamp(0.82f * uiScale, 0.68f, 1.05f),
                                glm::vec3(0.72f, 0.86f, 0.96f));
            }
        }

        const std::string mode = (services ? services->gameMode : std::string("classic"));
        appendRightText(edgePad + lineStep * 1.1f,
                        runtime::backend_status_text::modeLine(mode),
                        std::clamp(1.2f * uiScale, 0.95f, 1.7f),
                        glm::vec3(0.93f, 0.95f, 0.99f));
        if (services) {
            appendRightText(edgePad + lineStep * 2.2f,
                            runtime::backend_status_text::backendLine(
                                services->activeRendererBackend,
                                services->gpuRenderer),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.68f, 0.80f, 0.94f));
        }

        RoundPhase roundPhase = RoundPhase::Planning;
        bool combatActive = false;
        if (ecsWorld.alive(roundPhaseEntity)) {
            if (const auto* roundState = ecsWorld.get<game::RoundState>(roundPhaseEntity)) {
                roundPhase = roundState->phase;
            }
            if (const auto* combatState = ecsWorld.get<game::CombatActive>(roundPhaseEntity)) {
                combatActive = combatState->active;
            }
        }
        appendRightText(edgePad + lineStep * 3.3f,
                        runtime::backend_status_text::roundLine(roundPhase, combatActive),
                        std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                        glm::vec3(0.83f, 0.91f, 0.98f));

        int playerAlive = 0;
        int enemyAlive = 0;
        if (gameWorld) {
            for (const auto& unit : gameWorld->getPokemons()) {
                if (!unit.alive && !unit.captureInProgress) continue;
                if (unit.side == PokemonSide::Player) ++playerAlive;
                else ++enemyAlive;
            }
            appendRightText(edgePad + lineStep * 4.4f,
                            runtime::backend_status_text::unitsLine(playerAlive, enemyAlive),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.72f, 0.90f, 0.84f));
            appendRightText(edgePad + lineStep * 5.5f,
                            runtime::backend_status_text::goldLine(gameWorld->getMoney()),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.96f, 0.88f, 0.56f));
            const std::string selectedItem = gameWorld->getSelectedItem();
            if (!selectedItem.empty()) {
                appendRightText(edgePad + lineStep * 6.6f,
                                runtime::backend_status_text::selectedItemLine(selectedItem),
                                std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                                glm::vec3(0.84f, 0.90f, 0.98f));
            }

            refreshBackendInventoryFromWorld();
            const auto& inventoryModel = backendInventoryPanel.model;
            const float leftX = edgePad;
            const float invStartY = edgePad + lineStep * 7.7f;
            const bool adventureModeInventoryIcons = services && services->gameMode == "adventure";

            if (inventoryModel.totalCount > 0 || !selectedItem.empty()) {
                if (adventureModeInventoryIcons) {
                    const float panelScale = std::clamp(uiScale, 0.85f, 1.30f);
                    const float cardW = std::round(std::clamp(72.0f * panelScale, 60.0f, 90.0f));
                    const float cardH = cardW;
                    const float countScale = std::clamp(0.86f * panelScale, 0.72f, 1.05f);
                    const float titleScale = std::clamp(1.00f * panelScale, 0.82f, 1.20f);
                    const float labelScale = std::clamp(0.78f * panelScale, 0.68f, 0.95f);
                    const float navScale = std::clamp(0.80f * panelScale, 0.68f, 0.95f);
                    const float titleH =
                        std::max(12.0f, runtime::backend_text::measureTextHeight("Items", titleScale));
                    const float countH =
                        std::max(10.0f, runtime::backend_text::measureTextHeight("x99", countScale));
                    const float nameH =
                        std::max(10.0f, runtime::backend_text::measureTextHeight("Pokeball", labelScale));
                    const float rowPitch = cardH + std::max(6.0f, countH + 4.0f) + std::max(8.0f, nameH + 8.0f);
                    const float rightInset = std::round(std::max(edgePad, 24.0f * panelScale));
                    const float panelX = std::round(static_cast<float>(drawableW) - rightInset - cardW);
                    const float panelTop = std::round(std::max(
                        invStartY,
                        std::max(110.0f, static_cast<float>(drawableH) * 0.16f)));

                    const std::string title = runtime::backend_inventory::makeTitleLabel(inventoryModel);
                    const float titleW = std::max(1.0f, runtime::backend_text::measureTextWidth(title, titleScale));
                    appendText(std::max(edgePad, panelX + cardW - titleW),
                               panelTop,
                               title,
                               titleScale,
                               glm::vec3(0.92f, 0.95f, 0.99f));

                    float navY = panelTop + titleH + std::max(4.0f, lineStep * 0.20f);
                    const bool hasPrev = runtime::backend_inventory::canScrollPrev(inventoryModel);
                    const bool hasNext = runtime::backend_inventory::canScrollNext(inventoryModel);
                    if (hasPrev || hasNext) {
                        const std::string prevLabel = "[Up] Prev";
                        const std::string nextLabel = "[Down] Next";
                        appendText(panelX,
                                   navY,
                                   prevLabel,
                                   navScale,
                                   hasPrev ? glm::vec3(0.75f, 0.87f, 0.96f)
                                           : glm::vec3(0.42f, 0.48f, 0.55f));
                        if (hasPrev) {
                            runtime::backend_inventory_panel::HitRegion prevHit;
                            prevHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                            prevHit.offsetDelta = -1;
                            prevHit.x = panelX;
                            prevHit.y = navY;
                            prevHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, navScale));
                            prevHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(prevLabel, navScale));
                            backendInventoryPanel.hitRegions.push_back(std::move(prevHit));
                        }

                        const float nextW =
                            std::max(1.0f, runtime::backend_text::measureTextWidth(nextLabel, navScale));
                        const float nextX = panelX + cardW - nextW;
                        appendText(nextX,
                                   navY,
                                   nextLabel,
                                   navScale,
                                   hasNext ? glm::vec3(0.75f, 0.87f, 0.96f)
                                           : glm::vec3(0.42f, 0.48f, 0.55f));
                        if (hasNext) {
                            runtime::backend_inventory_panel::HitRegion nextHit;
                            nextHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                            nextHit.offsetDelta = 1;
                            nextHit.x = nextX;
                            nextHit.y = navY;
                            nextHit.w = nextW;
                            nextHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(nextLabel, navScale));
                            backendInventoryPanel.hitRegions.push_back(std::move(nextHit));
                        }
                        navY += std::max(12.0f, runtime::backend_text::measureTextHeight(prevLabel, navScale)) +
                                std::max(6.0f, lineStep * 0.12f);
                    }

                    for (std::size_t i = 0; i < inventoryModel.visibleEntries.size(); ++i) {
                        const auto& entry = inventoryModel.visibleEntries[i];
                        const bool selected = (entry.id == selectedItem);
                        const float y = navY + static_cast<float>(i) * rowPitch;
                        const float cardX = panelX;
                        const float cardY = y;

                        IRenderBackend::DebugQuad shadow;
                        shadow.x = cardX + 2.0f;
                        shadow.y = cardY + 2.0f;
                        shadow.w = cardW;
                        shadow.h = cardH;
                        shadow.r = 0.02f;
                        shadow.g = 0.03f;
                        shadow.b = 0.05f;
                        shadow.a = 0.50f;
                        worldQuads.push_back(shadow);

                        IRenderBackend::DebugQuad cardBg;
                        cardBg.x = cardX;
                        cardBg.y = cardY;
                        cardBg.w = cardW;
                        cardBg.h = cardH;
                        cardBg.r = selected ? 0.18f : 0.10f;
                        cardBg.g = selected ? 0.18f : 0.11f;
                        cardBg.b = selected ? 0.16f : 0.13f;
                        cardBg.a = 0.95f;
                        worldQuads.push_back(cardBg);

                        const float border = std::clamp(cardW * 0.045f, 2.0f, 4.0f);
                        const auto addBorderQuad = [&](float x, float y, float w, float h, const glm::vec4& c) {
                            IRenderBackend::DebugQuad q;
                            q.x = x;
                            q.y = y;
                            q.w = w;
                            q.h = h;
                            q.r = c.r;
                            q.g = c.g;
                            q.b = c.b;
                            q.a = c.a;
                            worldQuads.push_back(q);
                        };
                        const glm::vec4 borderColor = selected
                            ? glm::vec4(0.95f, 0.78f, 0.33f, 0.98f)
                            : glm::vec4(0.58f, 0.66f, 0.78f, 0.92f);
                        addBorderQuad(cardX, cardY, cardW, border, borderColor);
                        addBorderQuad(cardX, cardY + cardH - border, cardW, border, borderColor);
                        addBorderQuad(cardX, cardY + border, border, std::max(0.0f, cardH - border * 2.0f), borderColor);
                        addBorderQuad(cardX + cardW - border, cardY + border, border, std::max(0.0f, cardH - border * 2.0f), borderColor);

                        const BackendItemAtlasIcon* itemIcon = findBackendItemAtlasIcon(entry.id);
                        if (itemIcon) {
                            const glm::vec2 uvMin = backendItemAtlasUvMin(itemIcon->row, itemIcon->col);
                            const glm::vec2 uvMax = backendItemAtlasUvMax(itemIcon->row, itemIcon->col);
                            IRenderBackend::DebugSprite sprite;
                            const float pad = std::clamp(cardW * 0.10f, 6.0f, 10.0f);
                            sprite.x = cardX + pad;
                            sprite.y = cardY + pad;
                            sprite.w = cardW - pad * 2.0f;
                            sprite.h = cardH - pad * 2.0f;
                            sprite.u0 = uvMin.x;
                            sprite.v0 = uvMin.y;
                            sprite.u1 = uvMax.x;
                            sprite.v1 = uvMax.y;
                            sprite.r = 1.0f;
                            sprite.g = 1.0f;
                            sprite.b = 1.0f;
                            sprite.a = selected ? 1.0f : 0.96f;
                            sprite.texturePath = "assets/images/items_atlas.png";
                            sprites.push_back(std::move(sprite));
                        } else {
                            appendText(cardX + 6.0f,
                                       cardY + cardH * 0.36f,
                                       runtime::hud::humanizeToken(entry.id),
                                       labelScale,
                                       glm::vec3(0.86f, 0.90f, 0.96f));
                        }

                        const std::string countText = "x" + std::to_string(std::max(0, entry.count));
                        const float countW =
                            std::max(1.0f, runtime::backend_text::measureTextWidth(countText, countScale));
                        appendText(cardX + std::max(0.0f, (cardW - countW) * 0.5f),
                                   cardY + cardH + 2.0f,
                                   countText,
                                   countScale,
                                   selected ? glm::vec3(0.99f, 0.90f, 0.56f)
                                            : glm::vec3(0.90f, 0.94f, 0.99f));

                        const std::string nameText = runtime::hud::humanizeToken(entry.id);
                        const float nameW =
                            std::max(1.0f, runtime::backend_text::measureTextWidth(nameText, labelScale));
                        appendText(cardX + std::max(0.0f, (cardW - nameW) * 0.5f),
                                   cardY + cardH + 2.0f + countH + 2.0f,
                                   nameText,
                                   labelScale,
                                   glm::vec3(0.76f, 0.84f, 0.92f));

                        runtime::backend_inventory_panel::HitRegion hit;
                        hit.action = runtime::backend_inventory_panel::HitAction::SelectItem;
                        hit.itemId = entry.id;
                        hit.x = cardX;
                        hit.y = cardY;
                        hit.w = cardW;
                        hit.h = rowPitch - std::max(2.0f, lineStep * 0.08f);
                        backendInventoryPanel.hitRegions.push_back(std::move(hit));
                    }

                    const float footerY = navY + static_cast<float>(inventoryModel.visibleEntries.size()) * rowPitch;
                    const std::string clearLine = runtime::backend_inventory::clearSelectionLabel();
                    appendText(panelX,
                               footerY + 1.0f,
                               clearLine,
                               0.90f,
                               selectedItem.empty()
                                   ? glm::vec3(0.62f, 0.68f, 0.76f)
                                   : glm::vec3(0.95f, 0.78f, 0.66f));
                    runtime::backend_inventory_panel::HitRegion clearHit;
                    clearHit.action = runtime::backend_inventory_panel::HitAction::ClearSelection;
                    clearHit.itemId.clear();
                    clearHit.x = panelX;
                    clearHit.y = footerY + 1.0f;
                    clearHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(clearLine, 0.90f));
                    clearHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(clearLine, 0.90f));
                    backendInventoryPanel.hitRegions.push_back(std::move(clearHit));

                    appendText(panelX,
                               footerY + lineStep,
                               "[1-9] select   Wheel/Arrows page",
                               0.80f,
                               glm::vec3(0.66f, 0.76f, 0.90f));
                } else {
                float invY = invStartY;
                appendText(leftX,
                           invY,
                           runtime::backend_inventory::makeTitleLabel(inventoryModel),
                           std::clamp(1.0f * uiScale, 0.80f, 1.30f),
                           glm::vec3(0.92f, 0.95f, 0.99f));
                invY += lineStep;

                const bool hasPrev = runtime::backend_inventory::canScrollPrev(inventoryModel);
                const bool hasNext = runtime::backend_inventory::canScrollNext(inventoryModel);
                if (hasPrev || hasNext) {
                    constexpr float kNavScale = 0.84f;
                    const std::string prevLabel = runtime::backend_inventory::prevPageLabel();
                    const std::string nextLabel = runtime::backend_inventory::nextPageLabel();
                    appendText(leftX,
                               invY,
                               prevLabel,
                               kNavScale,
                               hasPrev ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasPrev) {
                        runtime::backend_inventory_panel::HitRegion prevHit;
                        prevHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        prevHit.offsetDelta = -1;
                        prevHit.x = leftX;
                        prevHit.y = invY;
                        prevHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale));
                        prevHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(prevLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(prevHit));
                    }

                    const float nextX = leftX + std::max(1.0f, runtime::backend_text::measureTextWidth(prevLabel, kNavScale)) + std::max(10.0f, lineStep * 0.75f);
                    appendText(nextX,
                               invY,
                               nextLabel,
                               kNavScale,
                               hasNext ? glm::vec3(0.75f, 0.87f, 0.96f)
                                       : glm::vec3(0.42f, 0.48f, 0.55f));
                    if (hasNext) {
                        runtime::backend_inventory_panel::HitRegion nextHit;
                        nextHit.action = runtime::backend_inventory_panel::HitAction::ScrollOffset;
                        nextHit.offsetDelta = 1;
                        nextHit.x = nextX;
                        nextHit.y = invY;
                        nextHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(nextLabel, kNavScale));
                        nextHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(nextLabel, kNavScale));
                        backendInventoryPanel.hitRegions.push_back(std::move(nextHit));
                    }
                    invY += lineStep * 0.88f;
                }

                for (const auto& row : inventoryModel.rows) {
                    constexpr float kItemScale = 0.95f;
                    appendText(leftX,
                               invY,
                               row.line,
                               kItemScale,
                               row.selected
                                   ? glm::vec3(0.98f, 0.90f, 0.58f)
                                   : glm::vec3(0.84f, 0.90f, 0.97f));
                    runtime::backend_inventory_panel::HitRegion hit;
                    hit.action = runtime::backend_inventory_panel::HitAction::SelectItem;
                    hit.itemId = row.itemId;
                    hit.x = leftX;
                    hit.y = invY;
                    hit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(row.line, kItemScale));
                    hit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(row.line, kItemScale));
                    backendInventoryPanel.hitRegions.push_back(std::move(hit));
                    invY += lineStep * 0.93f;
                }

                const std::string clearLine = runtime::backend_inventory::clearSelectionLabel();
                appendText(leftX,
                           invY + 1.0f,
                           clearLine,
                           0.90f,
                           selectedItem.empty()
                               ? glm::vec3(0.62f, 0.68f, 0.76f)
                               : glm::vec3(0.95f, 0.78f, 0.66f));
                runtime::backend_inventory_panel::HitRegion clearHit;
                clearHit.action = runtime::backend_inventory_panel::HitAction::ClearSelection;
                clearHit.itemId.clear();
                clearHit.x = leftX;
                clearHit.y = invY + 1.0f;
                clearHit.w = std::max(1.0f, runtime::backend_text::measureTextWidth(clearLine, 0.90f));
                clearHit.h = std::max(1.0f, runtime::backend_text::measureTextHeight(clearLine, 0.90f));
                backendInventoryPanel.hitRegions.push_back(std::move(clearHit));
                invY += lineStep;
                appendText(leftX,
                           invY + 2.0f,
                           runtime::backend_inventory::hintLabel(),
                           0.82f,
                           glm::vec3(0.66f, 0.76f, 0.90f));
                }
            }

            auto typeCounts = gameWorld->getPlayerTypeLineCounts();
            if (!typeCounts.empty()) {
                std::sort(typeCounts.begin(), typeCounts.end(),
                          [](const GameWorld::TypeLineCount& a, const GameWorld::TypeLineCount& b) {
                              if (a.uniqueLineCount != b.uniqueLineCount) {
                                  return a.uniqueLineCount > b.uniqueLineCount;
                              }
                              return a.type < b.type;
                          });

                float typeY = edgePad + lineStep * 6.6f;
                appendText(edgePad, typeY, "Type Lines", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                typeY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(6, typeCounts.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendText(edgePad,
                               typeY,
                               runtime::hud::formatTypeLineEntry(typeCounts[i].type, typeCounts[i].uniqueLineCount),
                               0.95f,
                               glm::vec3(0.92f, 0.94f, 0.98f));
                    typeY += lineStep * 0.93f;
                }
            }

            const auto& benchUnits = gameWorld->getBenchPokemons();
            if (!benchUnits.empty()) {
                float benchY = edgePad + lineStep * 13.6f;
                appendText(edgePad, benchY, "Bench", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.86f, 0.94f, 0.98f));
                benchY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(5, benchUnits.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendText(edgePad,
                               benchY,
                               runtime::hud::formatUnitEntry(benchUnits[i].name, benchUnits[i].level),
                               0.95f,
                               glm::vec3(0.80f, 0.88f, 0.96f));
                    benchY += lineStep * 0.93f;
                }
            }

            const auto& shopCards = gameWorld->getClassicShopCards();
            if (!shopCards.empty()) {
                float shopY = edgePad + lineStep * 13.6f;
                appendRightText(shopY, "Shop Offers", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                shopY += lineStep;
                const std::size_t maxRows = std::min<std::size_t>(5, shopCards.size());
                for (std::size_t i = 0; i < maxRows; ++i) {
                    appendRightText(shopY,
                                    runtime::hud::formatShopCardEntry(shopCards[i].name,
                                                                      shopCards[i].level,
                                                                      shopCards[i].cost),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    shopY += lineStep * 0.93f;
                }
            }
        }

        const auto recentMain = log.recentMainLines(7);
        if (!recentMain.empty()) {
            float y = std::max(edgePad + lineStep * 7.0f, static_cast<float>(drawableH) - lineStep * 11.0f);
            for (const auto& line : recentMain) {
                const std::string text = trimDebugLine(line.text, 84);
                const float scale = 1.0f;
                const float textW = std::max(1.0f, runtime::backend_text::measureTextWidth(text, scale));
                const float x = std::max(edgePad, static_cast<float>(drawableW) - textW - edgePad);
                appendText(x,
                           y,
                           text,
                           scale,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += lineStep;
            }
        }

        const bool classicMode = (mode == "classic");
        const auto sideLines = classicMode ? log.recentEconomyLines(5) : log.recentCatchLines(5);
        if (!sideLines.empty()) {
            float y = std::max(edgePad + lineStep * 7.0f, static_cast<float>(drawableH) - lineStep * 11.0f);
            for (const auto& line : sideLines) {
                const std::string text = trimDebugLine(line.text, 54);
                const float scale = 1.0f;
                appendText(edgePad,
                           y,
                           text,
                           scale,
                           glm::vec3(
                               std::clamp(line.color.r, 0.0f, 1.0f),
                               std::clamp(line.color.g, 0.0f, 1.0f),
                               std::clamp(line.color.b, 0.0f, 1.0f)));
                y += lineStep;
            }
        }

        if (!worldBackgroundQuads.empty()) {
            renderer->drawDebugQuads(worldBackgroundQuads.data(), worldBackgroundQuads.size(), drawableW, drawableH);
        }
        if (!world3DTriangles.empty() && hasWorldViewProj && supportsWorldTriangles3D) {
            renderer->drawWorldTriangles(
                world3DTriangles.data(),
                world3DTriangles.size(),
                worldViewProj,
                drawableW,
                drawableH);
        }
        if (!worldIndexedBatches.empty() && hasWorldViewProj && supportsWorldIndexedMeshes) {
            runtime::shared_world_batches::submitWorldIndexedBatches(
                *renderer, worldIndexedBatches, worldViewProj, drawableW, drawableH);
        }
        if (renderWorld && hasWorldViewProj && supportsWorldIndexedMeshes &&
            renderer && renderer->backendId() &&
            toLowerCopy(renderer->backendId()) == "opengl" &&
            gameWorld && camera && engineServices && engineServices->resources) {
            (void)runtime::shared_capture::drawOpenGlSharedCapturePokeballModels(
                gameWorld,
                engineServices->resources,
                camera);
        }
        if (!worldTriangles.empty()) {
            renderer->drawDebugTriangles(worldTriangles.data(), worldTriangles.size(), drawableW, drawableH);
        }
        if (!worldQuads.empty()) {
            renderer->drawDebugQuads(worldQuads.data(), worldQuads.size(), drawableW, drawableH);
        }
        if (!sprites.empty()) {
            renderer->drawDebugSprites(sprites.data(), sprites.size(), drawableW, drawableH);
        }
        if (!lines.empty()) {
            renderer->drawDebugLines(lines.data(), lines.size(), drawableW, drawableH);
        }
        if (!overlayQuads.empty()) {
            renderer->drawDebugQuads(overlayQuads.data(), overlayQuads.size(), drawableW, drawableH);
        }
        if (!textLines.empty()) {
            renderer->drawDebugLines(textLines.data(), textLines.size(), drawableW, drawableH);
        }
}

} // namespace game::runtime::shared_backend_debug_view
