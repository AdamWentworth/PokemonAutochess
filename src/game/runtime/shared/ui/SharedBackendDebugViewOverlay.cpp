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
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
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

using OverlayHash = std::uint64_t;

constexpr OverlayHash kOverlayHashOffset = 1469598103934665603ull;
constexpr OverlayHash kOverlayHashPrime = 1099511628211ull;

void hashBytes(OverlayHash& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<OverlayHash>(bytes[i]);
        hash *= kOverlayHashPrime;
    }
}

void hashInt(OverlayHash& hash, int value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashSize(OverlayHash& hash, std::size_t value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashBool(OverlayHash& hash, bool value) {
    const unsigned char byte = value ? 1u : 0u;
    hashBytes(hash, &byte, sizeof(byte));
}

void hashFloatQuantized(OverlayHash& hash, float value, float scale = 1000.0f) {
    const int quantized = static_cast<int>(std::lround(static_cast<double>(value) * scale));
    hashInt(hash, quantized);
}

void hashString(OverlayHash& hash, const std::string& value) {
    hashSize(hash, value.size());
    if (!value.empty()) {
        hashBytes(hash, value.data(), value.size());
    }
}

void hashVec3(OverlayHash& hash, const glm::vec3& value) {
    hashFloatQuantized(hash, value.r);
    hashFloatQuantized(hash, value.g);
    hashFloatQuantized(hash, value.b);
}

template <typename T>
void appendCachedVector(std::vector<T>& dst, const std::vector<T>& src) {
    if (src.empty()) return;
    dst.insert(dst.end(), src.begin(), src.end());
}

struct RetainedOverlayCache {
    OverlayHash key = 0;
    std::vector<IRenderBackend::DebugQuad> worldQuads;
    std::vector<IRenderBackend::DebugQuad> overlayQuads;
    std::vector<IRenderBackend::DebugLine> lines;
    std::vector<IRenderBackend::DebugLine> textLines;
    std::vector<IRenderBackend::DebugSprite> sprites;
    std::vector<game::runtime::backend_inventory_panel::HitRegion> hitRegions;
};
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
    EngineRenderBuildBreakdown localRenderBreakdown{};
    EngineRenderBuildBreakdown* renderBuildBreakdown =
        args.renderBuildBreakdown ? args.renderBuildBreakdown : &localRenderBreakdown;
    const float precomposedWorldMs = renderBuildBreakdown->worldComposeMs;
    const float precomposedWorldBackdropMs = renderBuildBreakdown->worldBackdropMs;
    const float precomposedWorldVfxMs = renderBuildBreakdown->worldVfxMs;
    const float precomposedWorldDepthFlushMs = renderBuildBreakdown->worldDepthFlushMs;
    *renderBuildBreakdown = {};
    renderBuildBreakdown->worldComposeMs = precomposedWorldMs;
    renderBuildBreakdown->worldBackdropMs = precomposedWorldBackdropMs;
    renderBuildBreakdown->worldVfxMs = precomposedWorldVfxMs;
    renderBuildBreakdown->worldDepthFlushMs = precomposedWorldDepthFlushMs;
    using clock = std::chrono::steady_clock;
    const auto toMs = [](const clock::time_point& start, const clock::time_point& end) {
        return static_cast<float>(
            std::chrono::duration<double, std::milli>(end - start).count());
    };
    const auto composeStart = clock::now();
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
                              << ", model " << perf.projectedModelMs;
                    if (perf.projectedModelPrepMs > 0.0f ||
                        perf.projectedModelGeometryMs > 0.0f) {
                        buildLine << " [prep " << perf.projectedModelPrepMs
                                  << ", geom " << perf.projectedModelGeometryMs << "]";
                    }
                    buildLine
                              << ", overlay " << perf.projectedOverlayMs << ")";
                    if (perf.projectedUnitsProcessed > 0u) {
                        buildLine << " | Clip " << perf.projectedClipSkinnedUnits
                                  << "/" << perf.projectedUnitsProcessed;
                    }
                }
                appendRightText(edgePad + lineStep * 0.55f,
                                buildLine.str(),
                                std::clamp(0.82f * uiScale, 0.68f, 1.05f),
                                glm::vec3(0.72f, 0.86f, 0.96f));
            }
        }

        const std::string cachedMode = (services ? services->gameMode : std::string("classic"));
        RoundPhase cachedRoundPhase = RoundPhase::Planning;
        bool cachedCombatActive = false;
        if (ecsWorld.alive(roundPhaseEntity)) {
            if (const auto* roundState = ecsWorld.get<game::RoundState>(roundPhaseEntity)) {
                cachedRoundPhase = roundState->phase;
            }
            if (const auto* combatState = ecsWorld.get<game::CombatActive>(roundPhaseEntity)) {
                cachedCombatActive = combatState->active;
            }
        }

        int cachedPlayerAlive = 0;
        int cachedEnemyAlive = 0;
        if (gameWorld) {
            for (const auto& unit : gameWorld->getPokemons()) {
                if (!unit.alive && !unit.captureInProgress) continue;
                if (unit.side == PokemonSide::Player) ++cachedPlayerAlive;
                else ++cachedEnemyAlive;
            }
        }

        const int cachedMoney = gameWorld ? gameWorld->getMoney() : 0;
        const std::string cachedSelectedItem = gameWorld ? gameWorld->getSelectedItem() : std::string();
        if (refreshBackendInventoryFromWorld) {
            refreshBackendInventoryFromWorld();
        }
        const auto& cachedInventoryModel = backendInventoryPanel.model;
        const bool cachedAdventureInventoryIcons = (cachedMode == "adventure");
        auto cachedTypeCounts = gameWorld ? gameWorld->getPlayerTypeLineCounts()
                                          : std::vector<GameWorld::TypeLineCount>{};
        if (!cachedTypeCounts.empty()) {
            std::sort(cachedTypeCounts.begin(), cachedTypeCounts.end(),
                      [](const GameWorld::TypeLineCount& a, const GameWorld::TypeLineCount& b) {
                          if (a.uniqueLineCount != b.uniqueLineCount) {
                              return a.uniqueLineCount > b.uniqueLineCount;
                          }
                          return a.type < b.type;
                      });
        }
        const auto* cachedBenchUnits = gameWorld ? &gameWorld->getBenchPokemons() : nullptr;
        const auto* cachedShopCards = gameWorld ? &gameWorld->getClassicShopCards() : nullptr;
        const auto cachedRecentMain = log.recentMainLines(7);
        const bool cachedClassicMode = (cachedMode == "classic");
        const auto cachedSideLogLines =
            cachedClassicMode ? log.recentEconomyLines(5) : log.recentCatchLines(5);

        const std::string cachedBackend =
            services ? services->activeRendererBackend : std::string();
        const std::string cachedGpuRenderer =
            services ? services->gpuRenderer : std::string();

        const auto hashLayoutKeyBase = [&](OverlayHash& key) {
            hashInt(key, drawableW);
            hashInt(key, drawableH);
            hashFloatQuantized(key, edgePad);
            hashFloatQuantized(key, lineStep);
            hashFloatQuantized(key, uiScale);
        };
        const auto appendRetainedRegion = [&](const RetainedOverlayCache& cache) {
            appendCachedVector(worldQuads, cache.worldQuads);
            appendCachedVector(overlayQuads, cache.overlayQuads);
            appendCachedVector(lines, cache.lines);
            appendCachedVector(textLines, cache.textLines);
            appendCachedVector(sprites, cache.sprites);
            appendCachedVector(backendInventoryPanel.hitRegions, cache.hitRegions);
        };
        const auto captureRetainedRegion =
            [&](RetainedOverlayCache& cache,
                OverlayHash key,
                std::size_t worldQuadsStart,
                std::size_t overlayQuadsStart,
                std::size_t linesStart,
                std::size_t textLinesStart,
                std::size_t spritesStart,
                std::size_t hitRegionsStart) {
                cache.key = key;
                cache.worldQuads.assign(
                    worldQuads.begin() + static_cast<std::ptrdiff_t>(worldQuadsStart),
                    worldQuads.end());
                cache.overlayQuads.assign(
                    overlayQuads.begin() + static_cast<std::ptrdiff_t>(overlayQuadsStart),
                    overlayQuads.end());
                cache.lines.assign(
                    lines.begin() + static_cast<std::ptrdiff_t>(linesStart),
                    lines.end());
                cache.textLines.assign(
                    textLines.begin() + static_cast<std::ptrdiff_t>(textLinesStart),
                    textLines.end());
                cache.sprites.assign(
                    sprites.begin() + static_cast<std::ptrdiff_t>(spritesStart),
                    sprites.end());
                cache.hitRegions.assign(
                    backendInventoryPanel.hitRegions.begin() +
                        static_cast<std::ptrdiff_t>(hitRegionsStart),
                    backendInventoryPanel.hitRegions.end());
            };

        OverlayHash statusKey = kOverlayHashOffset;
        hashLayoutKeyBase(statusKey);
        hashString(statusKey, cachedMode);
        hashString(statusKey, cachedBackend);
        hashString(statusKey, cachedGpuRenderer);
        hashInt(statusKey, static_cast<int>(cachedRoundPhase));
        hashBool(statusKey, cachedCombatActive);
        hashInt(statusKey, cachedPlayerAlive);
        hashInt(statusKey, cachedEnemyAlive);
        hashInt(statusKey, cachedMoney);
        hashString(statusKey, cachedSelectedItem);

        OverlayHash inventoryKey = kOverlayHashOffset;
        hashLayoutKeyBase(inventoryKey);
        hashString(inventoryKey, cachedMode);
        hashBool(inventoryKey, cachedAdventureInventoryIcons);
        hashInt(inventoryKey, cachedInventoryModel.offset);
        hashSize(inventoryKey, cachedInventoryModel.totalCount);
        hashString(inventoryKey, cachedSelectedItem);
        hashSize(inventoryKey, cachedInventoryModel.visibleEntries.size());
        for (const auto& entry : cachedInventoryModel.visibleEntries) {
            hashString(inventoryKey, entry.id);
            hashInt(inventoryKey, entry.count);
        }
        hashSize(inventoryKey, cachedInventoryModel.rows.size());
        for (const auto& row : cachedInventoryModel.rows) {
            hashString(inventoryKey, row.itemId);
            hashString(inventoryKey, row.line);
            hashBool(inventoryKey, row.selected);
        }

        OverlayHash rosterKey = kOverlayHashOffset;
        hashLayoutKeyBase(rosterKey);
        hashString(rosterKey, cachedMode);
        const std::size_t cachedTypeRows = std::min<std::size_t>(6u, cachedTypeCounts.size());
        hashSize(rosterKey, cachedTypeRows);
        for (std::size_t i = 0; i < cachedTypeRows; ++i) {
            hashString(rosterKey, cachedTypeCounts[i].type);
            hashInt(rosterKey, cachedTypeCounts[i].uniqueLineCount);
        }
        const std::size_t cachedBenchRows =
            (cachedBenchUnits != nullptr) ? std::min<std::size_t>(5u, cachedBenchUnits->size()) : 0u;
        hashSize(rosterKey, cachedBenchRows);
        for (std::size_t i = 0; i < cachedBenchRows; ++i) {
            hashString(rosterKey, (*cachedBenchUnits)[i].name);
            hashInt(rosterKey, (*cachedBenchUnits)[i].level);
        }
        const std::size_t cachedShopRows =
            (cachedShopCards != nullptr) ? std::min<std::size_t>(5u, cachedShopCards->size()) : 0u;
        hashSize(rosterKey, cachedShopRows);
        for (std::size_t i = 0; i < cachedShopRows; ++i) {
            hashString(rosterKey, (*cachedShopCards)[i].name);
            hashInt(rosterKey, (*cachedShopCards)[i].level);
            hashInt(rosterKey, (*cachedShopCards)[i].cost);
        }

        OverlayHash logKey = kOverlayHashOffset;
        hashLayoutKeyBase(logKey);
        hashBool(logKey, cachedClassicMode);
        hashSize(logKey, cachedRecentMain.size());
        for (const auto& line : cachedRecentMain) {
            hashString(logKey, trimDebugLine(line.text, 84));
            hashVec3(logKey, line.color);
        }
        hashSize(logKey, cachedSideLogLines.size());
        for (const auto& line : cachedSideLogLines) {
            hashString(logKey, trimDebugLine(line.text, 54));
            hashVec3(logKey, line.color);
        }

        thread_local RetainedOverlayCache statusCache;
        thread_local RetainedOverlayCache inventoryCache;
        thread_local RetainedOverlayCache rosterCache;
        thread_local RetainedOverlayCache logCache;

        if (statusCache.key == statusKey) {
            appendRetainedRegion(statusCache);
        } else {
            const std::size_t worldQuadsStart = worldQuads.size();
            const std::size_t overlayQuadsStart = overlayQuads.size();
            const std::size_t linesStart = lines.size();
            const std::size_t textLinesStart = textLines.size();
            const std::size_t spritesStart = sprites.size();
            const std::size_t hitRegionsStart = backendInventoryPanel.hitRegions.size();

            appendRightText(edgePad + lineStep * 1.1f,
                            runtime::backend_status_text::modeLine(cachedMode),
                            std::clamp(1.2f * uiScale, 0.95f, 1.7f),
                            glm::vec3(0.93f, 0.95f, 0.99f));
            if (services) {
                appendRightText(edgePad + lineStep * 2.2f,
                                runtime::backend_status_text::backendLine(
                                    cachedBackend,
                                    cachedGpuRenderer),
                                std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                                glm::vec3(0.68f, 0.80f, 0.94f));
            }
            appendRightText(edgePad + lineStep * 3.3f,
                            runtime::backend_status_text::roundLine(
                                cachedRoundPhase,
                                cachedCombatActive),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.83f, 0.91f, 0.98f));
            appendRightText(edgePad + lineStep * 4.4f,
                            runtime::backend_status_text::unitsLine(
                                cachedPlayerAlive,
                                cachedEnemyAlive),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.72f, 0.90f, 0.84f));
            appendRightText(edgePad + lineStep * 5.5f,
                            runtime::backend_status_text::goldLine(cachedMoney),
                            std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                            glm::vec3(0.96f, 0.88f, 0.56f));
            if (!cachedSelectedItem.empty()) {
                appendRightText(edgePad + lineStep * 6.6f,
                                runtime::backend_status_text::selectedItemLine(cachedSelectedItem),
                                std::clamp(1.0f * uiScale, 0.80f, 1.35f),
                                glm::vec3(0.84f, 0.90f, 0.98f));
            }

            captureRetainedRegion(
                statusCache,
                statusKey,
                worldQuadsStart,
                overlayQuadsStart,
                linesStart,
                textLinesStart,
                spritesStart,
                hitRegionsStart);
        }

        if (inventoryCache.key == inventoryKey) {
            appendRetainedRegion(inventoryCache);
        } else {
            const std::size_t worldQuadsStart = worldQuads.size();
            const std::size_t overlayQuadsStart = overlayQuads.size();
            const std::size_t linesStart = lines.size();
            const std::size_t textLinesStart = textLines.size();
            const std::size_t spritesStart = sprites.size();
            const std::size_t hitRegionsStart = backendInventoryPanel.hitRegions.size();

            const auto& inventoryModel = cachedInventoryModel;
            const float leftX = edgePad;
            const float invStartY = edgePad + lineStep * 7.7f;
            const bool adventureModeInventoryIcons = cachedAdventureInventoryIcons;
            const std::string& selectedItem = cachedSelectedItem;

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

            captureRetainedRegion(
                inventoryCache,
                inventoryKey,
                worldQuadsStart,
                overlayQuadsStart,
                linesStart,
                textLinesStart,
                spritesStart,
                hitRegionsStart);
        }

        if (rosterCache.key == rosterKey) {
            appendRetainedRegion(rosterCache);
        } else {
            const std::size_t worldQuadsStart = worldQuads.size();
            const std::size_t overlayQuadsStart = overlayQuads.size();
            const std::size_t linesStart = lines.size();
            const std::size_t textLinesStart = textLines.size();
            const std::size_t spritesStart = sprites.size();
            const std::size_t hitRegionsStart = backendInventoryPanel.hitRegions.size();

            if (!cachedTypeCounts.empty()) {
                float typeY = edgePad + lineStep * 6.6f;
                appendText(edgePad, typeY, "Type Lines", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                typeY += lineStep;
                for (std::size_t i = 0; i < cachedTypeRows; ++i) {
                    appendText(edgePad,
                               typeY,
                               runtime::hud::formatTypeLineEntry(
                                   cachedTypeCounts[i].type,
                                   cachedTypeCounts[i].uniqueLineCount),
                               0.95f,
                               glm::vec3(0.92f, 0.94f, 0.98f));
                    typeY += lineStep * 0.93f;
                }
            }

            if (cachedBenchUnits != nullptr && !cachedBenchUnits->empty()) {
                float benchY = edgePad + lineStep * 13.6f;
                appendText(edgePad, benchY, "Bench", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.86f, 0.94f, 0.98f));
                benchY += lineStep;
                for (std::size_t i = 0; i < cachedBenchRows; ++i) {
                    appendText(edgePad,
                               benchY,
                               runtime::hud::formatUnitEntry(
                                   (*cachedBenchUnits)[i].name,
                                   (*cachedBenchUnits)[i].level),
                               0.95f,
                               glm::vec3(0.80f, 0.88f, 0.96f));
                    benchY += lineStep * 0.93f;
                }
            }

            if (cachedShopCards != nullptr && !cachedShopCards->empty()) {
                float shopY = edgePad + lineStep * 13.6f;
                appendRightText(shopY, "Shop Offers", std::clamp(1.0f * uiScale, 0.80f, 1.30f), glm::vec3(0.98f, 0.90f, 0.60f));
                shopY += lineStep;
                for (std::size_t i = 0; i < cachedShopRows; ++i) {
                    appendRightText(shopY,
                                    runtime::hud::formatShopCardEntry(
                                        (*cachedShopCards)[i].name,
                                        (*cachedShopCards)[i].level,
                                        (*cachedShopCards)[i].cost),
                                    0.95f,
                                    glm::vec3(0.92f, 0.94f, 0.98f));
                    shopY += lineStep * 0.93f;
                }
            }

            captureRetainedRegion(
                rosterCache,
                rosterKey,
                worldQuadsStart,
                overlayQuadsStart,
                linesStart,
                textLinesStart,
                spritesStart,
                hitRegionsStart);
        }

        if (logCache.key == logKey) {
            appendRetainedRegion(logCache);
        } else {
            const std::size_t worldQuadsStart = worldQuads.size();
            const std::size_t overlayQuadsStart = overlayQuads.size();
            const std::size_t linesStart = lines.size();
            const std::size_t textLinesStart = textLines.size();
            const std::size_t spritesStart = sprites.size();
            const std::size_t hitRegionsStart = backendInventoryPanel.hitRegions.size();

            if (!cachedRecentMain.empty()) {
                float y = std::max(
                    edgePad + lineStep * 7.0f,
                    static_cast<float>(drawableH) - lineStep * 11.0f);
                for (const auto& line : cachedRecentMain) {
                    const std::string text = trimDebugLine(line.text, 84);
                    const float scale = 1.0f;
                    const float textW = std::max(
                        1.0f,
                        runtime::backend_text::measureTextWidth(text, scale));
                    const float x = std::max(
                        edgePad,
                        static_cast<float>(drawableW) - textW - edgePad);
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

            if (!cachedSideLogLines.empty()) {
                float y = std::max(
                    edgePad + lineStep * 7.0f,
                    static_cast<float>(drawableH) - lineStep * 11.0f);
                for (const auto& line : cachedSideLogLines) {
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

            captureRetainedRegion(
                logCache,
                logKey,
                worldQuadsStart,
                overlayQuadsStart,
                linesStart,
                textLinesStart,
                spritesStart,
                hitRegionsStart);
        }

        const auto submitStart = clock::now();
        renderBuildBreakdown->overlayPrepMs = toMs(composeStart, submitStart);

        if (!worldBackgroundQuads.empty()) {
            const auto stageStart = clock::now();
            renderer->drawDebugQuads(worldBackgroundQuads.data(), worldBackgroundQuads.size(), drawableW, drawableH);
            renderBuildBreakdown->worldBackgroundMs += toMs(stageStart, clock::now());
        }
        if (!world3DTriangles.empty() && hasWorldViewProj && supportsWorldTriangles3D) {
            const auto stageStart = clock::now();
            renderer->drawWorldTriangles(
                world3DTriangles.data(),
                world3DTriangles.size(),
                worldViewProj,
                drawableW,
                drawableH);
            renderBuildBreakdown->worldTriangles3dMs += toMs(stageStart, clock::now());
        }
        if (!worldIndexedBatches.empty() && hasWorldViewProj && supportsWorldIndexedMeshes) {
            float cameraWorldPos3[3] = {0.0f, 7.0f, 9.0f};
            float cameraForward3[3] = {0.0f, -0.6139406f, -0.7893522f};
            float cameraTarget3[3] = {0.0f, -1.0f, 0.0f};
            if (camera) {
                const glm::vec3 camPos = camera->getPosition();
                const glm::vec3 camForward = camera->getDirection();
                const glm::vec3 camTarget = camera->getTarget();
                cameraWorldPos3[0] = camPos.x;
                cameraWorldPos3[1] = camPos.y;
                cameraWorldPos3[2] = camPos.z;
                cameraForward3[0] = camForward.x;
                cameraForward3[1] = camForward.y;
                cameraForward3[2] = camForward.z;
                cameraTarget3[0] = camTarget.x;
                cameraTarget3[1] = camTarget.y;
                cameraTarget3[2] = camTarget.z;
            }
            const auto stageStart = clock::now();
            runtime::shared_world_batches::submitWorldIndexedBatches(
                *renderer,
                worldIndexedBatches,
                worldViewProj,
                drawableW,
                drawableH,
                cameraWorldPos3,
                cameraForward3,
                cameraTarget3);
            renderBuildBreakdown->worldIndexedMs += toMs(stageStart, clock::now());
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
            const auto stageStart = clock::now();
            renderer->drawDebugTriangles(worldTriangles.data(), worldTriangles.size(), drawableW, drawableH);
            renderBuildBreakdown->worldDebugMs += toMs(stageStart, clock::now());
        }
        if (!worldQuads.empty()) {
            const auto stageStart = clock::now();
            renderer->drawDebugQuads(worldQuads.data(), worldQuads.size(), drawableW, drawableH);
            renderBuildBreakdown->worldDebugMs += toMs(stageStart, clock::now());
        }
        if (!sprites.empty()) {
            const auto stageStart = clock::now();
            renderer->drawDebugSprites(sprites.data(), sprites.size(), drawableW, drawableH);
            renderBuildBreakdown->spriteMs += toMs(stageStart, clock::now());
        }
        if (!lines.empty()) {
            const auto stageStart = clock::now();
            renderer->drawDebugLines(lines.data(), lines.size(), drawableW, drawableH);
            renderBuildBreakdown->uiMs += toMs(stageStart, clock::now());
        }
        if (!overlayQuads.empty()) {
            const auto stageStart = clock::now();
            renderer->drawDebugQuads(overlayQuads.data(), overlayQuads.size(), drawableW, drawableH);
            renderBuildBreakdown->uiMs += toMs(stageStart, clock::now());
        }
        if (!textLines.empty()) {
            const auto stageStart = clock::now();
            renderer->drawDebugLines(textLines.data(), textLines.size(), drawableW, drawableH);
            renderBuildBreakdown->uiMs += toMs(stageStart, clock::now());
        }
}

} // namespace game::runtime::shared_backend_debug_view
