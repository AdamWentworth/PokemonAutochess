#pragma once

#include "game/runtime/backend_ui/BackendInputSlots.h"

namespace game::state::backend_input {

inline int slotFromNumberKey(InputEvent::Key keyId) {
    return game::runtime::backend_input::slotFromNumberKey(keyId);
}

} // namespace game::state::backend_input

