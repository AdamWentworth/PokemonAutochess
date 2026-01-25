// src/engine/render/ModelMeshTypes.h
#pragma once

#include <cstdint>

namespace pac_model_types {

struct Vertex {
    float px, py, pz;
    float u, v;
    std::uint16_t j0, j1, j2, j3;
    float w0, w1, w2, w3;

    // glTF COLOR_0 (linear 0..1)
    float r, g, b, a;
};

} // namespace pac_model_types
