// src/engine/core/ISystem.h
#pragma once

/*
    Compatibility shim.

    Some game systems historically included engine/core/ISystem.h.
    The engine's "system" interface is currently IUpdatable.

    Keeping this header avoids churn and keeps systems consistent.
*/

#include "IUpdatable.h"

// For now, a "system" is simply something updatable by the scheduler.
using ISystem = IUpdatable;
