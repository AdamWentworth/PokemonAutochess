#include "game/state/BackendInputSlots.h"
#include "game/runtime/backend_ui/BackendInputSlots.h"

#include <string>

bool test_backend_input_slots_contract(std::string& outFail) {
    using game::state::backend_input::slotFromNumberKey;

    if (slotFromNumberKey(InputEvent::Key::Num1) != 1) {
        outFail = "Num1 should map to slot 1";
        return false;
    }
    if (slotFromNumberKey(InputEvent::Key::Num5) != 5) {
        outFail = "Num5 should map to slot 5";
        return false;
    }
    if (slotFromNumberKey(InputEvent::Key::Num9) != 9) {
        outFail = "Num9 should map to slot 9";
        return false;
    }
    if (slotFromNumberKey(InputEvent::Key::Escape) != -1) {
        outFail = "non-number key should map to -1";
        return false;
    }
    if (game::runtime::backend_input::inventoryIndexFromSlot(1, 3) != 0) {
        outFail = "slot 1 should map to index 0";
        return false;
    }
    if (game::runtime::backend_input::inventoryIndexFromSlot(3, 3) != 2) {
        outFail = "slot 3 should map to index 2";
        return false;
    }
    if (game::runtime::backend_input::inventoryIndexFromSlot(4, 3) != -1) {
        outFail = "out-of-range slot should map to -1";
        return false;
    }
    if (game::runtime::backend_input::inventoryIndexFromSlot(-1, 3) != -1) {
        outFail = "negative slot should map to -1";
        return false;
    }
    if (!game::runtime::backend_input::isClearSelectionKey(InputEvent::Key::Num0)) {
        outFail = "Num0 should map to clear-selection key";
        return false;
    }
    if (game::runtime::backend_input::isClearSelectionKey(InputEvent::Key::Num1)) {
        outFail = "Num1 should not map to clear-selection key";
        return false;
    }
    if (game::runtime::backend_input::inventoryOffsetDeltaFromKey(InputEvent::Key::Up, 6) != -1) {
        outFail = "Up key should map to -1 inventory offset delta";
        return false;
    }
    if (game::runtime::backend_input::inventoryOffsetDeltaFromKey(InputEvent::Key::Down, 6) != 1) {
        outFail = "Down key should map to +1 inventory offset delta";
        return false;
    }
    if (game::runtime::backend_input::inventoryOffsetDeltaFromKey(InputEvent::Key::Left, 6) != -6) {
        outFail = "Left key should map to negative page-step inventory offset delta";
        return false;
    }
    if (game::runtime::backend_input::inventoryOffsetDeltaFromKey(InputEvent::Key::Right, 6) != 6) {
        outFail = "Right key should map to positive page-step inventory offset delta";
        return false;
    }
    if (game::runtime::backend_input::inventoryOffsetDeltaFromKey(InputEvent::Key::Right, 0) != 1) {
        outFail = "inventory page-step should clamp to at least 1";
        return false;
    }
    if (game::runtime::backend_input::inventoryOffsetDeltaFromKey(InputEvent::Key::A, 6) != 0) {
        outFail = "non-navigation keys should not change inventory offset";
        return false;
    }

    return true;
}

