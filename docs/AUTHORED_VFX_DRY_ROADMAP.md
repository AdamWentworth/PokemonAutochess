# Authored VFX DRY Roadmap

## Goal

Reduce duplicated Growl/Tackle authored-VFX startup and gameplay glue without collapsing effect-specific behavior into one hard-to-maintain abstraction.

The target outcome is:

- one shared authored-VFX prewarm pipeline
- one shared authored-VFX gameplay bridge pipeline
- one shared authored mesh/texture bake/cache layer
- minimal per-effect code limited to snapshot/config construction and effect-specific quirks

## What The Current Codebase Is Telling Us

After reviewing the current runtime path, the duplication is real but concentrated.

### 1. Startup prewarm is duplicated almost line-for-line

These two files are near-mirrors:

- [RuntimeGrowlVfxPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeGrowlVfxPrewarm.cpp)
- [RuntimeTackleVfxPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeTackleVfxPrewarm.cpp)

Both currently do the same five jobs:

- resolve effect config
- build a representative render snapshot
- resolve backend meshes
- resolve and bake textures into the shared backend cache
- append authored batches and prewarm renderer batches

The only meaningful differences are:

- effect-specific snapshot construction
- effect-specific representative ages/rings
- effect-specific stats type

That is a strong signal for extracting a shared authored-VFX prewarm helper.

### 2. Gameplay Growl/Tackle bridges repeat the same authored batch assembly

These files also mirror each other heavily:

- [SharedProjectedWorldGrowlBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldGrowlBridge.cpp)
- [SharedProjectedWorldTackleBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTackleBridge.cpp)

Both currently do the same work:

- guard on runtime/backend availability
- build an effect snapshot from `GameWorld`
- resolve baked textures through the backend texture cache
- resolve reusable mesh data
- append authored batches
- convert authored batches into shared world batches

The meaningful differences are:

- Growl has the legacy enable gate
- Tackle splits per-ring to preserve single-ring batch behavior

That means we should not merge the files blindly, but we should extract a shared helper that accepts a small behavior policy.

### 3. Session startup fan-out is getting wider with every authored effect

The current fan-out lives in:

- [RuntimeStartupAssetPrewarm.h](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeStartupAssetPrewarm.h)
- [RuntimeStartupAssetPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeStartupAssetPrewarm.cpp)
- [SessionStartupRuntime.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionStartupRuntime.cpp)
- [SessionBackendAssetBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionBackendAssetBridge.cpp)
- [SessionRenderConfig.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionRenderConfig.cpp)

Every new authored effect currently adds:

- a new stats struct
- a new callback slot
- a new startup stage branch
- a new config flag
- a new backend bridge function

That is manageable for two effects, but it will scale badly if we add several more move VFX.

### 4. SharedAuthoredVfxInterop is the right seed for centralization

- [SharedAuthoredVfxInterop.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.cpp)

This file already owns:

- reusable mesh conversion
- reusable mesh caching
- authored batch -> shared world batch conversion

That is the right place to keep growing shared authored-VFX runtime plumbing.

## What Should Stay Effect-Specific

We should not force these into a generic layer yet:

- [GrowlWaveVFX.h](/c:/Code/PokemonAutochess/src/vfx/effects/growl/GrowlWaveVFX.h) and its snapshot/config semantics
- [TackleSmokeVFX.h](/c:/Code/PokemonAutochess/src/vfx/effects/tackle/TackleSmokeVFX.h) and its snapshot/config semantics
- effect manifests such as [growl_draw_passes.json](/c:/Code/PokemonAutochess/config/vfx/moves/growl_draw_passes.json) and [tackle_draw_passes.json](/c:/Code/PokemonAutochess/config/vfx/moves/tackle_draw_passes.json)
- effect-specific gameplay quirks, like Tackle’s single-ring split behavior

Those are real domain differences, not duplication.

## What Should Become Shared

These are the best DRY candidates:

### Shared authored prewarm execution

Centralize:

- mesh resolution
- baked texture lookup/build
- authored batch append
- renderer batch prewarm

Keep per-effect:

- config resolver
- representative snapshot builder
- stats labeling

### Shared authored gameplay bridge execution

Centralize:

- baked texture resolution
- mesh resolution via reusable mesh cache
- authored batch append
- conversion to shared world batches

Keep per-effect:

- effect enable gate
- snapshot builder callback
- ring batching policy

### Shared authored startup registration

Replace the current “one callback per effect” fan-out with a small authored-VFX startup registry.

Each authored effect should provide a descriptor like:

- display name
- enable flag function
- prewarm function
- progress slot
- stats formatter

That avoids adding a new `prewarmXxxVfx` member to multiple structs every time.

## Recommended Refactor Order

Do this in narrow phases so we can verify behavior after each one.

### Phase 0: Freeze Behavior With Targeted Tests

Before extracting anything, keep these passing as the baseline:

- `runtime_growl_vfx_prewarm_contract`
- `runtime_tackle_vfx_prewarm_contract`
- `runtime_startup_asset_prewarm_contract`
- `session_render_config_contract`
- `shared_authored_vfx_interop_contract`
- `projected_world_scene_seams_contract`
- `shared_authored_vfx_batches_contract`
- `tackle_smoke_vfx_contract`

Why first:

- the refactor risk is not compile failure
- the refactor risk is silently changing authored VFX timing, batching, or gameplay render behavior

### Phase 1: Extract Shared Authored Prewarm Core

Create one new shared helper, for example:

- `src/game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.h`
- `src/game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.cpp`

Responsibility:

- given a ready-made render snapshot, mesh resolver, and backend texture cache
- resolve meshes/textures
- bake pass textures
- append authored batches
- prewarm renderer batches
- return generic stats

Keep thin effect wrappers:

- [RuntimeGrowlVfxPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeGrowlVfxPrewarm.cpp)
- [RuntimeTackleVfxPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeTackleVfxPrewarm.cpp)

Those wrappers should shrink to:

- build snapshot
- call shared helper
- translate generic stats to effect-specific stats if needed

Verification after Phase 1:

- `runtime_growl_vfx_prewarm_contract`
- `runtime_tackle_vfx_prewarm_contract`
- `shared_authored_vfx_interop_contract`
- `tackle_smoke_vfx_contract`

### Phase 2: Extract Shared Authored Gameplay Bridge Core

Create one shared helper, for example:

- `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.h`
- `src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.cpp`

Responsibility:

- consume a built snapshot
- resolve baked textures
- resolve reusable meshes
- append authored batches
- push to shared world batches

Add a tiny policy/config struct for effect-specific behavior:

- `splitRingsIndividually`
- `reserveMultiplier`
- optional `enabled` gate already handled by wrapper

Then reduce:

- [SharedProjectedWorldGrowlBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldGrowlBridge.cpp)
- [SharedProjectedWorldTackleBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldTackleBridge.cpp)

to small wrappers that:

- guard runtime conditions
- build snapshot
- invoke the shared helper with the right policy

Verification after Phase 2:

- `projected_world_scene_seams_contract`
- `shared_authored_vfx_interop_contract`
- `shared_authored_vfx_batches_contract`
- `tackle_smoke_vfx_contract`

### Phase 3: Replace Per-Effect Startup Fan-Out With Authored VFX Registry

This is the first phase where we touch startup orchestration.

Current churn points:

- [RuntimeStartupAssetPrewarm.h](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeStartupAssetPrewarm.h)
- [RuntimeStartupAssetPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeStartupAssetPrewarm.cpp)
- [SessionStartupRuntime.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionStartupRuntime.cpp)
- [SessionBackendAssetBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionBackendAssetBridge.cpp)
- [SessionRenderConfig.h](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionRenderConfig.h)
- [SessionRenderConfig.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionRenderConfig.cpp)

Introduce a registry-driven path for authored VFX only.

Do not generalize tail fire, particle, UI, or world shading yet.

Example direction:

- one `AuthoredVfxPrewarmDescriptor`
- one `std::vector<AuthoredVfxPrewarmDescriptor>`
- one generic loop in startup

Each descriptor would supply:

- stable id like `growl` or `tackle`
- startup title string
- enable flag
- progress value
- callable returning generic authored prewarm stats
- log formatter

This lets new authored VFX be added in one place instead of editing four or five files.

Verification after Phase 3:

- `runtime_startup_asset_prewarm_contract`
- `session_render_config_contract`
- `runtime_growl_vfx_prewarm_contract`
- `runtime_tackle_vfx_prewarm_contract`

### Phase 4: Consolidate Test Harness Duplication

After the production code is stable, DRY the tests.

Good candidates:

- [TestRuntimeGrowlVfxPrewarm.cpp](/c:/Code/PokemonAutochess/tests/TestRuntimeGrowlVfxPrewarm.cpp)
- [TestRuntimeTackleVfxPrewarm.cpp](/c:/Code/PokemonAutochess/tests/TestRuntimeTackleVfxPrewarm.cpp)

Both currently duplicate:

- recording backend
- mesh factory
- texture factory
- cache assertions

Move shared harness code into a small test helper file only after the runtime refactor is done.

This should be the last phase because test duplication is lower-value than runtime duplication.

## What Not To Do

Avoid these traps:

- do not merge Growl and Tackle effect classes into one effect class
- do not push all startup prewarm systems into one mega-generic system
- do not combine authored VFX with particle/TailFire prewarm just because they all “warm things”
- do not centralize effect-specific ring/snapshot construction before we have at least one more authored move using the same pattern

The right abstraction level is “shared authored VFX runtime plumbing,” not “all VFX are one system.”

## Suggested Implementation Sequence

1. Extract shared authored prewarm helper.
2. Migrate Growl to it.
3. Run authored prewarm tests.
4. Migrate Tackle to it.
5. Run authored prewarm plus Tackle smoke tests.
6. Extract shared authored gameplay bridge helper.
7. Migrate Growl wrapper.
8. Run seam/interops tests.
9. Migrate Tackle wrapper.
10. Run seam/interops plus gameplay VFX checks.
11. Replace startup authored-VFX callback fan-out with registry.
12. Run startup/config tests.
13. Clean up duplicated test harness code.

## Minimum Test Matrix Per Refactor Step

Run at least these after each step touching authored VFX:

- `shared_authored_vfx_interop_contract`
- `shared_authored_vfx_batches_contract`

Run these when touching prewarm:

- `runtime_growl_vfx_prewarm_contract`
- `runtime_tackle_vfx_prewarm_contract`
- `runtime_startup_asset_prewarm_contract`
- `session_render_config_contract`

Run these when touching gameplay bridge behavior:

- `projected_world_scene_seams_contract`
- `tackle_smoke_vfx_contract`

Run these when touching startup routing:

- `runtime_startup_asset_prewarm_contract`
- `session_render_config_contract`

## Success Criteria

We should consider the DRY pass successful when:

- adding a new authored move VFX does not require creating a dedicated prewarm execution file
- adding a new authored move VFX does not require duplicating most of a gameplay bridge file
- startup authored-VFX registration happens through one centralized list
- effect-specific behavior still lives in thin wrappers or manifests
- all authored VFX tests still pass unchanged or become simpler

## My Recommendation

Do Phases 1 and 2 next.

That is the best return on effort:

- biggest duplication removed
- lowest behavior risk
- strongest existing tests

Phase 3 is worth doing after that, but only once the shared authored prewarm/bridge helpers exist and have proven stable.
