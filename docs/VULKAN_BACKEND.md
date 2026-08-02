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
- character-inking silhouette outline replay in both direct and indirect world
  submission, ordered before the textured surface pass
- shared ACES output tone mapping and exposure
- GPU model transforms and palette skinning, including clip-skinning mode
- prewarmed keyed world geometry in paged device-local arena buffers, using
  indexed base-vertex offsets instead of one allocation per mesh
- native indexed-mesh instancing with rigid or per-instance skin palettes
- direct shared world-scene draw-class submission for rigid and skinned models
- capability-gated descriptor-indexed material tables and order-preserving
  indexed indirect multi-draw over the shared geometry arena
- content-verified frame-local reuse for identical skin-palette payloads
- frame-local view/material uniform reuse, prepared scene-material descriptors,
  and redundant pipeline/viewport/geometry-buffer binding suppression
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
- `vulkan/VulkanRenderBackendGeometryArena.cpp`: paged device-local arena
  allocation and lifetime
- `vulkan/VulkanGeometryArenaLayout.*`: tested API-independent byte-offset to
  indexed-draw planning
- `vulkan/VulkanRenderBackendInstances.cpp`: transient instance records and
  per-instance skin-palette packing/reuse
- `vulkan/VulkanRenderBackendState.cpp`: frame-local uniform reuse, command
  binding state, viewport state, and optional cache telemetry
- `vulkan/VulkanRenderBackendWorldScene.cpp`: shared scene registry/material
  cache lifetime and compatibility draw-class submission
- `vulkan/VulkanRenderBackendIndirectWorldScene.cpp`: indirect-scene
  eligibility, per-draw state/command packing, and grouped submission
- `vulkan/VulkanRenderBackendMaterialTable.cpp`: frame-local indexed material
  descriptor-table registration and synchronization
- `vulkan/VulkanWorldIndirectBatch.*`: tested order-preserving grouping of
  contiguous pipeline/geometry-buffer runs
- `vulkan/VulkanWorldIndirectState.h`: tested CPU/GPU indirect draw-state and
  push-constant packing
- `vulkan/VulkanWorldSceneData.h`: shared scene-material translation contract
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
- `assets/shaders/vulkan/world_indirect*`: descriptor-indexed world shading and
  draw-ID-based instance/material addressing

## Known Maturity Gaps

This is a usable backend, not a claim of pixel-perfect or performance parity
with the established renderers:

- initial cache population uses synchronous transfer submission; startup
  prewarming keeps that work out of steady-state frames
- animated skin palettes remain transient; identical payloads are reused within
  a frame, but static palette components are not retained across frames
- the indirect path requires descriptor indexing, non-uniform sampled-image
  array access, shader draw parameters, and multi-draw indirect support; devices
  without that feature/limit combination use the direct path
- the indexed table currently holds 256 world materials; scenes exceeding that
  capacity fall back to direct material descriptors

The PMREM resource now uses the same linear RGBA16F precision contract as
OpenGL and D3D12. The deterministic three-backend image-diff gate passes after
the conversion. Retained geometry and GPU skinning remove
the largest Vulkan-only transient CPU geometry work. Native instancing and
world-scene submission remove repeated rigid draws and intermediate projected
batch construction. Frame-local state reuse now removes repeated view/material
uniform uploads plus redundant pipeline, viewport, and geometry-buffer
commands, while the registry-generation material cache avoids rebuilding
Vulkan material bindings on every scene draw. The dense-roster measurement
showed unique material descriptors per draw class, so opaque sorting could not
reduce its material switches. The descriptor-indexed path now consumes that
arena directly: a representative steady frame submitted 58 logical world draw
classes with one material-table bind and one `vkCmdDrawIndexedIndirect` call,
while preserving the direct path as its compatibility fallback.

The paired cross-frame static-palette experiment was not retained. On the
dense-roster Release scene it increased CPU frame time from 1.496 ms to
2.194 ms and render-build time from 0.859 ms to 1.460 ms. The existing
frame-local, composed-palette path remains the preferred implementation.

Indirect character outline replay was retained. In the inked dense-roster
same-binary comparison it reduced CPU frame time from 1.872 ms to 1.637 ms,
GPU frame time from 0.642 ms to 0.538 ms, and average API draw calls from 153
to 33.75. Outline extrusion is shader-driven, and the outline is submitted
before the textured surface so blended materials remain colored and textured.

The indirect path is enabled by default when supported. Set
`PAC_VULKAN_INDIRECT_WORLD_SCENE=0` for same-binary A/B profiling or fallback
validation.

Set `PAC_VULKAN_DISABLE_DESCRIPTOR_INDEXING=1` or
`PAC_VULKAN_DISABLE_INDIRECT_WORLD=1` only for compatibility testing. Leave
them unset in the preferred runtime configuration. See
`docs/RENDERER_CONFIGURATION.md` for the cross-backend configuration guide.

Set `PHLOSION_VULKAN_STATE_CACHE_LOG=1` to sample Vulkan palette/state-cache counters
every 120 frames while profiling, including sprite instances, texture runs,
draws saved, transient uploads saved, vertex/index buffer bind reuse, indirect
API calls/commands, and compatibility fallbacks.

Validate changes with the backend contract tests,
`tools/render_parity_matrix.ps1`, and `tools/runtime_visual_smoke.ps1`. Use
`tools/render_parity_screenshot_diff.ps1` for focused ad-hoc snapshot checks.
Use `tools/renderer_qualification.ps1` for a formal checkpoint that exercises
both the native capability-selected path and the forced direct compatibility
path while recording adapter/driver evidence.
Performance comparisons should use the same
scene, resolution, settings, build type, and warmup policy across backends.
