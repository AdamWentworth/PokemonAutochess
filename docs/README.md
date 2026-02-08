# Docs

These are **internal notes** for quality, architecture, and testing. They are not meant to be marketing copy.

## Index
| Doc | Purpose |
| --- | --- |
| `GOALS.md` | Portfolio and engine reuse goals. |
| `ENGINE_RULES.md` | Non-negotiable architecture rules. |
| `TEST_PLAN.md` | Test coverage goals and next additions. |
| `TECH_DEBT.md` | Known tech debt and smells to keep in mind. |
| `CI.md` | What CI runs and how to mirror it locally. |

## Update Policy
- Keep these docs small and current.
- If something becomes outdated, delete or rewrite it instead of letting it drift.

## Current Focus (Next 3)
- ECS adoption for movement/combat/round systems.
- UI sizing via a shared viewport service.
- Render a real model in the GL smoke test.
