# Docs

Status: Active
Type: Index
Last updated: 2026-07-30

This folder holds the live engineering docs for the repo. Historical or
superseded plans belong in `docs/archive/`. Live docs stay in `docs/`; their
role is expressed through metadata rather than deep folder nesting.

## Active Docs
| Doc | Type | Purpose |
| --- | --- | --- |
| `GOALS.md` | `Goal` | Stable project goals and success criteria. |
| `REPO_ASSESSMENT.md` | `Assessment` | Living high-level maintainability and repo-health read. |
| `REPO_CLEANUP_ROADMAP.md` | `Roadmap` | Ranked cleanup plan based on current repo-health findings. |
| `ENGINE_RULES.md` | `Rule` | Non-negotiable architecture guardrails. |
| `PHLOSION_ASSET_ARCHITECTURE.md` | `Architecture` | Engine-wide source, canonical IR, cooked PHRC resource, scene, and vault decisions. |
| `LGPE_ENVIRONMENT_FIDELITY_CONTRACT.md` | `Contract` | Source-first fidelity, permitted board-layout edits, direct LGPE ingestion, and validation rules. |
| `lgpe/evidence/README.md` | `Evidence` | Promoted direct-source Route 1 manifest, provenance, scope, and reproduction commands. |
| `ARENA_BACKDROP_PLAN.md` | `Roadmap` | Source-faithful arena environment integration and Route 1 implementation sequence. |
| `CPU_GPU_WORK_SPLIT.md` | `Architecture` | Current CPU/GPU ownership and projected-path decision guide. |
| `RENDERER_PARITY_CONTRACT.md` | `Contract` | Backend render-state and parity baseline. |
| `RENDERER_PARITY_ROADMAP.md` | `Roadmap` | Active renderer parity and performance roadmap. |
| `RENDER_RESTRUCTURING_OUTSTANDING.md` | `Roadmap` | Remaining deeper renderer restructuring work. |
| `RENDER_PATH_FILE_MAP.md` | `Reference` | Where runtime render behavior lives in code. |
| `PERF_DECISIONS.md` | `Reference` | Durable performance lessons and decision rules. |
| `TEST_PLAN.md` | `Runbook` | Validation protocol for correctness, perf, and tooling. |
| `TECH_DEBT.md` | `Tracker` | Short strategic debt list. |
| `OUTSTANDING_ISSUES.md` | `Tracker` | Concrete maintainability and ownership issues. |
| `DISPLAY_GRAPHICS_ROADMAP.md` | `Roadmap` | Display/settings roadmap grounded in current implementation. |
| `CI.md` | `Runbook` | CI scope and local equivalent checks. |
| `VFX_PIPELINE.md` | `Architecture` | Current reusable vs game-specific VFX ownership and asset rules. |

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
