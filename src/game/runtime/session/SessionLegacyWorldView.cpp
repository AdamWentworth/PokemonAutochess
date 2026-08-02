#include "game/runtime/session/SessionLegacyWorldView.h"

#include "game/runtime/render_prep/UnitVisuals.h"
#include "game/runtime/render_prep/WorldProjection.h"
#include "game/world/GameWorld.h"

#include <algorithm>
#include <utility>

namespace game::runtime::session_legacy_world_view {

namespace {

void appendBoardBackdrop(const Args& args,
                         session_render_scratch::RenderScratch& scratch) {
    IRenderBackend::DebugQuad boardBg;
    boardBg.x = args.boardX;
    boardBg.y = args.boardY;
    boardBg.w = args.boardW;
    boardBg.h = args.boardH;
    boardBg.r = args.renderWorld ? 0.08f : 0.07f;
    boardBg.g = args.renderWorld ? 0.09f : 0.08f;
    boardBg.b = args.renderWorld ? 0.10f : 0.09f;
    boardBg.a = args.renderWorld ? 1.0f : 0.90f;
    scratch.worldBackgroundQuads.push_back(boardBg);

    for (int r = 0; r < args.rows; ++r) {
        for (int c = 0; c < args.cols; ++c) {
            IRenderBackend::DebugQuad cell;
            cell.x = args.boardX + args.cellW * static_cast<float>(c);
            cell.y = args.boardY + args.cellH * static_cast<float>(r);
            cell.w = args.cellW;
            cell.h = args.cellH;
            const bool darkCell = ((r + c) % 2) == 0;
            if (darkCell) {
                cell.r = args.renderWorld ? 0.08f : 0.07f;
                cell.g = args.renderWorld ? 0.09f : 0.08f;
                cell.b = args.renderWorld ? 0.10f : 0.09f;
                cell.a = args.renderWorld ? 0.24f : 0.20f;
            } else {
                cell.r = args.renderWorld ? 0.11f : 0.09f;
                cell.g = args.renderWorld ? 0.12f : 0.10f;
                cell.b = args.renderWorld ? 0.13f : 0.11f;
                cell.a = args.renderWorld ? 0.18f : 0.14f;
            }
            scratch.worldBackgroundQuads.push_back(cell);
        }
    }
}

float boardLineThickness(const Args& args) {
    return std::max(1.0f, args.minDim * 0.002f);
}

void appendBoardGridLines(const Args& args,
                          float line,
                          session_render_scratch::RenderScratch& scratch) {
    for (int c = 0; c <= args.cols; ++c) {
        IRenderBackend::DebugLine vLine;
        vLine.x1 = args.boardX + args.cellW * static_cast<float>(c);
        vLine.y1 = args.boardY;
        vLine.x2 = vLine.x1;
        vLine.y2 = args.boardY + args.boardH;
        vLine.thickness = line;
        vLine.r = args.renderWorld ? 0.74f : 0.62f;
        vLine.g = args.renderWorld ? 0.75f : 0.63f;
        vLine.b = args.renderWorld ? 0.77f : 0.65f;
        vLine.a = 1.0f;
        scratch.lines.push_back(vLine);
    }
    for (int r = 0; r <= args.rows; ++r) {
        IRenderBackend::DebugLine hLine;
        hLine.x1 = args.boardX;
        hLine.y1 = args.boardY + args.cellH * static_cast<float>(r);
        hLine.x2 = args.boardX + args.boardW;
        hLine.y2 = hLine.y1;
        hLine.thickness = line;
        hLine.r = args.renderWorld ? 0.74f : 0.62f;
        hLine.g = args.renderWorld ? 0.75f : 0.63f;
        hLine.b = args.renderWorld ? 0.77f : 0.65f;
        hLine.a = 1.0f;
        scratch.lines.push_back(hLine);
    }
}

void appendBoardUnits(const Args& args,
                      float hudCellPx,
                      float worldCellSize,
                      session_render_scratch::RenderScratch& scratch,
                      Result& result) {
    for (const auto& unit : args.gameWorld->getPokemons()) {
        if (!unit.alive && !unit.captureInProgress && !unit.fainting) continue;
        const auto uv = render_prep_projection::worldToBoardUv(
            unit.position.x,
            unit.position.z,
            args.cols,
            args.rows,
            worldCellSize);
        if (uv.first < 0.0f || uv.first > 1.0f || uv.second < 0.0f || uv.second > 1.0f) continue;
        ++result.visibleAnimatedUnits;

        IRenderBackend::DebugQuad tint;
        const float centerX = args.boardX + uv.first * args.boardW;
        const float centerY = args.boardY + uv.second * args.boardH;
        tint.w = args.cellW * 0.60f;
        tint.h = args.cellH * 0.60f;
        tint.x = centerX - tint.w * 0.5f;
        tint.y = centerY - tint.h * 0.5f;
        render_prep_units::applyWorldUnitTint(tint, unit);

        const std::string unitImagePath =
            render_prep_units::resolveWorldUnitImagePath(unit.name);
        IRenderBackend::DebugSprite unitSprite =
            render_prep_units::makeWorldUnitSprite(
                centerX,
                centerY,
                args.cellW,
                args.cellH,
                unitImagePath,
                unit.alive ? 0.96f : 0.70f);
        const bool hasUnitSprite = !unitSprite.texturePath.empty();
        if (render_prep_units::shouldRenderTintUnderPortrait(hasUnitSprite)) {
            scratch.worldQuads.push_back(tint);
        }
        if (hasUnitSprite) {
            scratch.sprites.push_back(std::move(unitSprite));
        }

        if (unit.alive) {
            shared_unit_hud::appendLegacyUnitHud(
                scratch.worldQuads,
                scratch.lines,
                scratch.textLines,
                args.sharedUnitHudCfg,
                unit,
                centerX,
                centerY,
                hudCellPx);
        }
    }
}

void appendBench(const Args& args,
                 float line,
                 float worldCellSize,
                 session_render_scratch::RenderScratch& scratch,
    Result& result) {
    const int benchSlots = std::max(1, args.benchSlots);
    const float benchGap =
        static_cast<float>(std::max(0, args.benchGapCells)) *
        (args.boardH / static_cast<float>(std::max(1, args.rows)));
    const float benchH = std::max(26.0f, args.minDim * 0.085f);
    const float benchW = std::max(
        160.0f,
        std::min(args.boardW, static_cast<float>(args.drawableW) - 40.0f));
    const float benchX = (static_cast<float>(args.drawableW) - benchW) * 0.5f;
    const float desiredBenchY = args.boardY + args.boardH + benchGap;
    const float benchY = std::min(
        desiredBenchY,
        static_cast<float>(args.drawableH) - benchH - 24.0f);

    const bool benchOverlapsBoard = (benchY <= args.boardY + args.boardH + 3.0f);
    IRenderBackend::DebugQuad benchBg;
    benchBg.x = benchX;
    benchBg.y = benchY;
    benchBg.w = benchW;
    benchBg.h = benchH;
    benchBg.r = 0.09f;
    benchBg.g = 0.12f;
    benchBg.b = 0.15f;
    benchBg.a = benchOverlapsBoard ? 0.90f : 0.96f;
    scratch.worldQuads.push_back(benchBg);

    const float benchCellW = benchW / static_cast<float>(benchSlots);
    const float benchLineThickness = std::max(1.0f, line * 0.95f);
    for (int slot = 0; slot < benchSlots; ++slot) {
        IRenderBackend::DebugQuad cellBg;
        cellBg.x = benchX + benchCellW * static_cast<float>(slot);
        cellBg.y = benchY;
        cellBg.w = benchCellW;
        cellBg.h = benchH;
        const bool dark = (slot % 2) == 0;
        cellBg.r = dark ? 0.10f : 0.13f;
        cellBg.g = dark ? 0.13f : 0.16f;
        cellBg.b = dark ? 0.17f : 0.20f;
        cellBg.a = benchOverlapsBoard ? 0.14f : 0.20f;
        scratch.worldQuads.push_back(cellBg);
    }

    for (int slot = 0; slot <= benchSlots; ++slot) {
        IRenderBackend::DebugLine slotLine;
        slotLine.x1 = benchX + benchCellW * static_cast<float>(slot);
        slotLine.y1 = benchY;
        slotLine.x2 = slotLine.x1;
        slotLine.y2 = benchY + benchH;
        slotLine.thickness = benchLineThickness;
        slotLine.r = 0.58f;
        slotLine.g = 0.66f;
        slotLine.b = 0.74f;
        slotLine.a = 0.96f;
        scratch.lines.push_back(slotLine);
    }

    IRenderBackend::DebugLine top;
    top.x1 = benchX;
    top.y1 = benchY;
    top.x2 = benchX + benchW;
    top.y2 = benchY;
    top.thickness = benchLineThickness;
    top.r = 0.64f;
    top.g = 0.71f;
    top.b = 0.79f;
    top.a = 0.98f;
    scratch.lines.push_back(top);

    IRenderBackend::DebugLine bottom = top;
    bottom.y1 = benchY + benchH;
    bottom.y2 = benchY + benchH;
    scratch.lines.push_back(bottom);

    for (const auto& unit : args.gameWorld->getBenchPokemons()) {
        ++result.visibleAnimatedUnits;
        const int slot = render_prep_projection::worldToBenchSlot(
            unit.position.x,
            benchSlots,
            worldCellSize);
        IRenderBackend::DebugQuad benchUnit;
        benchUnit.x = benchX + benchCellW * static_cast<float>(slot) + benchCellW * 0.20f;
        benchUnit.y = benchY + benchH * 0.20f;
        benchUnit.w = benchCellW * 0.60f;
        benchUnit.h = benchH * 0.60f;
        benchUnit.r = 0.34f;
        benchUnit.g = 0.73f;
        benchUnit.b = 0.96f;
        benchUnit.a = 0.24f;

        const std::string benchImagePath =
            render_prep_units::resolveWorldUnitImagePath(unit.name);
        IRenderBackend::DebugSprite benchSprite =
            render_prep_units::makeBenchUnitSprite(
                benchUnit.x,
                benchUnit.y,
                benchUnit.w,
                benchUnit.h,
                benchImagePath,
                0.92f);
        const bool hasBenchSprite = !benchSprite.texturePath.empty();
        if (render_prep_units::shouldRenderTintUnderPortrait(hasBenchSprite)) {
            scratch.worldQuads.push_back(benchUnit);
        }
        if (hasBenchSprite) {
            scratch.sprites.push_back(std::move(benchSprite));
        }
    }
}

} // namespace

Result appendLegacyWorldView(const Args& args,
                             session_render_scratch::RenderScratch& scratch) {
    Result result{};
    appendBoardBackdrop(args, scratch);
    const float line = boardLineThickness(args);
    appendBoardGridLines(args, line, scratch);

    if (!args.renderWorld || args.gameWorld == nullptr) {
        return result;
    }

    const float worldCellSize = args.gameWorld->getBoardCellSize();
    const float hudCellPx = std::clamp(args.minDim * 0.070f, 38.0f, 58.0f);
    appendBoardUnits(args, hudCellPx, worldCellSize, scratch, result);
    appendBench(args, line, worldCellSize, scratch, result);
    return result;
}

} // namespace game::runtime::session_legacy_world_view
