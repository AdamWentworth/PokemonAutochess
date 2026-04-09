#include <string>

#include <glm/glm.hpp>

#include "game/vfx/AquaSwooshVFX.h"
#include "game/vfx/ClawSwipeVFX.h"
#include "game/vfx/GrassImpactVFX.h"
#include "game/vfx/HealPlusVFX.h"
#include "game/vfx/LeechSeedDrainVFX.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_gameplay_particle_vfx_emit_contract(std::string& outFail) {
    {
        HealPlusVFX vfx;
        HealPlusVFX::Config cfg;
        cfg.minParticles = 3;
        cfg.maxParticles = 3;
        vfx.setConfig(cfg);
        vfx.emitAt(glm::vec3(0.0f));
        if (!expect(vfx.getParticles().particleCount() == 3u,
                    "HealPlusVFX should honor fixed particle counts after the shared helper refactor.",
                    outFail)) {
            return false;
        }
    }

    {
        LeechSeedDrainVFX vfx;
        LeechSeedDrainVFX::Config cfg;
        cfg.minParticles = 2;
        cfg.maxParticles = 2;
        vfx.setConfig(cfg);
        vfx.emitBetween(glm::vec3(0.0f), glm::vec3(1.0f, 0.5f, 0.0f), 0.35f);
        if (!expect(vfx.getParticles().particleCount() == 2u,
                    "LeechSeedDrainVFX should preserve fixed emit counts after the shared helper refactor.",
                    outFail)) {
            return false;
        }
    }

    {
        GrassImpactVFX vfx;
        GrassImpactVFX::Config cfg;
        cfg.minParticles = 4;
        cfg.maxParticles = 4;
        vfx.setConfig(cfg);
        vfx.emitAt(glm::vec3(0.0f));
        if (!expect(vfx.getParticles().particleCount() == 4u,
                    "GrassImpactVFX should preserve fixed emit counts after the shared helper refactor.",
                    outFail)) {
            return false;
        }
    }

    {
        ClawSwipeVFX vfx;
        ClawSwipeVFX::Config cfg;
        cfg.minParticles = 5;
        cfg.maxParticles = 5;
        vfx.setConfig(cfg);
        vfx.emitAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), false);
        if (!expect(vfx.getParticles().particleCount() == 1u,
                    "Non-metallic ClawSwipeVFX should still collapse to a single explicit scratch mark.",
                    outFail)) {
            return false;
        }
    }

    {
        AquaSwooshVFX vfx;
        vfx.emitAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), AquaSwooshVFX::Style::WaterGun);
        if (!expect(vfx.getParticles().particleCount() >= 14u &&
                        vfx.getParticles().particleCount() <= 22u,
                    "AquaSwooshVFX should preserve its style-specific particle-count envelope.",
                    outFail)) {
            return false;
        }
    }

    return true;
}
