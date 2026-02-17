#pragma once

#include "engine/input/InputEvent.h"

#include <cstddef>

namespace game::runtime::backend_input {

inline int slotFromNumberKey(InputEvent::Key keyId) {
    switch (keyId) {
        case InputEvent::Key::Num1: return 1;
        case InputEvent::Key::Num2: return 2;
        case InputEvent::Key::Num3: return 3;
        case InputEvent::Key::Num4: return 4;
        case InputEvent::Key::Num5: return 5;
        case InputEvent::Key::Num6: return 6;
        case InputEvent::Key::Num7: return 7;
        case InputEvent::Key::Num8: return 8;
        case InputEvent::Key::Num9: return 9;
        default:
            return -1;
    }
}

inline int inventoryIndexFromSlot(int slot, std::size_t visibleCount) {
    if (slot <= 0) return -1;
    const std::size_t index = static_cast<std::size_t>(slot - 1);
    if (index >= visibleCount) return -1;
    return static_cast<int>(index);
}

inline bool isClearSelectionKey(InputEvent::Key keyId) {
    return keyId == InputEvent::Key::Num0;
}

} // namespace game::runtime::backend_input
