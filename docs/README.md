<p align="center">
  <a href="../README.md"><img src="assets/readme/autochess-lockup.png" alt="Pokemon Autochess" width="420"></a>
</p>

# Pokemon Autochess Documentation

Status: Active
Type: Index
Last updated: 2026-09-19

This folder holds the live engineering docs for the repo. Historical or
superseded plans belong in `docs/archive/`. Live docs stay in `docs/`; their
role is expressed through metadata rather than deep folder nesting.

## Start Here

- **Build or run the project:** [Development guide](DEVELOPMENT.md).
- **Review the architecture:** [Game and engine boundaries](PROJECT_BOUNDARIES.md),
  [editor scene model](EDITOR_SCENE_MODEL.md), and [character materials](CHARACTER_MATERIALS.md).
- **Check the evidence:** [Test plan](TEST_PLAN.md), [renderer parity contract](RENDERER_PARITY_CONTRACT.md),
  and [material verification results](CHARACTER_MATERIALS.md#boundary-verification-2026-09-19).
- **Refresh the showcase:** [README branding and media](DEMO_MEDIA_CAPTURE.md#readme-branding-and-showcase).

## Active Docs
| Doc | Type | Purpose |
| --- | --- | --- |
| [DEVELOPMENT.md](DEVELOPMENT.md) | `Runbook` | Setup, build targets, editor pairing, debugging, packaging and developer tools. |
| [PROJECT_BOUNDARIES.md](PROJECT_BOUNDARIES.md) | `Contract` | Ownership of the game, engine, packages, material programs and research tools. |
| [DEMO_MEDIA_CAPTURE.md](DEMO_MEDIA_CAPTURE.md) | `Runbook` | Branding provenance, selected README images and repeatable screenshot/video capture. |
| [GOALS.md](GOALS.md) | `Goal` | Stable project goals and success criteria. |
| [REPO_ASSESSMENT.md](REPO_ASSESSMENT.md) | `Assessment` | Living high-level maintainability and repo-health read. |
| [REPO_CLEANUP_ROADMAP.md](REPO_CLEANUP_ROADMAP.md) | `Roadmap` | Current maintenance scope, completed boundaries and deferred decisions. |
| [ENGINE_RULES.md](ENGINE_RULES.md) | `Rule` | Non-negotiable architecture guardrails. |
| [PHLOSION_ASSET_ARCHITECTURE.md](PHLOSION_ASSET_ARCHITECTURE.md) | `Architecture` | Engine-wide source, canonical IR, cooked PHRC resource, scene, and vault decisions. |
| [PHLOSION_ASSET_MIGRATION.md](PHLOSION_ASSET_MIGRATION.md) | `Runbook` | Current Forge cook, strict gameplay proof, compatibility boundaries, and promotion gates. |
| [EXTERNAL_ASSET_RESEARCH.md](EXTERNAL_ASSET_RESEARCH.md) | `Architecture` | Boundary between the game, private research workspace, and private asset depot. |
| [BLENDER_ARENA_PILOT.md](BLENDER_ARENA_PILOT.md) | `Runbook` | Current south-entrance authoring, export, recovery, and verification. |
| [BLENDER_SOUTH_CLEARING.md](BLENDER_SOUTH_CLEARING.md) | `Runbook` | South Clearing layout, Blender editing, preview, recovery, and qualification. |
| [BLENDER_NORTH_TERRACES.md](BLENDER_NORTH_TERRACES.md) | `Runbook` | Third Route 1 arena farther north, source blueprint, editing, preview and qualification. |
| [BLENDER_NORTH_ENTRANCE.md](BLENDER_NORTH_ENTRANCE.md) | `Runbook` | Final Route 1 arena, northern ramp and grass layout, editing, preview and qualification. |
| [STARTER_LAB_BACKDROP.md](STARTER_LAB_BACKDROP.md) | `Runbook` | Editable Oak's Lab frontend backdrop, rendering, publication and starter preview. |
| [POKEMON_HUD_PORTRAITS.md](POKEMON_HUD_PORTRAITS.md) | `Runbook` | Original TCG card art and face portraits for all 151 Kanto species, catalog sources, framing and validation. |
| [ARENA_TRAVEL.md](ARENA_TRAVEL.md) | `Runbook` | Replayable team recall, covered arena change and formation-preserving send-out. |
| [ENCOUNTER_GRASS.md](ENCOUNTER_GRASS.md) | `Contract` | Grass sight, attack reveals, search patrols, interaction and editor checks. |
| [COMBAT_MOVEMENT.md](COMBAT_MOVEMENT.md) | `Contract` | Step reservations and deterministic combat movement. |
| [AUTHORED_ARENA_MAP.md](AUTHORED_ARENA_MAP.md) | `Contract` | Authored cells, directed height connections, and encounter-grass regions. |
| [EDITOR_GAMEPLAY_RELOAD.md](EDITOR_GAMEPLAY_RELOAD.md) | `Runbook` | C++ rebuild and reload while the editor remains open. |
| [CPU_GPU_WORK_SPLIT.md](CPU_GPU_WORK_SPLIT.md) | `Architecture` | Current CPU/GPU ownership and projected-path decision guide. |
| [RENDERER_PARITY_CONTRACT.md](RENDERER_PARITY_CONTRACT.md) | `Contract` | Backend render-state and parity baseline. |
| [RENDERER_PARITY_ROADMAP.md](RENDERER_PARITY_ROADMAP.md) | `Roadmap` | Renderer capabilities, dated qualification and conditional future performance work. |
| [RENDER_RESTRUCTURING_OUTSTANDING.md](RENDER_RESTRUCTURING_OUTSTANDING.md) | `Roadmap` | Deferred renderer candidates and required three-API verification. |
| [RENDER_PATH_FILE_MAP.md](RENDER_PATH_FILE_MAP.md) | `Reference` | Where runtime render behavior lives in code. |
| [FIELD_MATERIALS.md](FIELD_MATERIALS.md) | `Reference` | Game-owned field shaders, material profiles, provenance, and verification. |
| [CHARACTER_MATERIALS.md](CHARACTER_MATERIALS.md) | `Reference` | Character and fire programs, source provenance, engine boundary and renderer verification. |
| [PERF_DECISIONS.md](PERF_DECISIONS.md) | `Reference` | Durable performance lessons and decision rules. |
| [TEST_PLAN.md](TEST_PLAN.md) | `Runbook` | Validation protocol for correctness, perf, and tooling. |
| [TECH_DEBT.md](TECH_DEBT.md) | `Tracker` | Short strategic debt list. |
| [OUTSTANDING_ISSUES.md](OUTSTANDING_ISSUES.md) | `Tracker` | Concrete maintainability and ownership issues. |
| [DISPLAY_GRAPHICS_ROADMAP.md](DISPLAY_GRAPHICS_ROADMAP.md) | `Roadmap` | Display/settings roadmap grounded in current implementation. |
| [CI.md](CI.md) | `Runbook` | CI scope and local equivalent checks. |
| [VFX_PIPELINE.md](VFX_PIPELINE.md) | `Architecture` | Current reusable vs game-specific VFX ownership and asset rules. |
| [EDITOR_TOOLING.md](EDITOR_TOOLING.md) | `Architecture` | PokemonAutochess-owned Phlosion Editor extension behavior and persistence. |
| [TERRAIN_PATCH_COOKER_V2.md](TERRAIN_PATCH_COOKER_V2.md) | `Architecture` | Regional terrain-preview topology, validation, and promotion rules. |
| [LGPE_TERRAIN_PIECE_WORKFLOW.md](LGPE_TERRAIN_PIECE_WORKFLOW.md) | `Architecture` | Complete exact-source donor catalog, socket matching, and production bake boundary. |

## Type Guide
- `Goal`: Long-lived direction and success criteria.
- `Assessment`: Living high-level read of repo health or system quality.
- `Rule`: Non-negotiable engineering guardrails.
- `Contract`: Stable, testable baseline that should not drift accidentally.
- `Architecture`: Current ownership model, system structure, or decision guide.
- `Reference`: Navigation aid or source-of-truth map of where behavior lives.
- `Evidence`: Promoted deterministic outputs and the provenance needed to
  reproduce them.
- `Roadmap`: Active priorities, next steps, and staged cleanup work.
- `Runbook`: How to operate, validate, or run the repo/tooling correctly.
- `Tracker`: Debt and issue lists that need follow-through.
- `Journal`: Experiment history, lessons learned, and rejected ideas.
- `Index`: Top-level map of the documentation set itself.

## Archive
- `docs/archive/` holds historical plans, assessments, and superseded designs.
- Archived docs should explain why they were retired and which active doc now
  owns the topic.

## Live Doc Contract
- Every live doc starts with `Status:`, `Type:`, and `Last updated:`.
- Keep only current source-of-truth docs in `docs/`.
- Move superseded plans to `docs/archive/`; do not leave them in the active
  index once they stop driving work.
- Prefer one current roadmap per topic plus one issue register over multiple
  overlapping plans on the same topic.
