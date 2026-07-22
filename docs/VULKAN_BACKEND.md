# Vulkan Backend

Status: Active
Type: Reference
Last updated: 2026-07-22

## Current Scope

Vulkan is an implemented renderer option on platforms where SDL2 can create a
Vulkan surface and the selected adapter supports graphics, presentation, and
`VK_KHR_swapchain`. It uses the same gameplay presentation contracts as
OpenGL and D3D12.

Implemented today:

- SDL Vulkan window and surface creation
- physical-device selection with preferred-adapter matching
- swapchain creation, resize recovery, and VSync/present-mode switching
- per-frame command buffers, synchronization, timestamps, and renderer stats
- depth-buffered world triangles and indexed meshes
- base-color, normal, metallic/roughness, occlusion, and emissive material maps
- direct GGX PBR lighting, alpha masking/windows, and the shared blend modes
- shared ACES output tone mapping and exposure
- CPU model transforms and CPU skinning fallback
- debug quads, lines, triangles, sprites, text/card UI, and texture prewarm
- runtime screenshots through the existing backend capture environment variables
- the shared renderer parity-state startup check

## Selection

Choose Vulkan in Display settings and restart, or override a single launch:

```powershell
$env:PAC_RENDER_BACKEND = "vulkan"
.\build\Debug\PokemonAutochess.exe
```

If Vulkan initialization fails, normal renderer recovery records the failure
and attempts the OpenGL fallback.

## Build Requirements

The vcpkg manifest supplies Vulkan headers/loader support, SDL2's Vulkan
integration, and `glslangValidator`. CMake compiles
`assets/shaders/vulkan/*.vert` and `*.frag` to generated SPIR-V before building
`engine_render`. The runtime machine still needs a Vulkan-capable display
driver.

## Module Ownership

The Vulkan implementation is split by API responsibility so parity work does
not grow a second monolithic backend:

- `VulkanRenderBackend.*`: public `IRenderBackend` adapter
- `vulkan/VulkanRenderBackendInternal.h`: private Vulkan state and contracts
- `vulkan/VulkanRenderBackendLifecycle.cpp`: device, swapchain, pipelines,
  frame lifetime, and capture infrastructure
- `vulkan/VulkanRenderBackendDraw.cpp`: debug, sprite, and world submission
- `vulkan/VulkanRenderBackendTextures.cpp`: texture upload and sprite cache
- `vulkan/VulkanRenderBackendMaterials.cpp`: world-map cache and five-map
  descriptor assembly
- `vulkan/VulkanWorldMaterialState.h`: tested 128-byte world push-constant
  packing
- `assets/shaders/vulkan/world_material.glsl`: Vulkan world-material shading

## Known Maturity Gaps

This is a usable initial backend, not a claim of pixel-perfect or performance
parity with the established renderers:

- world shading now covers the five material maps and direct GGX PBR, but its
  ambient term is an albedo-tinted approximation; neutral-room PMREM/IBL and
  exact OpenGL/D3D12 material-lighting identity remain to be ported
- authored tail-fire material mode, character inking/outline submission, and
  dual-source blending still use simpler fallback behavior
- cached geometry entrypoints currently use dynamic transient uploads
- indexed-mesh instancing and the fast world-scene route are not implemented;
  shared fallback submission remains functional
- model skinning is transformed on the CPU rather than through a Vulkan
  palette/clip-skinning pipeline
- sprite and indexed submission need batching and descriptor/state-change
  optimization after fidelity work is complete

On the current GTX 1070 debug reference run, the material path reports roughly
`0.1 ms` GPU time while Vulkan render-build work is roughly `7.6 ms`. That
points the next performance slice at retained geometry/GPU skinning rather than
at further fragment-shader micro-optimization.

Validate changes with the backend contract tests plus
`tools/runtime_visual_smoke.ps1`. Performance comparisons should use the same
scene, resolution, settings, build type, and warmup policy across backends.
