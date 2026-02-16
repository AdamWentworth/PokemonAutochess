#include "game/world/GameWorld.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/core/Random.h"
#include "engine/utils/ResourceManager.h"

#include "game/GameConfig.h"

#include "game/config/GameDataDb.h"
#include "game/config/PokemonConfigLoader.h"

#include "game/logging/LogBus.h"

namespace {

std::string capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

}  // namespace

bool GameWorld::startCaptureAttempt(int targetId, float ballMult, const glm::vec3* throwOrigin) {
    auto* target = findUnitById(targetId);
    if (!target) return false;
    if (target->side != PokemonSide::Enemy) return false;
    if (target->captureInProgress) return false;
    if (!target->alive && !target->fainting) return false;  // already gone

    const PokemonStats* stats = data ? data->pokemon.getStats(target->name) : nullptr;
    const float baseRate = stats ? stats->catchRate : 0.0f;
    if (baseRate <= 0.0f) return false;

    float hpFrac = 1.0f;
    if (target->maxHP > 0) {
        hpFrac = std::clamp(static_cast<float>(target->hp) / static_cast<float>(target->maxHP), 0.0f, 1.0f);
    }
    const float hpFactorRange = std::max(0.0f, config.captureHpFactorMax - config.captureHpFactorMin);
    float hpFactor = config.captureHpFactorMin + (1.0f - hpFrac) * hpFactorRange;
    if (target->fainting || (!target->alive && target->fainting)) {
        hpFactor *= std::max(0.0f, config.captureFaintBonus);
    }

    float chance = baseRate * std::max(0.0f, ballMult) * hpFactor;
    chance = std::clamp(chance, config.captureMinChance, config.captureMaxChance);

    // Shake logic: three checks with p = chance^(1/3)
    const float shakeP = std::pow(chance, 1.0f / 3.0f);
    int shakes = 0;
    auto roll = [&](float p) {
        if (rng) return engine::random::nextFloat01(*rng) <= p;
        return (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) <= p;
    };

    for (int i = 0; i < 3; ++i) {
        if (roll(shakeP)) {
            ++shakes;
        } else {
            break;
        }
    }

    const bool success = (shakes >= 3);

    target->captureInProgress = true;
    target->captureScale = 1.0f;
    target->captureTintStrength = 0.0f;
    target->isMoving = false;
    target->committedDest = {-1, -1};
    target->attackTimerSec = 0.0f;

    ensurePokeballModel();

    if (log) {
        log->catchInfo("Threw pokeball at " + capitalize(target->name) +
            " (Lv" + std::to_string(target->level) + ")");
    }

    CaptureAttempt attempt;
    attempt.targetId = target->id;
    attempt.success = success;
    attempt.shakes = shakes;
    attempt.name = target->name;
    attempt.level = target->level;
    attempt.throwDur = 0.35f;
    attempt.absorbDur = 0.35f;
    attempt.shakeDur = std::max(0.2f, config.captureAttemptSec);
    attempt.resolveDur = 0.35f;

    attempt.targetPos = target->position;
    attempt.targetPos.y = 0.15f;
    if (throwOrigin) {
        attempt.startPos = *throwOrigin;
    } else {
        attempt.startPos = target->position + glm::vec3(0.0f, 0.0f, -getBoardCellSize() * 3.0f);
        attempt.startPos.y = 0.45f;
    }
    attempt.ballPos = attempt.startPos;
    attempt.ballImpactScale = std::max(0.05f, config.captureBallScale);
    attempt.ballStartScale = std::max(attempt.ballImpactScale, config.captureBallScaleStart);
    attempt.ballBaseScale = attempt.ballImpactScale;
    attempt.ballScale = attempt.ballStartScale;
    attempt.ballYawDeg = 0.0f;
    attempt.phase = CaptureAttempt::Phase::Throw;
    attempt.phaseTime = 0.0f;
    attempt.timeLeftSec = 1.0f;

    captureAttempts.push_back(attempt);

    return true;
}

