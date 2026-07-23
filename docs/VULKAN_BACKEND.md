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
- native dual-source alpha/additive pipelines with a capability-gated fallback
- neutral-room cube-UV PMREM with diffuse/specular IBL and multiscattering
- camera-relative direct lighting through per-draw dynamic uniform state
- authored and procedural tail-fire material playback
- character-inking silhouette outline replay
- shared ACES output tone mapping and exposure
- GPU model transforms and palette skinning, including clip-skinning mode
- prewarmed device-local vertex/index caches for keyed world geometry
- native indexed-mesh instancing with rigid or per-instance skin palettes
- direct shared world-scene draw-class submission for rigid and skinned models
- content-verified frame-local reuse for identical skin-palette payloads
- frame-local view/material uniform reuse, prepared scene-material descriptors,
  and redundant pipeline/viewport binding suppression
- order-preserving instanced sprite runs with one transient instance upload per
  submission call and adjacent same-texture draw coalescing
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
- `vulkan/VulkanRenderBackendDraw.cpp`: debug geometry and world submission
- `vulkan/VulkanRenderBackendSprites.cpp`: instanced sprite packing, contiguous
  texture-run construction, and submission
- `vulkan/VulkanRenderBackendGeometry.cpp`: device-local world-geometry upload,
  cache lifetime, and cached submission
- `vulkan/VulkanRenderBackendInstances.cpp`: transient instance records and
  per-instance skin-palette packing/reuse
- `vulkan/VulkanRenderBackendState.cpp`: frame-local uniform reuse, command
  binding state, viewport state, and optional cache telemetry
- `vulkan/VulkanRenderBackendWorldScene.cpp`: shared scene registry/material
  translation, prepared material bindings, and draw-class submission
- `vulkan/VulkanRenderBackendTextures.cpp`: texture upload and sprite cache
- `vulkan/VulkanRenderBackendMaterials.cpp`: world-map cache and five-map
  plus environment descriptor assembly
- `vulkan/VulkanRenderBackendEnvironment.cpp`: neutral PMREM validation,
  linear RGBA16F upload, and environment lifetime
- `vulkan/VulkanEnvironmentParity.h`: maps the actual Vulkan PMREM image format
  into the shared runtime parity contract
- `vulkan/VulkanWorldMaterialLayout.h`: tested material descriptor bindings
- `vulkan/VulkanWorldMaterialState.h`: tested 128-byte world push-constant
  packing
- `vulkan/VulkanWorldViewState.h`: tested camera/view uniform packing used by
  camera-relative world lighting
- `vulkan/VulkanWorldSpecializedMaterialState.h`: tested animated-material
  uniform packing for specialized shader modes
- `vulkan/VulkanWorldTransformState.h`: tested model, vertex-color, and GPU
  skin-palette state packing
- `vulkan/VulkanSpriteInstanceState.h`: tested sprite rectangle, UV, and color
  instance packing shared by the CPU writer and vertex layout
- `assets/shaders/vulkan/world_material.glsl`: Vulkan world-material shading
- `assets/shaders/vulkan/world_environment.glsl`: cube-UV PMREM sampling and
  image-based lighting helpers
- `assets/shaders/vulkan/world_tail_fire.glsl`: tail-fire atlas playback and
  procedural flame shading

## Known Maturity Gaps

This is a usable backend, not a claim of pixel-perfect or performance parity
with the established renderers:

- initial cache population uses synchronous transfer submission; startup
  prewarming keeps that work out of steady-state frames
- animated skin palettes remain transient; identical payloads are reused within
  a frame, but static palette components are not retained across frames
- command submission suppresses redundant pipeline and viewport work and
  reuses prepared scene materials, but larger multi-draw/descriptor-indexing
  batches and material-aware opaque ordering are not implemented

The PMREM resource now uses the same linear RGBA16F precision contract as
OpenGL and D3D12. The deterministic three-backend image-diff gate passes after
the conversion. Retained geometry and GPU skinning remove
the largest Vulkan-only transient CPU geometry work. Native instancing and
world-scene submission remove repeated rigid draws and intermediate projected
batch construction. Frame-local state reuse now removes repeated view/material
uniform uploads plus redundant pipeline and viewport commands, while the
registry-generation material cache avoids rebuilding Vulkan material bindings
on every scene draw. The next performance slice should use measured scene
ordering to decide whether opaque material grouping, larger multi-draw batches,
or cross-frame static palette retention has the best return.

Set `PAC_VULKAN_STATE_CACHE_LOG=1` to sample Vulkan palette/state-cache counters
every 120 frames while profiling, including sprite instances, texture runs,
draws saved, and transient uploads saved.

Validate changes with the backend contract tests,
`tools/render_parity_matrix.ps1`, and `tools/runtime_visual_smoke.ps1`. Use
`tools/render_parity_screenshot_diff.ps1` for focused ad-hoc snapshot checks.
Performance comparisons should use the same
scene, resolution, settings, build type, and warmup policy across backends.
