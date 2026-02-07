# Delete/Replace List (Globals and Singletons)

Last updated: 2026-02-07

Purpose: Track remaining global or singleton patterns and their replacements.

---

## Current Count

- getInstance() call sites: 0

---

## Inventory

### EventManager singleton

Status:
- Removed. No call sites.

Replacement:
- Use engine::CoreServices.events or EngineServices.events.

---

### LogBus active logger compatibility

Files:
- src/game/logging/LogBus.h
- src/game/logging/LogBus.cpp

Status:
- No call sites in runtime code.
- Compatibility functions and thread-local active logger still exist.

Replacement:
- Use LogBus::Logger instance passed via GameServices or CoreServices.

---

## Next Migration Slice

- Delete LogBus compatibility functions and thread-local active logger.
- Require explicit Logger references in runtime helpers.
