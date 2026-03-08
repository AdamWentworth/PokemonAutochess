# Display And Graphics Roadmap

Date: 2026-03-07

## Purpose
- Define what the project should eventually ship for display and graphics settings.
- Separate real near-term work from placeholder UI and long-term enthusiast features.
- Keep the roadmap grounded in current engine reality instead of generic PC graphics checklists.

## Current Baseline

### What is actually implemented today
- Windowed and fullscreen switching.
- Resolution switching for the exposed presets.
- Renderer backend preference for next launch (`OpenGL`, `D3D12`, `Vulkan` placeholder).
- VSync preference for next launch.
- Preferred GPU / discrete GPU preference for next launch.
- Character inking toggle.

### What is currently exposed as placeholder UI
- FPS cap.
- UI scale.
- Quality preset.
- Most non-video settings menus are also still placeholder-driven.

### Important technical context
- The project is currently a shared gameplay render path with `OpenGL` and `D3D12`.
- New display and graphics work should target both active backends wherever shared implementation is viable. Backend-only behavior should be the exception and should be documented when unavoidable.
- `Vulkan` is still placeholder, not an active implementation target.
- Recent perf work moved the main gameplay hot paths out of Lua and reduced fixed-step cost substantially.
- Current bottlenecks are mostly in render build / projected unit rendering, not in raw GPU saturation.
- That matters because many modern GPU features only help when the game is meaningfully GPU-bound.

## What A Real PC Graphics Stack Should Eventually Include

This project does not need every buzzword. It needs the settings that match its renderer, scale, and likely user expectations.

### Tier 1: Must-Have Shipping Display Features
- Display mode:
  - `Windowed`
  - `Borderless Fullscreen`
  - `Exclusive Fullscreen` only if there is a real technical reason to keep it
- Resolution:
  - desktop-native
  - common 16:9 presets
  - refresh-rate aware selection if SDL/platform support is clean
- VSync:
  - on/off
  - clearly indicate if it applies immediately or on restart
- Frame rate limit:
  - uncapped
  - 30 / 60 / 90 / 120 / 144 / 165 / 240
  - custom cap later if useful
- Render backend:
  - `OpenGL`
  - `D3D12`
  - hide `Vulkan` until implemented
- UI scale:
  - global HUD/menu scale
  - separate from render quality
- Restart labeling:
  - any setting that needs restart must say so explicitly in the menu

### Tier 2: Must-Have Shipping Graphics Features
- Graphics preset:
  - `Low`, `Medium`, `High`, `Ultra`
  - must be a real preset bundle, not a label
- Internal render scale:
  - `50%` to `100%` in reasonable steps
  - more valuable than exotic vendor tech in the near term
- Dynamic resolution:
  - target FPS based
  - only after internal render scale exists cleanly
- Anti-aliasing:
  - start simple
  - likely FXAA or TAA depending the shared path and art tolerances
- Texture quality:
  - mainly a memory/bandwidth budget setting
- Anisotropic filtering:
  - `Off / 2x / 4x / 8x / 16x`
  - low complexity, clear visual benefit
- Effects quality:
  - particle density
  - projected overlay complexity
  - optional heavy VFX reductions
- Model / animation quality budget:
  - projected unit LOD policy
  - update-rate / budget controls only if visually acceptable

### Tier 3: Good Secondary Features
- Brightness / gamma calibration.
- Motion reduction toggles that are real:
  - camera shake
  - hit stop
  - intense flashes
- Colorblind / contrast support that is real, not placeholder.
- Screenshot and capture options if the game starts supporting creator workflows more directly.

## Placeholder Comparison: What To Do

### Keep And Finish
- `VSync`
  - already has real preference plumbing
  - should become a fully trusted control
- `Resolution`
  - already functional
  - extend with better mode enumeration later
- `Fullscreen`
  - already functional
  - likely evolve into `Windowed / Borderless / Fullscreen`
- `Render API`
  - keep, but only expose implemented backends

### Wire Soon
- `FPS Cap`
  - this should be real
  - it is useful for thermals, laptops, battery, and frame pacing experiments
- `UI Scale`
  - this should be real
  - this is usability, not graphics fluff
- `Quality Preset`
  - should exist only after there are multiple real graphics controls to bind together

### Hide Until Real
- `Vulkan`
  - keep out of normal player-facing settings until implemented
- any menu item marked `(placeholder)` that has no engine behavior behind it

## Recommended Final Menu Structure

### Display
- Display mode
- Resolution
- Refresh rate
- VSync
- Frame cap
- Render backend
- GPU adapter preference

### Graphics
- Graphics preset
- Render scale
- Dynamic resolution
- Anti-aliasing
- Texture quality
- Anisotropic filtering
- Effects quality
- Character inking

### Accessibility / Comfort
- UI scale
- Text scale
- Reduce motion
- Camera shake
- Hit stop
- High contrast UI
- Color filters

## Recommended Implementation Order

### Phase 0: Clean Up Menu Honesty
- Remove `(placeholder)` items from the shipping-facing path or gray them out as unavailable.
- Hide `Vulkan` unless running a developer build.
- Add explicit `applies immediately` vs `restart required` labeling.

Definition of done:
- The menu stops implying features exist when they do not.

### Phase 1: Finish Core Display Controls
- Make `VSync` fully trusted and confirm per-backend behavior.
- Implement real `FPS cap`.
- Implement real `UI scale`.
- Add `Borderless Fullscreen`.
- Expand resolution handling to real display mode enumeration instead of only a hardcoded preset list.

