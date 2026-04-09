# Repo DRY Roadmap

Status: Active  
Type: Roadmap  
Last updated: 2026-04-08

This document is the repo-wide “don’t repeat yourself” follow-up to the
authored VFX cleanup work. It is intentionally narrower than the general
cleanup roadmap: this one is only about duplicated structures, mirrored glue,
and opportunities to centralize repeated patterns without flattening useful
ownership boundaries.

## Scope

This roadmap is based on:

- a repo-wide scan over `src/` and `tests/`
- a directory concentration pass
- a lightweight file-similarity pass over `744` code files
- direct manual review of the strongest candidate duplicate families

This is not a mandate to merge everything that looks similar. The goal is:

- remove high-friction duplication
- keep meaningful ownership seams
- avoid introducing generic frameworks before we have enough real use cases

## What We Just Finished

The authored VFX family is already materially cleaner now:

- shared authored prewarm core
- shared authored gameplay bridge core
- startup authored-VFX registry
- shared authored prewarm test harness
- shared preview controller base
- shared controller-backed preview effect wrapper

Those changes live around:

- [SharedAuthoredVfxPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/vfx/authored/SharedAuthoredVfxPrewarm.cpp)
- [SharedProjectedWorldAuthoredVfxBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/shared/projected/world_vfx/SharedProjectedWorldAuthoredVfxBridge.cpp)
- [RuntimeStartupAssetPrewarm.cpp](/c:/Code/PokemonAutochess/src/game/runtime/startup/RuntimeStartupAssetPrewarm.cpp)
- [TestAuthoredVfxPrewarmHarness.h](/c:/Code/PokemonAutochess/tests/TestAuthoredVfxPrewarmHarness.h)
- [SharedPreviewControllerBase.h](/c:/Code/PokemonAutochess/src/vfx/preview/shared/SharedPreviewControllerBase.h)
- [ControllerBackedPreviewEffect.h](/c:/Code/PokemonAutochess/src/vfx/preview/shared/ControllerBackedPreviewEffect.h)

So this roadmap starts from there instead of repeating that work.

## Repo-Wide Read

The strongest broad signals from the scan were:

- `tests/` is the single largest directory family by file count
- `src/game/runtime/session` is still a major concentration point
- `Bridge`, `Prewarm`, `Preview`, `Renderer`, and `Session` are all heavily repeated patterns
- some repeated families are genuinely mirrored
- others are only similar because they live in the same domain and should stay separate

### Highest-Signal Repeated Families

These were the strongest believable duplicate groups after manual inspection:

1. VFX preview controllers
2. Preview effect wrappers for game/lab
3. Simple particle-effect wrapper classes in `src/game/vfx/`
4. Startup/session bridge fan-out patterns
5. Repeated test harness setup around runtime/presentation/prewarm seams

### Areas That Look Similar But Should Not Be Merged Yet

- backend mega-files
- projected backend mesh pipeline helpers
- Tail Fire exact CPU files
- renderer backends across `OpenGL` and `D3D12`

Those areas need decomposition and ownership cleanup, but not necessarily DRY
centralization right now.

## High-Confidence Next Targets

### 1. Preview Controllers

Strong candidate pair:

- [GrowlPreviewController.cpp](/c:/Code/PokemonAutochess/src/vfx/preview/growl/GrowlPreviewController.cpp)
- [TacklePreviewController.cpp](/c:/Code/PokemonAutochess/src/vfx/preview/tackle/TacklePreviewController.cpp)

Why this is real duplication:

- same fixed-step accumulator loop
- same captured-scene replay/reset logic
- same manifest hot-reload polling
- same renderer lifecycle (`render`, `onResize`)
- same logging pattern

What is actually different:

- effect/config type
- emit behavior
- active-count call
- one render call uses `effect_`, the other `effect_.sharedWave()`
- Tackle uses impact point semantics

Recommended refactor:

- introduce one shared controller base or helper for:
  - capture/replay/reset
  - fixed-step update
  - manifest file hot-reload
  - preview renderer ownership
- keep effect-specific emit/render/count hooks in the concrete controllers

Do not do:

- one giant templated preview controller with many boolean branches

