# Repo health targets (6→7)

This repo will be treated as "7/10" when these move measurably:

- **Singleton call sites**: `getInstance()` occurrences trending down.
- **Composition boundary**: all config loading/wiring happens in `GameBootstrap` (not in runtime loop/session).

## Quick check

Run:

- `python tools/health/count_singletons.py`

Record the count in your PR description and ensure it does not increase.