Definition of done:
- The project has a respectable baseline PC display menu.

### Phase 2: Build Real Graphics Presets
- Implement real graphics options first:
  - render scale
  - anti-aliasing
  - anisotropic filtering
  - texture quality
  - effects quality
- Then define presets as bundles over those settings.

Definition of done:
- `Low/Medium/High/Ultra` are honest, predictable, and measurable.

### Phase 3: Dynamic Performance Controls
- Add internal resolution scale UI.
- Add dynamic resolution targeting a chosen FPS band.
- Add telemetry so the game can report when DRS is active and what scale range it used.

Definition of done:
- The game can preserve frame rate in heavy scenes without pretending the GPU never dips.

### Phase 4: HDR And Presentation Polish
- Investigate HDR only after the render path, tone mapping, and swap-chain handling are stable.
- Add correct SDR/HDR output handling and calibration UX if pursued.

Definition of done:
- HDR is either implemented correctly or deliberately omitted.

### Phase 5: Advanced Platform Features
- Re-evaluate upscalers and vendor features only after the project is clearly GPU-bound at target settings.

Definition of done:
- advanced features are driven by profiling, not fashion.

## How The YouTube Feature List Maps To This Project

The transcript is a useful glossary, but not a roadmap by itself. For this project, the right question is not "can modern GPUs do this?" It is "does this solve our real bottleneck and fit our renderer?"

### Ray Tracing
Recommendation:
- Not a roadmap priority.

Reason:
- The game does not currently read like a renderer built around reflective, shadow-rich, physically-lit showcase scenes.
- The current bottleneck is not "we need much more expensive lighting."
- This would dramatically expand renderer complexity for limited practical gain.

### Path Tracing
Recommendation:
- Out of scope.

Reason:
- This is a major renderer direction change, not a settings feature.
- It does not fit the current project scale or current perf priorities.

### DLSS
Recommendation:
- Defer.

Reason:
- Vendor-specific.
- Integration cost is nontrivial.
- Current telemetry suggests the project is often CPU/render-build limited before it is GPU limited.
- DLSS helps GPU pixel cost more than CPU-side render prep cost.

### FSR
Recommendation:
- Consider before DLSS, but still later.

Reason:
- Cross-vendor.
- Better fit than DLSS if the project eventually needs upscaling.
- Still lower priority than internal render scale and dynamic resolution.

### Frame Generation
Recommendation:
- Very low priority, likely skip.

Reason:
- Adds latency and artifact risk.
- This project benefits more from making real frames cheaper.
- It is a poor substitute for fixing actual frame time.

### Dynamic Resolution
Recommendation:
- Yes, real roadmap item.

Reason:
- Practical.
- Cross-vendor.
- Works with the project's existing need for steady frame time.
- Much more realistic than DLSS/FG in the near term.

### HDR
Recommendation:
- Later, but valid.

Reason:
- Real player-facing value if done correctly.
- Requires proper tone mapping, output format handling, and settings UX.

### Display Sync: VSync / G-Sync / FreeSync
Recommendation:
- Yes for VSync.
- VRR support should mostly come "for free" by behaving correctly with modern swap-chain / presentation flow rather than exposing fake vendor toggles.

Reason:
- Players expect VSync.
- VRR is usually a presentation-path compatibility result, not a separate menu feature labeled "G-Sync."

### Anti-Aliasing
Recommendation:
- Yes, core roadmap item.

Reason:
- High visual value.
- Reasonable complexity.
- Fits the art/rendering profile much better than ray tracing.

### Anisotropic Filtering
Recommendation:
- Yes, core roadmap item.

Reason:
- Cheap and useful.
- Strong benefit on oblique ground/board textures.

### Texture Quality
Recommendation:
- Yes, core roadmap item.

Reason:
- Standard PC expectation.
- Straightforward budget knob.

### NVENC / GPU Video Encoding
Recommendation:
- Not part of normal display/graphics settings.

Reason:
- Useful for tooling, capture, or creator workflows, not for the player-facing graphics menu.

## Recommended Priority Order

If the project wants the highest return with the least nonsense, the order should be:

1. Make the display menu honest.
2. Finish `VSync`, `FPS cap`, `UI scale`, and proper display mode handling.
3. Add real graphics options and presets.
4. Add render scale and dynamic resolution.
5. Add anti-aliasing and anisotropic filtering if not already present.
6. Consider HDR only after the render path is stable.
7. Consider FSR only if higher-resolution GPU cost becomes a real bottleneck.
8. Keep DLSS, frame generation, ray tracing, and path tracing out of the near-term roadmap.

## Practical Rule For Future Features
- If a feature reduces CPU render-build cost or stabilizes frame time on all hardware, it is likely worth prioritizing.
- If a feature mainly adds marketing value but increases renderer complexity, it should be deferred unless profiling proves a real need.
- Do not expose a setting in the menu until it has:
  - real implementation
  - persistence
  - known runtime/restart behavior
  - basic test coverage
  - clear expected visual/perf impact

## Bottom Line
- The project should aim for a strong, honest PC settings stack, not a maximalist AAA buzzword stack.
- Near-term wins are practical controls: frame cap, UI scale, borderless mode, real presets, render scale, dynamic resolution, AA, AF, texture/effects quality.
- Ray tracing, path tracing, DLSS, and frame generation are not the right next problems for this renderer.

