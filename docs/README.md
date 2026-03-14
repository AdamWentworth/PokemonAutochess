# Docs

These are internal engineering docs. Keep them current, decision-oriented, and
short enough to stay useful.

## Index
| Doc | Purpose |
| --- | --- |
| `GOALS.md` | Stable project goals and success criteria. |
| `ENGINE_RULES.md` | Non-negotiable architecture guardrails. |
| `REPO_ASSESSMENT.md` | Current repo-wide quality snapshot and rating. |
| `RENDERER_PARITY_ROADMAP.md` | Current renderer parity/performance roadmap and priority order. |
| `RENDER_PATH_FILE_MAP.md` | Where rendering behavior lives in code. |
| `RENDERER_PARITY_CONTRACT.md` | Backend render-state and color-pipeline contract. |
| `CPU_GPU_WORK_SPLIT.md` | Stable CPU vs GPU responsibility guide. |
| `PERF_EXPERIMENT_NOTES.md` | Short log of validated and rejected perf hypotheses. |
| `TEST_PLAN.md` | Validation and benchmark protocol. |
| `TECH_DEBT.md` | Active high-value debt list. |
| `DISPLAY_GRAPHICS_ROADMAP.md` | User-facing display/graphics settings roadmap. |
| `CI.md` | CI scope and local equivalent checks. |
| `VFX_PIPELINE.md` | Data-driven VFX authoring and asset contracts. |

## Current Program Focus
1. Keep runtime performance work centered on steady-state gameplay frame time.
2. Keep perf telemetry and benchmark discipline trustworthy.
3. Remove user-visible cold-path stalls without trading away runtime smoothness.
4. Keep docs, menus, and logs honest about the current engine state.

## Update Policy
- Put concrete dates on status docs.
- Replace stale plans instead of layering new ones on top.
- Prefer one current roadmap over multiple partially-overlapping ones.
- Delete docs that no longer serve a distinct purpose.
