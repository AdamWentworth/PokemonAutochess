#include "game/runtime/session/SessionSnapshotRuntime.h"

#include <string>

bool test_session_snapshot_runtime_contract(std::string& outFail) {
    using game::runtime::session_debug_snapshot::SessionSnapshotMetadata;
    using game::runtime::session_snapshot_runtime::RuntimeFlags;
    using game::runtime::session_snapshot_runtime::resolveRuntimeFlags;

    {
        SessionSnapshotMetadata session;
        session.hasCombatActive = true;
        session.combatActive = false;
        session.hasRoundPhase = true;
        session.roundPhase = RoundPhase::Resolution;

        const RuntimeFlags flags = resolveRuntimeFlags(session, true);
        if (!flags.combatActive || flags.phase != RoundPhase::Battle) {
            outFail = "SessionSnapshotRuntime should force battle flags when enemy units require combat restore.";
            return false;
        }
    }

    {
        SessionSnapshotMetadata session;
        session.hasCombatActive = true;
        session.combatActive = false;
        session.hasRoundPhase = true;
        session.roundPhase = RoundPhase::Resolution;

        const RuntimeFlags flags = resolveRuntimeFlags(session, false);
        if (flags.combatActive || flags.phase != RoundPhase::Resolution) {
            outFail = "SessionSnapshotRuntime should honor saved combat and round metadata when combat restore is not forced.";
            return false;
        }
    }

    {
        SessionSnapshotMetadata session;
        const RuntimeFlags flags = resolveRuntimeFlags(session, false);
        if (flags.combatActive || flags.phase != RoundPhase::Planning) {
            outFail = "SessionSnapshotRuntime should default missing metadata to planning and combat inactive.";
            return false;
        }
    }

    return true;
}
