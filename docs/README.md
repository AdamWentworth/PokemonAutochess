# Docs

These are internal engineering docs. Keep them decision-oriented and current.

## Index
| Doc | Purpose |
| --- | --- |
| `GOALS.md` | Project goals and explicit rendering/performance success criteria. |
| `ENGINE_RULES.md` | Architecture guardrails that are not negotiable. |
| `RENDERER_PARITY_ROADMAP.md` | Primary pre-merge roadmap for OpenGL/D3D12 parity and performance readiness. |
| `PARITY_OUTSTANDING.md` | Short blocker list that must be closed before merging D3D12 work to `master`. |
| `RENDER_PATH_FILE_MAP.md` | Where rendering behavior lives in code (shared path, backend implementations, settings). |
| `TEST_PLAN.md` | Automated and manual validation strategy, including Release benchmark protocol. |
| `TECH_DEBT.md` | Highest-value debt list for rendering/runtime stability and performance. |
| `HOUSEWORK_ROADMAP.md` | Post-merge cleanup and optimization sequence after merge gates are satisfied. |
| `CI.md` | CI scope and local equivalent checks. |
| `VFX_PIPELINE.md` | Data-driven VFX authoring and asset contracts. |

## Rendering Program Focus (as of 2026-02-28)
1. Merge D3D12 changes only after objective parity/performance gates pass.
2. Make performance telemetry trustworthy (Release-only, CPU/GPU/present split).
3. Remove user-facing settings ambiguity (placeholder toggles and stale backend logs).

## Update Policy
- Put concrete dates on status sections.
- Replace stale plans instead of appending long historical logs.
- Keep blockers and acceptance criteria explicit.
