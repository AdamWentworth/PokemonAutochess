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
- neutral-room cube-UV PMREM with diffuse/specular IBL and multiscattering
- camera-relative direct lighting through per-draw dynamic uniform state
- authored and procedural tail-fire material playback
- character-inking silhouette outline replay
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
  plus environment descriptor assembly
- `vulkan/VulkanRenderBackendEnvironment.cpp`: neutral PMREM validation,
  RGBM upload, and environment lifetime
- `vulkan/VulkanWorldMaterialLayout.h`: descriptor binding and PMREM encoding
  constants shared by the Vulkan resource modules and tests
- `vulkan/VulkanWorldMaterialState.h`: tested 128-byte world push-constant
  packing
- `vulkan/VulkanWorldViewState.h`: tested camera/view uniform packing used by
  camera-relative world lighting
- `vulkan/VulkanWorldSpecializedMaterialState.h`: tested animated-material
  uniform packing for specialized shader modes
- `assets/shaders/vulkan/world_material.glsl`: Vulkan world-material shading
- `assets/shaders/vulkan/world_environment.glsl`: cube-UV PMREM sampling and
  image-based lighting helpers
- `assets/shaders/vulkan/world_tail_fire.glsl`: tail-fire atlas playback and
  procedural flame shading

## Known Maturity Gaps

This is a usable initial backend, not a claim of pixel-perfect or performance
parity with the established renderers:

- world shading now covers the five material maps, direct GGX PBR, and the
  neutral-room PMREM/IBL treatment; RGBM8 rather than half-float environment
  precision still differs from OpenGL/D3D12
- dual-source blending still uses the standard blend-mode fallback
- cached geometry entrypoints currently use dynamic transient uploads
- indexed-mesh instancing and the fast world-scene route are not implemented;
  shared fallback submission remains functional
- model skinning is transformed on the CPU rather than through a Vulkan
  palette/clip-skinning pipeline
- sprite and indexed submission need batching and descriptor/state-change
  optimization after fidelity work is complete

The PMREM smoke reference now matches OpenGL's measured scene luminance within
`0.0002` in the lower-center region and exactly at the reported four-decimal
precision in the center-board region. Render-build cost remains dominated by
transient CPU geometry work, so the next performance slice should target
retained geometry/GPU skinning rather than fragment-shader micro-optimization.

Validate changes with the backend contract tests plus
`tools/runtime_visual_smoke.ps1`. Performance comparisons should use the same
scene, resolution, settings, build type, and warmup policy across backends.