Best shape:

- a small non-virtual helper or slim base class that owns the repeated loop/state
- concrete Growl/Tackle controllers remain tiny and readable

### 2. Preview Effect Wrappers

Strong families:

- [GrowlPreviewEffect.h](/c:/Code/PokemonAutochess/src/game/preview/effects/GrowlPreviewEffect.h)
- [TacklePreviewEffect.h](/c:/Code/PokemonAutochess/src/game/preview/effects/TacklePreviewEffect.h)
- [GrowlLabPreviewEffect.h](/c:/Code/PokemonAutochess/src/vfx/preview/effects/GrowlLabPreviewEffect.h)
- [TackleLabPreviewEffect.h](/c:/Code/PokemonAutochess/src/vfx/preview/effects/TackleLabPreviewEffect.h)

Why this is real duplication:

- almost all methods are controller-forwarding adapters
- game and lab versions differ mostly in a few extra preview traits
- the filesystem split is useful, but the per-effect wrappers are still repetitive

Recommended refactor:

- create a shared controller-backed preview effect wrapper for common forwarding
- let each concrete effect override only the traits that differ:
  - effect name
  - overlay lines
  - optional focus frame
  - optional caster motion / species / exact clip flags

Best shape:

- one reusable forwarding wrapper under `engine/tools/vfx_preview/` or `vfx/preview/shared/`
- very small derived types for specific effect metadata

### 3. Small `ParticleSystem`-Backed Game VFX Classes

Representative files:

- [HealPlusVFX.h](/c:/Code/PokemonAutochess/src/game/vfx/HealPlusVFX.h)
- [HealPlusVFX.cpp](/c:/Code/PokemonAutochess/src/game/vfx/HealPlusVFX.cpp)
- [LeechSeedDrainVFX.h](/c:/Code/PokemonAutochess/src/game/vfx/LeechSeedDrainVFX.h)
- [LeechSeedDrainVFX.cpp](/c:/Code/PokemonAutochess/src/game/vfx/LeechSeedDrainVFX.cpp)
- [GrassImpactVFX.h](/c:/Code/PokemonAutochess/src/game/vfx/GrassImpactVFX.h)
- [ClawSwipeVFX.h](/c:/Code/PokemonAutochess/src/game/vfx/ClawSwipeVFX.h)

Why this is real duplication:

- repeated `ParticleSystem` ownership
- repeated `configured` flag pattern
- repeated shader/render/update setup in `ensureConfigured()`
- repeated RNG helpers (`rand01`, `randRange`)
- repeated `update`, `render`, `getParticles`

What is different:

- spawn geometry
- acceleration/velocity formula
- emission entrypoint shape
- fragment shader path and tuning defaults

Recommended refactor:

- create one tiny shared helper for “simple particle effect with `ParticleSystem`”
- centralize:
  - `ensureConfigured` boilerplate
  - RNG helpers
  - common render/update forwarding
- keep per-effect emission logic local

Best shape:

- either a small composition helper used by these classes
- or a narrow base class only for this specific family

Avoid:

- moving emission logic into generic lambdas everywhere
- creating a giant particle DSL for small gameplay effects

### 4. Session/Startup Bridge Fan-Out Beyond Authored VFX

Good files to watch:

- [SessionBackendAssetBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionBackendAssetBridge.cpp)
- [SessionStartupBridge.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionStartupBridge.cpp)
- [SessionStartupRuntime.cpp](/c:/Code/PokemonAutochess/src/game/runtime/session/SessionStartupRuntime.cpp)

What improved already:

- authored VFX startup now uses a registry-style loop

What still repeats:

- one-off bridge lambdas for each startup subsystem
- per-subsystem callback plumbing
- repeated pattern of:
  - backend enable/config gate
  - bridge lambda
  - startup title/progress/log wiring

Recommendation:

- stop here for now on runtime code unless a third or fourth authored/prewarm
  family appears
- if more families arrive, extend the “small registry” idea by subsystem
- do not try to centralize all startup work into one mega-registry yet

This is a medium-confidence future DRY target, not the next immediate one.

### 5. Repeated Test Harness Utilities

What we already did:

