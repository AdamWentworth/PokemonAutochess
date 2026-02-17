#include "engine/render/DebugGeometry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

bool approx(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

float minX(const std::vector<engine::render::debug::Vertex2D>& verts) {
    float out = verts.front().x;
    for (const auto& v : verts) out = std::min(out, v.x);
    return out;
}

float maxX(const std::vector<engine::render::debug::Vertex2D>& verts) {
    float out = verts.front().x;
    for (const auto& v : verts) out = std::max(out, v.x);
    return out;
}

float minY(const std::vector<engine::render::debug::Vertex2D>& verts) {
    float out = verts.front().y;
    for (const auto& v : verts) out = std::min(out, v.y);
    return out;
}

float maxY(const std::vector<engine::render::debug::Vertex2D>& verts) {
    float out = verts.front().y;
    for (const auto& v : verts) out = std::max(out, v.y);
    return out;
}

} // namespace

bool test_debug_geometry_line_raster_contract(std::string& outFail) {
    using engine::render::debug::Vertex2D;

    std::vector<Vertex2D> vertices;
    IRenderBackend::DebugLine hLine;
    hLine.x1 = 10.0f;
    hLine.y1 = 20.0f;
    hLine.x2 = 30.0f;
    hLine.y2 = 20.0f;
    hLine.thickness = 4.0f;
    hLine.r = 0.3f;
    hLine.g = 0.4f;
    hLine.b = 0.5f;
    hLine.a = 0.9f;
    if (!engine::render::debug::appendLineAsTriangles(hLine, vertices)) {
        outFail = "expected horizontal line rasterization to succeed";
        return false;
    }
    if (vertices.size() != 6u) {
        outFail = "horizontal line should rasterize to 6 vertices";
        return false;
    }
    if (!approx(minX(vertices), 10.0f) || !approx(maxX(vertices), 30.0f)) {
        outFail = "horizontal line x bounds mismatch";
        return false;
    }
    if (!approx(minY(vertices), 18.0f) || !approx(maxY(vertices), 22.0f)) {
        outFail = "horizontal line y bounds mismatch";
        return false;
    }
    if (!approx(vertices[0].r, 0.3f) || !approx(vertices[0].g, 0.4f) ||
        !approx(vertices[0].b, 0.5f) || !approx(vertices[0].a, 0.9f)) {
        outFail = "line color should be copied into vertex output";
        return false;
    }

    vertices.clear();
    IRenderBackend::DebugLine dLine;
    dLine.x1 = 0.0f;
    dLine.y1 = 0.0f;
    dLine.x2 = 10.0f;
    dLine.y2 = 10.0f;
    dLine.thickness = 2.0f;
    if (!engine::render::debug::appendLineAsTriangles(dLine, vertices)) {
        outFail = "expected diagonal line rasterization to succeed";
        return false;
    }
    if (vertices.size() != 6u) {
        outFail = "diagonal line should rasterize to 6 vertices";
        return false;
    }
    if (!(minX(vertices) < 0.0f) || !(minY(vertices) < 0.0f)) {
        outFail = "diagonal line should expand bounds by thickness";
        return false;
    }
    if (!(maxX(vertices) > 10.0f) || !(maxY(vertices) > 10.0f)) {
        outFail = "diagonal line should expand max bounds by thickness";
        return false;
    }

    vertices.clear();
    IRenderBackend::DebugLine degenerate;
    degenerate.x1 = 5.0f;
    degenerate.y1 = 5.0f;
    degenerate.x2 = 5.0f;
    degenerate.y2 = 5.0f;
    degenerate.thickness = 3.0f;
    if (engine::render::debug::appendLineAsTriangles(degenerate, vertices)) {
        outFail = "degenerate line should be rejected";
        return false;
    }
    if (!vertices.empty()) {
        outFail = "degenerate line should not emit vertices";
        return false;
    }

    return true;
}
