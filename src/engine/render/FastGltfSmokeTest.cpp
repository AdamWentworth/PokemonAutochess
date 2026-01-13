// FastGltfSmokeTest.cpp
//
// Step 1: integration-only compilation smoke test for fastgltf.
// No runtime behavior change. This file should compile and link if fastgltf is correctly provided by vcpkg.

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>

namespace pac::fastgltf_smoketest {

// Force a couple of types to be referenced so headers are actually instantiated/parsed.
static void touch_types() {
    fastgltf::Parser parser;
    (void)parser;

    fastgltf::Asset asset{};
    (void)asset;
}

} // namespace pac::fastgltf_smoketest