- authored VFX prewarm harness is shared now

What likely remains:

- small fake render backends
- env-var scaffolding
- startup/runtime harness builders
- preview controller/effect harness setup

Recommendation:

- do another targeted pass only when a second clear pair emerges
- prefer tiny shared test headers over sprawling `TestUtils.cpp` files
- keep the helpers local to one domain

Good future candidates:

- startup/session config tests
- preview/controller tests if more authored effects are added

## Lower-Priority Or “Do Not DRY Yet” Areas

### 1. Backend Renderer Files

Examples:

- `src/engine/render/opengl/*`
- `src/engine/render/d3d12/*`

These files are large and related, but the right operation is decomposition and
interface cleanup, not DRY merging.

### 2. Projected Backend Mesh Helpers

Examples:

- `src/game/runtime/shared/projected/backend_mesh/*`

These are still concentration hotspots, but they are specialized enough that a
similarity score alone is not evidence for safe centralization.

### 3. Tail Fire Exact CPU Files

Examples:

- [SharedTailFireExactCpuSnapshotBatches.h](/c:/Code/PokemonAutochess/src/game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuSnapshotBatches.h)
- [SharedTailFireExactCpuTileBake.h](/c:/Code/PokemonAutochess/src/game/runtime/shared/vfx/tail_fire/SharedTailFireExactCpuTileBake.h)

These look similar structurally because they are in the same subsystem, but
they likely represent neighboring steps rather than obvious duplication.

## Recommended Order

### Phase A: Preview DRY Pass

Status: Completed on 2026-04-08

Focus:

- preview controllers
- preview effect wrappers

Why first:

- highest confidence after the authored-VFX work
- low runtime risk
- strong readability payoff

Suggested exit criteria:

- Growl/Tackle preview controllers lose most loop/hot-reload duplication
- game/lab preview effect wrappers become thin metadata shells

### Phase B: Small Particle-Effect Helper Pass

Focus:

- `src/game/vfx/` simple `ParticleSystem`-backed classes

Why second:

- medium implementation effort
- good payoff
- bounded blast radius

Suggested exit criteria:

- common setup/update/render/RNG boilerplate is centralized
- individual emit formulas remain local and readable

### Phase C: Targeted Test-Harness Passes

Focus:

- only after another obvious duplicate pair is confirmed

Why third:

- lower payoff than runtime/preview cleanup
- good follow-up once runtime seams are stable

### Phase D: Broader Startup Registry Revisit

Focus:

- only if new startup prewarm families continue to accumulate

Why last:

- easy to over-abstract too early
- better informed once the repo has more real repeated cases

## Test Guardrails For DRY Work

### For Preview Refactors

Run:

- `shared_authored_vfx_batches_contract`
- `tackle_smoke_vfx_contract`
- any preview-specific tests already present

Also do a quick manual smoke in:

- [VfxLab.exe](/c:/Code/PokemonAutochess/build/Debug/VfxLab.exe)
- [PAC_VfxPreviewer.exe](/c:/Code/PokemonAutochess/build/Debug/PAC_VfxPreviewer.exe)

### For Particle-Effect Helper Refactors

Run:

- relevant gameplay or VFX tests touching those effects
- any startup/runtime smoke that exercises them

### For Startup/Bridge Refactors

Run:

- `runtime_startup_asset_prewarm_contract`
- `session_render_config_contract`
- `projected_world_scene_seams_contract`

### For Test-Only DRY Passes

Run:

- the directly touched test contracts
- at least one neighboring contract in the same subsystem

## Decision Rules

Before DRYing a family, ask:

1. Is the repeated code really the same responsibility?
2. Are we centralizing mechanism, not policy?
3. Will the shared helper stay smaller than the duplication it removes?
4. Do we have tests that will catch behavioral drift?

If the answer to any of those is “not really,” defer the DRY pass.

## Current Recommendation

If we continue this DRY track, the best next repo-wide target is:

1. Preview controllers
2. Preview effect wrappers
3. Small `ParticleSystem`-backed gameplay VFX classes

That order gives the best maintainability win without walking into the trap of
over-generalizing the renderer or startup/runtime seams.
