#include "game/state/BackendInputSlots.h"

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

    return true;
}