void GameWorld::updateCaptureAttempts(float dt) {
    if (captureAttempts.empty()) return;

    std::vector<int> removeIds;

    for (auto& attempt : captureAttempts) {
        attempt.phaseTime = std::max(0.0f, attempt.phaseTime + dt);
        PokemonInstance* target = findUnitById(attempt.targetId);

        switch (attempt.phase) {
            case CaptureAttempt::Phase::Throw: {
                const float dur = std::max(0.05f, attempt.throwDur);
                const float t = std::clamp(attempt.phaseTime / dur, 0.0f, 1.0f);
                attempt.ballPos = glm::mix(attempt.startPos, attempt.targetPos, t);
                const float arc = std::max(0.1f, getBoardCellSize() * 0.9f);
                attempt.ballPos.y += std::sin(3.1415926f * t) * arc;
                attempt.ballYawDeg += dt * 720.0f;
                attempt.ballScale = glm::mix(attempt.ballStartScale, attempt.ballImpactScale, t);

                if (t >= 1.0f) {
                    attempt.phase = CaptureAttempt::Phase::Absorb;
                    attempt.phaseTime = 0.0f;
                }
                break;
            }
            case CaptureAttempt::Phase::Absorb: {
                const float dur = std::max(0.05f, attempt.absorbDur);
                const float t = std::clamp(attempt.phaseTime / dur, 0.0f, 1.0f);
                attempt.ballPos = attempt.targetPos;
                attempt.ballPos.y = 0.1f;
                attempt.ballScale = attempt.ballImpactScale;

                if (target) {
                    target->captureScale = 1.0f - t;
                    target->captureTintStrength = 1.0f;
                }

                if (t >= 1.0f) {
                    attempt.phase = CaptureAttempt::Phase::Shake;
                    attempt.phaseTime = 0.0f;
                    attempt.shakesEmitted = 0;
                }
                break;
            }
            case CaptureAttempt::Phase::Shake: {
                const int totalShakes = std::max(0, attempt.shakes);
                const float perShake = std::max(0.2f, attempt.shakeDur);
                const float totalDur = (totalShakes > 0) ? (perShake * totalShakes) : perShake;

                const int currentShake = (totalShakes > 0)
                    ? std::min(totalShakes, static_cast<int>(attempt.phaseTime / perShake) + 1)
                    : 0;
                while (attempt.shakesEmitted < currentShake) {
                    attempt.shakesEmitted++;
                    if (log) {
                        if (attempt.shakesEmitted == 1) log->catchInfo("The ball shook once!");
                        if (attempt.shakesEmitted == 2) log->catchInfo("The ball shook twice!");
                        if (attempt.shakesEmitted == 3) log->catchInfo("The ball shook three times!");
                    }
                }

                const float wobble = std::sin(attempt.phaseTime * 16.0f) * (getBoardCellSize() * 0.04f);
                attempt.ballPos = attempt.targetPos;
                attempt.ballPos.x += wobble;
                attempt.ballPos.y = 0.05f;
                attempt.ballYawDeg = std::sin(attempt.phaseTime * 18.0f) * 20.0f;
                attempt.ballScale = attempt.ballImpactScale;

                if (attempt.phaseTime >= totalDur) {
                    attempt.phase = CaptureAttempt::Phase::Resolve;
                    attempt.phaseTime = 0.0f;
                    if (log) {
                        if (attempt.success) {
                            log->catchInfo("Gotcha! " + capitalize(attempt.name) + " was caught!");
                        } else {
                            log->catchInfo(capitalize(attempt.name) + " broke free!");
                        }
                    }
                }
                break;
            }
            case CaptureAttempt::Phase::Resolve: {
                const float dur = std::max(0.05f, attempt.resolveDur);
                const float t = std::clamp(attempt.phaseTime / dur, 0.0f, 1.0f);
                attempt.ballPos = attempt.targetPos;
                attempt.ballPos.y = 0.05f;

                if (attempt.success) {
                    attempt.ballScale = attempt.ballImpactScale * (1.0f - t);
                    if (t >= 1.0f) {
                        addToBench(attempt.name, attempt.level);
                        if (target) removeIds.push_back(target->id);
                        attempt.timeLeftSec = 0.0f;
                    }
                } else {
                    attempt.ballScale = attempt.ballImpactScale * (1.0f + t * 0.6f);
                    if (t >= 1.0f) {
                        if (target) {
                            if (renderEnabled) {
                                emitTackleImpactAt(*target, nullptr);
                            }
                            target->captureInProgress = false;
                            target->captureScale = 1.0f;
                            target->captureTintStrength = 0.0f;
                            if (!target->alive) {
                                target->alive = true;
                                target->hp = std::max(1, target->hp);
                                target->fainting = false;
                                target->fadeOutTimerSec = 0.0f;
                                target->visualScale = 1.0f;
                                target->activeAnimIndex = target->animIdleIndex;
                                target->animTimeSec = 0.0f;
                            }
                        }
                        attempt.timeLeftSec = 0.0f;
                    }
                }
                break;
            }
        }
    }

    if (!removeIds.empty()) {
        pokemons.erase(
            std::remove_if(pokemons.begin(), pokemons.end(),
                [&](const PokemonInstance& u) {
                    return std::find(removeIds.begin(), removeIds.end(), u.id) != removeIds.end();
                }),
            pokemons.end()
        );
    }

    captureAttempts.erase(
        std::remove_if(captureAttempts.begin(), captureAttempts.end(),
            [](const CaptureAttempt& a) { return a.timeLeftSec <= 0.0f; }),
        captureAttempts.end()
    );
}

void GameWorld::ensurePokeballModel() {
    if (pokeballModelLoaded) return;
    if (!resources) return;
    pokeballModel = resources->getModel("assets/models/pokeball.glb");
    pokeballModelLoaded = (pokeballModel != nullptr);
}

