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
- Compatibility functions removed.
- No runtime call sites remain.

Replacement:
- Use LogBus::Logger instance passed via GameServices or CoreServices.

---

## Next Migration Slice

- None for LogBus (completed). Next focus: remove any remaining legacy adapters in runtime systems.
