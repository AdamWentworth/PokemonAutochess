#pragma once

#include "game/runtime/ui/InputSlots.h"

namespace game::state::backend_input {

inline int slotFromNumberKey(InputEvent::Key keyId) {
    return game::runtime::ui_input::slotFromNumberKey(keyId);
}

} // namespace game::state::backend_input



