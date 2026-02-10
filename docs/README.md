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
| `VFX_PIPELINE.md` | Data-driven VFX authoring direction and folder contracts. |

## Update Policy
- Keep these docs small and current.
- If something becomes outdated, delete or rewrite it instead of letting it drift.

## Release Flow
Validated locally by running `tools/build_installer.ps1 -Bundle` in a clean clone.
Prereqs
- Visual Studio 2026 or newer
- CMake 3.22+
- vcpkg with `VCPKG_ROOT` set
- Inno Setup 6

## Current Focus (Next 3)
- ECS adoption for remaining gameplay systems (shop/bench/camera/interaction).
- UI scale tuning across resolutions.
- Packaged build smoke checklist automation (optional).
