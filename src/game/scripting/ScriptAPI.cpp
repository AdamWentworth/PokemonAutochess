// src/game/scripting/ScriptAPI.cpp

#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

#include "engine/render/Model.h"

#include "game/GameWorld.h"
#include "game/GameStateManager.h"
#include "game/GameServices.h"
#include "game/state/ScriptedState.h"
#include "game/state/CombatState.h"
#include "game/logging/LoggerUtil.h"

#include "game/animation/FlightLocomotion.h"
#include "game/animation/AttackAnimDebug.h"

#include "game/config/GameDataDb.h"
#include "game/config/MovesConfigLoader.h"
#include "game/config/AttackAnimConfigLoader.h"
#include "game/config/AnimSetLoader.h"

#include "game/logging/DebugTrace.h"

#include "LuaBindings_Internal.h"

namespace {
constexpr float kLeechSeedSpawnFrame = 41.0f;
constexpr float kLeechSeedSpeed = 5.0f;          // world units per second
constexpr float kLeechSeedMinTravelSec = 0.12f;
constexpr float kLeechSeedMaxTravelSec = 0.55f;

float computeLeechSeedTravelSec(float distance) {
    if (distance <= 0.0f) return kLeechSeedMinTravelSec;
    float t = distance / kLeechSeedSpeed;
    return std::clamp(t, kLeechSeedMinTravelSec, kLeechSeedMaxTravelSec);
}

bool isCombatActive(const PokemonInstance& u) {
    return u.alive && !u.captureInProgress;
}
} // namespace

ScriptAPI::ScriptAPI(GameWorld* world, GameStateManager* manager, GameServices& services)
    : world_(world), manager_(manager), services_(services) {}

LogBus::Logger& ScriptAPI::logger() const { return services_.log; }
ScriptEventBus& ScriptAPI::events() const { return services_.events; }
const GameConfigData& ScriptAPI::config() const { return services_.config; }

int ScriptAPI::getMoney() const {
    return world_ ? world_->getMoney() : 0;
}

void ScriptAPI::addMoney(int amount) {
    if (!world_) return;
    world_->addMoney(amount);
}

bool ScriptAPI::spendMoney(int amount) {
    if (!world_) return false;
    return world_->spendMoney(amount);
}

int ScriptAPI::getItemCount(const std::string& item) const {
    if (!world_) return 0;
    return world_->getItemCount(item);
}

void ScriptAPI::addItem(const std::string& item, int amount) {
    if (!world_) return;
    world_->addItem(item, amount);
}

bool ScriptAPI::consumeItem(const std::string& item, int amount) {
    if (!world_) return false;
    return world_->consumeItem(item, amount);
}

float ScriptAPI::getPokemonCatchRate(const std::string& name) const {
    const std::string key = toLowerCopy(name);
    const PokemonStats* ps = services_.dataDb.pokemon.getStats(key);
    if (!ps) return 0.0f;
    return std::clamp(ps->catchRate, 0.0f, 1.0f);
}

std::string ScriptAPI::getGameMode() const {
    return services_.gameMode;
}

void ScriptAPI::setGameMode(const std::string& mode) {
    const std::string key = toLowerCopy(mode);
    if (key == "classic" || key == "adventure") {
        services_.gameMode = key;
    }
}

bool ScriptAPI::getHasStartedGame() const {
    return services_.hasStartedGame;
}

void ScriptAPI::setHasStartedGame(bool started) {
    services_.hasStartedGame = started;
}

bool ScriptAPI::setVideoMode(int width, int height, bool fullscreen) {
    if (!services_.applyVideoMode) return false;
    const int safeW = std::max(640, width);
    const int safeH = std::max(360, height);
    return services_.applyVideoMode(safeW, safeH, fullscreen);
}

GameServices::VideoMode ScriptAPI::getVideoMode() const {
    if (services_.queryVideoMode) return services_.queryVideoMode();
    return {};
}

void ScriptAPI::requestQuit() {
    if (services_.requestQuit) {
        services_.requestQuit();
    }
}

ScriptAPI::ClassicIncomeResult ScriptAPI::awardClassicRoundIncome(bool playerWon) {
    ClassicIncomeResult out{};
    if (!world_) return out;
    if (services_.gameMode != "classic") return out;

    const auto r = world_->awardClassicRoundIncome(playerWon);
    out.baseIncome = r.baseIncome;
    out.interestIncome = r.interestIncome;
    out.streakIncome = r.streakIncome;
    out.totalIncome = r.totalIncome;
    out.winStreak = r.winStreak;
    out.lossStreak = r.lossStreak;
    out.roundIndex = r.roundIndex;
    out.won = r.won;
    return out;
}

std::vector<ScriptAPI::ClassicShopCardSnapshot> ScriptAPI::getClassicShopCards() const {
    std::vector<ClassicShopCardSnapshot> out;
    if (!world_) return out;

    const auto& src = world_->getClassicShopCards();
    out.reserve(src.size());
    for (const auto& c : src) {
        ClassicShopCardSnapshot s;
        s.name = c.name;
        s.level = c.level;
        s.cost = c.cost;
        out.push_back(std::move(s));
    }
    return out;
}

void ScriptAPI::setClassicShopCards(const std::vector<ClassicShopCardSnapshot>& cards) {
    if (!world_) return;
    std::vector<GameWorld::ClassicShopCard> out;
    out.reserve(cards.size());
    for (const auto& c : cards) {
        if (c.name.empty()) continue;
        GameWorld::ClassicShopCard gc;
        gc.name = c.name;
        gc.level = c.level;
        gc.cost = c.cost;
        out.push_back(std::move(gc));
    }
    world_->setClassicShopCards(out);
}

void ScriptAPI::clearClassicShopCards() {
    if (!world_) return;
    world_->clearClassicShopCards();
}

void ScriptAPI::startNewGame(const std::string& mode) {
    StartNewGameCommand cmd;
    cmd.mode = mode;
    enqueue(cmd);
}

std::vector<ScriptAPI::UnitSnapshot> ScriptAPI::listUnits() const {
    std::vector<UnitSnapshot> out;
    if (!world_) return out;
    auto& list = world_->getPokemons();
    out.reserve(list.size());
    for (const auto& u : list) {
        UnitSnapshot s;
        s.id = u.id;
        s.name = u.name;
        s.side = u.side;
        s.hp = u.hp;
        s.attack = u.attack;
        s.speed = u.movementSpeed;
        s.energy = u.energy;
        s.maxEnergy = u.maxEnergy;
        auto cell = worldToGrid(config(), u.position);
        s.col = cell.x;
        s.row = cell.y;
        const bool active = isCombatActive(u);
        s.alive = active;
        s.fainting = u.fainting;
        s.blocksTile = active || u.captureInProgress || (u.fainting && config().faintBlockTiles);
        s.captureInProgress = u.captureInProgress;
        s.fastMove = u.fastMove;
        s.chargedMove = u.chargedMove;
        s.types = u.types;
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<ScriptAPI::UnitSnapshot> ScriptAPI::getUnitSnapshot(int unitId) const {
    if (!world_) return std::nullopt;
    auto* u = world_->findUnitById(unitId);
    if (!u) return std::nullopt;

    UnitSnapshot s;
    s.id = u->id;
    s.name = u->name;
    s.side = u->side;
    s.hp = u->hp;
    s.attack = u->attack;
    s.speed = u->movementSpeed;
    s.energy = u->energy;
    s.maxEnergy = u->maxEnergy;
    auto cell = worldToGrid(config(), u->position);
    s.col = cell.x;
    s.row = cell.y;
    const bool active = isCombatActive(*u);
    s.alive = active;
    s.fainting = u->fainting;
    s.blocksTile = active || u->captureInProgress || (u->fainting && config().faintBlockTiles);
    s.captureInProgress = u->captureInProgress;
    s.fastMove = u->fastMove;
    s.chargedMove = u->chargedMove;
    s.types = u->types;
    return s;
}

std::pair<int, int> ScriptAPI::nearestEnemyCell(int unitId) const {
    if (!world_) return std::make_pair(-1, -1);

    auto& list = world_->getPokemons();
    const auto it = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == unitId; });
    if (it == list.end()) return std::make_pair(-1, -1);

    if (!isCombatActive(*it)) return std::make_pair(-1, -1);
    const auto myCell = worldToGrid(config(), it->position);

    int best = std::numeric_limits<int>::max();
    glm::ivec2 bestCell(-1, -1);

    for (const auto& u : list) {
        if (!isCombatActive(u) || u.side == it->side) continue;
        const auto ec = worldToGrid(config(), u.position);
        const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
        if (d < best) {
            best = d;
            bestCell = ec;
        }
    }

    return std::make_pair(bestCell.x, bestCell.y);
}

bool ScriptAPI::isAdjacentToEnemy(int unitId) const {
    if (!world_) return false;
    auto& list = world_->getPokemons();
    auto it = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == unitId; });
    if (it == list.end()) return false;
    if (!isCombatActive(*it)) return false;
    auto myCell = worldToGrid(config(), it->position);

    int best = std::numeric_limits<int>::max();
    glm::ivec2 bestCell(-999,-999);
    for (auto& u : list) {
        if (!isCombatActive(u) || u.side == it->side) continue;
        auto ec = worldToGrid(config(), u.position);
        const int d = std::max(std::abs(myCell.x - ec.x), std::abs(myCell.y - ec.y));
        if (d < best) { best = d; bestCell = ec; }
    }
    const int dx = std::abs(myCell.x - bestCell.x);
    const int dy = std::abs(myCell.y - bestCell.y);
    return std::max(dx, dy) == 1;
}

std::vector<int> ScriptAPI::enemiesAdjacent(int unitId) const {
    std::vector<int> out;
    if (!world_) return out;

    PokemonInstance* attacker = nullptr;
    for (auto& u : world_->getPokemons()) if (u.id == unitId) { attacker = &u; break; }
    if (!attacker || !isCombatActive(*attacker)) return out;

    auto ac = worldToGrid(config(), attacker->position);
    for (auto& u : world_->getPokemons()) {
        if (!isCombatActive(u) || u.side == attacker->side) continue;
        auto ec = worldToGrid(config(), u.position);
        const int dx = std::abs(ac.x - ec.x);
        const int dy = std::abs(ac.y - ec.y);
        if (std::max(dx, dy) == 1) {
            out.push_back(u.id);
        }
    }
    return out;
}

bool ScriptAPI::canAttack(int unitId) const {
    if (!world_) return false;
    for (auto& u : world_->getPokemons()) {
        if (u.id != unitId) continue;
        if (!isCombatActive(u)) return false;
        if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
        return true;
    }
    return false;
}

bool ScriptAPI::attackReady(int unitId) const {
    if (!world_) return false;
    for (auto& u : world_->getPokemons()) {
        if (u.id != unitId) continue;
        if (!isCombatActive(u)) return false;
        if (u.usesAirLocomotion && FlightLocomotion::isAirborne(u)) return false;
        if (u.attackTimerSec > 0.0001f) return false;
        return true;
    }
    return false;
}

float ScriptAPI::attackMinRequestSec(int attackerId,
                                     const std::optional<std::string>& moveName,
                                     const std::optional<std::string>& kind) const {
    if (!world_) return 0.0f;
    auto& list = world_->getPokemons();
    auto A = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == attackerId; });
    if (A == list.end()) return 0.0f;

    const std::string speciesLower = toLowerCopy(A->name);
    const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
    std::string kindLower          = kind ? toLowerCopy(*kind) : "";

    const auto& data = services_.dataDb;
    if (kindLower.empty() && !moveLower.empty()) {
        if (const MoveData* md = data.moves.getMove(moveLower)) {
            kindLower = toLowerCopy(md->kind);
        }
    }
    if (kindLower.empty()) kindLower = "fast";

    return data.attackAnims.getMinRequestSec(speciesLower, kindLower, moveLower, &services_.log);
}

int ScriptAPI::getEnergy(int unitId) const {
    if (!world_) return 0;
    if (auto* u = world_->findUnitById(unitId)) return u->energy;
    return 0;
}

int ScriptAPI::getMaxEnergy(int unitId) const {
    if (!world_) return 100;
    if (auto* u = world_->findUnitById(unitId)) return u->maxEnergy;
    return 100;
}

float ScriptAPI::getUnitSpeed(int unitId) const {
    if (!world_) return 0.0f;
    if (auto* u = world_->findUnitById(unitId)) return u->movementSpeed;
    return 0.0f;
}

float ScriptAPI::getDamageMultiplier(int attackerId, int targetId) const {
    if (!world_) return 1.0f;
    auto* A = world_->findUnitById(attackerId);
    auto* T = world_->findUnitById(targetId);
    if (!A || !T) return 1.0f;

    const auto& b = world_->getCombatBalance();

    const float attMult = (A->side == PokemonSide::Player) ? b.playerDamageMult : b.enemyDamageMult;
    const float takenMult = (T->side == PokemonSide::Player) ? b.playerDamageTakenMult : b.enemyDamageTakenMult;

    const float safeAtt = std::max(0.0f, attMult);
    const float safeTaken = std::max(0.0f, takenMult);
    const float out = safeAtt * safeTaken;
    return (out > 0.0f) ? out : 0.0f;
}

std::string ScriptAPI::getUnitFastMove(int unitId) const {
    if (!world_) return {};
    if (auto* u = world_->findUnitById(unitId)) return u->fastMove;
    return {};
}

std::string ScriptAPI::getUnitChargedMove(int unitId) const {
    if (!world_) return {};
    if (auto* u = world_->findUnitById(unitId)) return u->chargedMove;
    return {};
}

std::optional<ScriptAPI::MoveSnapshot> ScriptAPI::getMove(const std::string& name) const {
    const auto& data = services_.dataDb;
    const MoveData* md = data.moves.getMove(name);
    if (!md) return std::nullopt;

    MoveSnapshot m;
    m.name = md->name;
    m.type = md->type;
    m.kind = md->kind;
    m.cooldownSec = md->cooldownSec;
    m.power = md->power;
    m.range = md->range;
    m.energyGain = md->energyGain;
    m.energyCost = md->energyCost;
    if (md->status.valid) {
        m.status.effect = md->status.effect;
        m.status.magnitude = md->status.magnitude;
        m.status.durationSec = md->status.durationSec;
        m.status.target = md->status.target;
        m.status.valid = true;
    }
    return m;
}

bool ScriptAPI::hasPlannedMove(int unitId) const {
    if (!world_) return false;
    if (auto* u = world_->findUnitById(unitId)) {
        return (u->committedDest.x >= 0 && u->committedDest.y >= 0);
    }
    return false;
}

bool ScriptAPI::isMoving(int unitId) const {
    if (!world_) return false;
    if (auto* u = world_->findUnitById(unitId)) return u->isMoving;
    return false;
}

std::vector<ScriptEvent> ScriptAPI::drainEvents() {
    return services_.events.drain();
}

void ScriptAPI::enqueue(Command cmd) {
    queue_.push_back(std::move(cmd));
}

void ScriptAPI::flush() {
    for (const auto& cmd : queue_) {
        applyCommand(cmd);
    }
    queue_.clear();
}

void ScriptAPI::emit(const std::string& tagOrMsg, const std::optional<std::string>& payload) {
    EmitCommand cmd;
    cmd.tag = tagOrMsg;
    if (payload.has_value() && !payload->empty()) {
        cmd.payload = *payload;
        cmd.hasPayload = true;
    }
    enqueue(cmd);
}

void ScriptAPI::spawnPokemon(const std::string& name, float x, float y, float z) {
    SpawnCommand cmd;
    cmd.name = name;
    cmd.x = x;
    cmd.y = y;
    cmd.z = z;
    enqueue(cmd);
}

void ScriptAPI::spawnOnGrid(const std::string& name, int col, int row, PokemonSide side, int level) {
    SpawnOnGridCommand cmd;
    cmd.name = name;
    cmd.col = col;
    cmd.row = row;
    cmd.side = side;
    cmd.level = level;
    enqueue(cmd);
}

void ScriptAPI::emitCatch(const std::string& msg) {
    if (msg.empty()) return;
    services_.log.catchInfo(msg);
}

void ScriptAPI::emitGold(const std::string& msg) {
    if (msg.empty()) return;
    services_.log.economyInfo(msg);
}

void ScriptAPI::pushState(const std::string& scriptPath) {
    PushStateCommand cmd;
    cmd.scriptPath = scriptPath;
    enqueue(cmd);
}

void ScriptAPI::pushCombatState(const std::string& scriptPath) {
    PushCombatStateCommand cmd;
    cmd.scriptPath = scriptPath;
    enqueue(cmd);
}

void ScriptAPI::popState() {
    enqueue(PopStateCommand{});
}

void ScriptAPI::addToBench(const std::string& name, int level) {
    AddToBenchCommand cmd;
    cmd.name = name;
    cmd.level = level;
    enqueue(cmd);
}

bool ScriptAPI::applyMove(int unitId, int col, int row) {
    if (!world_) return false;
    auto* u = world_->findUnitById(unitId);
    if (!u || !isCombatActive(*u)) return false;

    ApplyMoveCommand cmd;
    cmd.unitId = unitId;
    cmd.col = col;
    cmd.row = row;
    enqueue(cmd);
    return true;
}

bool ScriptAPI::commitMove(int unitId, int col, int row) {
    if (!world_) return false;
    auto* u = world_->findUnitById(unitId);
    if (!u || !isCombatActive(*u)) return false;

    CommitMoveCommand cmd;
    cmd.unitId = unitId;
    cmd.col = col;
    cmd.row = row;
    enqueue(cmd);
    return true;
}

void ScriptAPI::faceEnemy(int unitId, const std::optional<int>& tgtCol, const std::optional<int>& tgtRow) {
    FaceEnemyCommand cmd;
    cmd.unitId = unitId;
    cmd.hasTarget = (tgtCol.has_value() && tgtRow.has_value());
    if (cmd.hasTarget) {
        cmd.col = *tgtCol;
        cmd.row = *tgtRow;
    }
    enqueue(cmd);
}

void ScriptAPI::faceTarget(int unitId, int targetId) {
    FaceTargetCommand cmd;
    cmd.unitId = unitId;
    cmd.targetId = targetId;
    enqueue(cmd);
}

bool ScriptAPI::setEnergy(int unitId, int value) {
    if (!world_) return false;
    if (!world_->findUnitById(unitId)) return false;

    SetEnergyCommand cmd;
    cmd.unitId = unitId;
    cmd.value = value;
    enqueue(cmd);
    return true;
}

int ScriptAPI::addEnergy(int unitId, int delta) {
    if (!world_) return 0;
    auto* u = world_->findUnitById(unitId);
    if (!u) return 0;

    AddEnergyCommand cmd;
    cmd.unitId = unitId;
    cmd.delta = delta;
    enqueue(cmd);
    int m = u->maxEnergy;
    return std::max(0, std::min(u->energy + delta, m));
}

void ScriptAPI::applyCommand(const Command& cmd) {
    if (std::holds_alternative<EmitCommand>(cmd)) {
        const auto& c = std::get<EmitCommand>(cmd);
        if (c.hasPayload) {
            services_.events.emit(c.tag, c.payload);
        } else {
            services_.events.emit("log", c.tag);
        }
        if (c.hasPayload) {
            const bool hasBrackets = !c.tag.empty() && c.tag.front()=='[' && c.tag.back()==']';
            const std::string header = hasBrackets ? c.tag : ("[" + c.tag + "]");
            game::log::infoTerminalOnly(&services_.log, header + " " + c.payload);
        } else {
            game::log::info(&services_.log, c.tag);
        }
        return;
    }

    if (std::holds_alternative<SpawnCommand>(cmd)) {
        const auto& c = std::get<SpawnCommand>(cmd);
        if (world_) world_->spawnPokemon(c.name, {c.x, c.y, c.z});
        return;
    }

    if (std::holds_alternative<SpawnOnGridCommand>(cmd)) {
        const auto& c = std::get<SpawnOnGridCommand>(cmd);
        if (world_) world_->spawnPokemonAtGrid(c.name, c.col, c.row, c.side, c.level);
        return;
    }

    if (std::holds_alternative<PushStateCommand>(cmd)) {
        const auto& c = std::get<PushStateCommand>(cmd);
        if (manager_) {
            manager_->pushState(std::make_unique<ScriptedState>(manager_, world_, services_, c.scriptPath));
        }
        return;
    }

    if (std::holds_alternative<PushCombatStateCommand>(cmd)) {
        const auto& c = std::get<PushCombatStateCommand>(cmd);
        if (manager_) {
            manager_->pushState(std::make_unique<CombatState>(manager_, world_, services_, c.scriptPath));
        }
        return;
    }

    if (std::holds_alternative<PopStateCommand>(cmd)) {
        if (manager_) manager_->popState();
        return;
    }

    if (std::holds_alternative<AddToBenchCommand>(cmd)) {
        const auto& c = std::get<AddToBenchCommand>(cmd);
        if (world_) world_->addToBench(c.name, c.level);
        return;
    }

    if (std::holds_alternative<ApplyMoveCommand>(cmd)) {
        const auto& c = std::get<ApplyMoveCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u || !isCombatActive(*u)) return;
        u->position = gridToWorld(config(), c.col, c.row);
        u->isMoving = false;
        u->moveT = 1.0f;
        u->committedDest = {-1,-1};
        return;
    }

    if (std::holds_alternative<CommitMoveCommand>(cmd)) {
        const auto& c = std::get<CommitMoveCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u || !isCombatActive(*u)) return;
        const glm::ivec2 target{c.col, c.row};
        for (const auto& other : world_->getPokemons()) {
            if (!other.alive && !other.captureInProgress && !(other.fainting && config().faintBlockTiles)) continue;
            if (other.id == u->id) continue;

            const auto oc = worldToGrid(config(), other.position);
            if (oc == target) return;
            if (other.committedDest.x >= 0 && other.committedDest.y >= 0) {
                if (other.committedDest == target) return;
            }
        }
        u->committedDest = {c.col, c.row};
        u->moveFrom = u->position;
        u->moveTo = gridToWorld(config(), c.col, c.row);
        u->moveT = 0.0f;
        u->isMoving = true;
        return;
    }

    if (std::holds_alternative<FaceEnemyCommand>(cmd)) {
        const auto& c = std::get<FaceEnemyCommand>(cmd);
        if (!world_) return;
        auto& list = world_->getPokemons();
        auto it = std::find_if(list.begin(), list.end(),
            [&](const PokemonInstance& p){ return p.id == c.unitId; });
        if (it == list.end()) return;

        glm::vec3 target;
        if (c.hasTarget) {
            target = gridToWorld(config(), c.col, c.row);
        } else {
            float best = std::numeric_limits<float>::max();
            glm::vec3 bestPos = it->position;
            for (auto& u : list) {
                if (!isCombatActive(u) || u.side == it->side) continue;
                float d = glm::distance(it->position, u.position);
                if (d < best) { best = d; bestPos = u.position; }
            }
            target = bestPos;
        }
        glm::vec3 lookDir = glm::normalize(target - it->position);
        it->rotation.y = std::atan2(lookDir.x, lookDir.z) * 180.0f / 3.14159265358979323846f;
        return;
    }

    if (std::holds_alternative<FaceTargetCommand>(cmd)) {
        const auto& c = std::get<FaceTargetCommand>(cmd);
        if (!world_) return;
        if (c.unitId < 0 || c.targetId < 0) return;

        auto* u = world_->findUnitById(c.unitId);
        auto* t = world_->findUnitById(c.targetId);
        if (!u || !t) return;

        glm::vec3 lookDir = glm::normalize(t->position - u->position);
        u->rotation.y = std::atan2(lookDir.x, lookDir.z) * 180.0f / 3.14159265358979323846f;
        return;
    }

    if (std::holds_alternative<SetEnergyCommand>(cmd)) {
        const auto& c = std::get<SetEnergyCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u) return;
        u->energy = std::max(0, std::min(c.value, u->maxEnergy));
        return;
    }

    if (std::holds_alternative<AddEnergyCommand>(cmd)) {
        const auto& c = std::get<AddEnergyCommand>(cmd);
        if (!world_) return;
        auto* u = world_->findUnitById(c.unitId);
        if (!u) return;
        int m = u->maxEnergy;
        u->energy = std::max(0, std::min(u->energy + c.delta, m));
        return;
    }

    if (std::holds_alternative<StartNewGameCommand>(cmd)) {
        const auto& c = std::get<StartNewGameCommand>(cmd);
        const std::string mode = toLowerCopy(c.mode);
        if (mode == "classic" || mode == "adventure") {
            services_.gameMode = mode;
        }
        services_.hasStartedGame = true;

        if (world_) {
            int startingMoney = services_.config.startingCash;
            if (services_.gameMode == "classic") {
                startingMoney = services_.config.classicStartingGold;
            }
            world_->resetForNewGame(startingMoney);
        }

        if (manager_) {
            manager_->clearAndPushState(std::make_unique<ScriptedState>(
                manager_, world_, services_, "scripts/states/starter.lua"));
        }
        return;
    }
}

int ScriptAPI::applyDamage(int attackerId,
                           int targetId,
                           int amount,
                           const std::optional<float>& cadenceSec,
                           const std::optional<std::string>& moveName,
                           const std::optional<std::string>& kind) {
    if (!world_) return -1;

    const auto* data = world_->getData();
    auto& list = world_->getPokemons();

    auto A = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == attackerId; });
    auto T = std::find_if(list.begin(), list.end(),
        [&](const PokemonInstance& p){ return p.id == targetId; });

    if (A == list.end() || T == list.end()) return -1;
    if (!isCombatActive(*A)) return T->hp;
    if (T->captureInProgress) return T->hp;

    const std::string speciesLower = toLowerCopy(A->name);
    const std::string moveLower    = moveName ? toLowerCopy(*moveName) : "";
    std::string kindLower          = kind ? toLowerCopy(*kind) : "";

    const MoveData* md = nullptr;
    if (!moveLower.empty() && data) {
        md = data->moves.getMove(moveLower);
    }

    if (kindLower.empty() && md) {
        kindLower = toLowerCopy(md->kind);
    }
    if (kindLower.empty()) kindLower = "fast";

    const std::string moveTypeLower = md ? toLowerCopy(md->type) : std::string();
    const bool isGrassMove = (moveTypeLower == "grass");
    const bool isLeechSeed = (moveLower == "leech_seed");
    const bool isTackle = (moveLower == "tackle");
    const bool isGrassImpact = (isGrassMove || isLeechSeed);

    const bool traceCombat = DebugTrace::combat(speciesLower, moveLower);
    auto trlog = [&](const std::string& msg) {
        if (!traceCombat) return;
        game::log::infoTerminalOnly(&services_.log, std::string("[TRACE_COMBAT_CPP] ") +
                                 "unit=" + speciesLower + " move=" + (moveLower.empty() ? std::string("-") : moveLower) + " " + msg);
    };

    if (traceCombat) {
        trlog(std::string("enter attackerId=") + std::to_string(attackerId) +
              " targetId=" + std::to_string(targetId) +
              " kind=" + kindLower +
              " move=" + (moveLower.empty() ? std::string("-") : moveLower) +
              " amount=" + std::to_string(amount) +
              " cadenceSec_in=" + std::to_string(cadenceSec.value_or(-1.0f)) +
              " atkTimer=" + std::to_string(A->attackTimerSec) +
              " atkDur=" + std::to_string(A->attackDurationSec) +
              " activeAnimIdx=" + std::to_string(A->activeAnimIndex) +
              " curAtkAnimIdx=" + std::to_string(A->currentAttackAnimIndex) +
              " atkAnimSpeed=" + std::to_string(A->attackAnimSpeed) +
              " fastChainTimerSec=" + std::to_string(A->fastChainTimerSec) +
              " chainedFastMove=" + (A->chainedFastMove.empty() ? std::string("-") : A->chainedFastMove));
    }

    if (A->attackDurationSec > 0.0f && A->animAttack1Index >= 0) {
        bool airborne = false;
        if (A->usesAirLocomotion) airborne = FlightLocomotion::isAirborne(*A);

        float desiredWindowSec = cadenceSec.value_or(0.0f);
        if (desiredWindowSec <= 0.0f) desiredWindowSec = A->attackDurationSec;

        const auto* animCfg = data ? &data->attackAnims : nullptr;

        const float minReqSec = animCfg
            ? animCfg->getMinRequestSec(speciesLower, kindLower, moveLower, &services_.log)
            : 0.0f;
        if (minReqSec > 0.0f) desiredWindowSec = std::max(desiredWindowSec, minReqSec);

        if (traceCombat) {
            const float baseCad = cadenceSec.value_or(0.0f);
            const float atk1Dur = (A->model && A->animAttack1Index >= 0) ? A->model->getAnimationDurationSec(A->animAttack1Index) : 0.0f;
            trlog(std::string("cadence baseCadenceArg=") + std::to_string(baseCad) +
                  " attackDurationSec=" + std::to_string(A->attackDurationSec) +
                  " clipDur_attack1=" + std::to_string(atk1Dur) +
                  " minReqSec=" + std::to_string(minReqSec) +
                  " desiredWindowSec=" + std::to_string(desiredWindowSec));
        }

        if (amount <= 0) {
            if (traceCombat) trlog("cosmetic: amount<=0 -> ignore (no cycle)");
            return T->hp;
        }

        const float kMidCycleEps = 0.0001f;
        if (A->attackTimerSec > kMidCycleEps) {
            if (traceCombat) trlog("lock: mid-cycle -> ignore request (no new cycle, no damage)");
            return T->hp;
        }

        int desiredAnimIdx = A->animAttack1Index;
        std::string phase = "default";
        std::string clipUsed;

        if (!speciesLower.empty()) {
            if (kindLower == "charged") {
                phase = "one_shot";
                clipUsed = animCfg
                    ? animCfg->getClipName(speciesLower, "charged", moveLower, "one_shot", &services_.log)
                    : std::string();
                const int idx = animIndexCached(*A, clipUsed);
                if (idx >= 0) desiredAnimIdx = idx;
            } else if (kindLower == "fast" && !moveLower.empty()) {
                const std::string clipLoop = animCfg
                    ? animCfg->getClipName(speciesLower, "fast", moveLower, "loop", &services_.log)
                    : std::string();
                const std::string clipDef  = animCfg
                    ? animCfg->getClipName(speciesLower, "fast", moveLower, "default", &services_.log)
                    : std::string();

                phase = "loop";
                clipUsed = clipLoop;
                if (clipUsed.empty()) {
                    phase = "default";
                    clipUsed = clipDef;
                }

                if (!clipUsed.empty()) {
                    const int idx = animIndexCached(*A, clipUsed);
                    if (idx >= 0) desiredAnimIdx = idx;
                }

                A->chainedFastMove.clear();
                A->fastChainTimerSec = 0.0f;
            }
        }

        if (traceCombat) {
            float clipDur = (A->model && desiredAnimIdx >= 0) ? A->model->getAnimationDurationSec(desiredAnimIdx) : 0.0f;
            trlog(std::string("resolved desiredAnimIdx=") + std::to_string(desiredAnimIdx) +
                  " clipDur=" + std::to_string(clipDur) +
                  " fastChainTimerSec=" + std::to_string(A->fastChainTimerSec) +
                  " chainedFastMove=" + (A->chainedFastMove.empty() ? std::string("-") : A->chainedFastMove));
        }

#ifdef PAC_DEBUG_ANIM
        std::cout << "[AnimDebug] " << A->name << " (ID " << A->id << ") "
                  << "attack requested airborne=" << (airborne ? "true" : "false")
                  << " dmg=" << amount
                  << " cadence=" << desiredWindowSec
                  << " animIdx=" << desiredAnimIdx
                  << " clipDur=" << (A->model ? A->model->getAnimationDurationSec(desiredAnimIdx) : 0.0f)
                  << "\n";
#endif

        if (airborne) {
            FlightLocomotion::queueAttackAfterLanding(*A, desiredWindowSec, desiredAnimIdx);
            return T->hp;
        }

        const float clipDur  = (A->model ? A->model->getAnimationDurationSec(desiredAnimIdx) : A->attackDurationSec);
        const float windowSec = std::max(0.05f, desiredWindowSec);

        A->attackTimerSec = windowSec;
        A->animTimeSec = 0.0f;
        A->currentAttackAnimIndex = desiredAnimIdx;
        A->activeAnimIndex = desiredAnimIdx;
        A->attackAnimSpeed = (windowSec > 0.0f && clipDur > 0.0f) ? (clipDur / windowSec) : 1.0f;

        A->pendingDamageActive = false;
        A->pendingDamageApplied = false;
        A->pendingDamageTargetId = -1;
        A->pendingDamageAmount = 0;
        A->pendingDamageHitTimeSec = 0.0f;
        A->pendingDamageIsGrass = false;
        A->pendingDamageIsTackle = false;
        A->pendingProjectileActive = false;
        A->pendingProjectileSpawned = false;
        A->pendingProjectileTargetId = -1;
        A->pendingProjectileSpawnTimeSec = 0.0f;
        A->pendingProjectileTravelSec = 0.0f;
        A->pendingImpactActive = false;
        A->pendingImpactApplied = false;
        A->pendingImpactTargetId = -1;
        A->pendingImpactTimeSec = 0.0f;
        A->pendingImpactIsGrass = false;
        A->pendingImpactIsLeechSeed = false;

        const bool startedThisCall = true;

        if (traceCombat) {
            trlog(std::string("attack_state startedThisCall=true") +
                  " windowSec=" + std::to_string(windowSec) +
                  " clipDur=" + std::to_string(clipDur) +
                  " activeAnimIdx=" + std::to_string(A->activeAnimIndex) +
                  " currentAttackAnimIndex=" + std::to_string(A->currentAttackAnimIndex) +
                  " atkAnimSpeed=" + std::to_string(A->attackAnimSpeed) +
                  " atkTimer=" + std::to_string(A->attackTimerSec));
        }

        if (A->debugAnimLogs) {
            const int hpBeforeDbg = T->hp;
            const bool willKillDbg = (std::max(0, amount) > 0 && (hpBeforeDbg - std::max(0, amount) <= 0));
            const float clipDurDbg = (A->model && desiredAnimIdx >= 0) ? A->model->getAnimationDurationSec(desiredAnimIdx) : 0.0f;
            AttackAnimDebug::logSelection(*A, kindLower, moveLower, phase, clipUsed, desiredAnimIdx,
                                        clipDurDbg, desiredWindowSec, amount,
                                        hpBeforeDbg, (std::max(0, hpBeforeDbg - std::max(0, amount))), startedThisCall,
                                        willKillDbg, A->fastChainTimerSec);
        }

        if (amount <= 0) return T->hp;
        if (!attackerIsInAttackAnimation(*A)) return T->hp;

        if (isLeechSeed) {
            const float fps = (A->animFps > 0.0f) ? A->animFps : 24.0f;
            float spawnTimeClip = kLeechSeedSpawnFrame / fps;

            const glm::vec3 aPos = A->position + glm::vec3(0.0f, A->visualYOffset, 0.0f);
            const glm::vec3 tPos = T->position + glm::vec3(0.0f, T->visualYOffset, 0.0f);
            const float dist = glm::distance(aPos, tPos);

            float travelReal = computeLeechSeedTravelSec(dist);
            float travelClip = travelReal * A->attackAnimSpeed;

            if (clipDur > 0.0f) {
                const float maxHit = std::max(0.0f, clipDur - 0.0001f);
                if (spawnTimeClip > maxHit) spawnTimeClip = maxHit;
                if (spawnTimeClip + travelClip > maxHit) {
                    travelClip = std::max(0.0f, maxHit - spawnTimeClip);
                    travelReal = (A->attackAnimSpeed > 0.0f) ? (travelClip / A->attackAnimSpeed) : travelReal;
                }
            }

            if (!A->pendingImpactActive) {
                A->pendingImpactActive = true;
                A->pendingImpactApplied = false;
                A->pendingImpactTargetId = targetId;
                A->pendingImpactTimeSec = std::max(0.0f, spawnTimeClip + travelClip);
                A->pendingImpactIsGrass = isGrassImpact;
                A->pendingImpactIsLeechSeed = true;

                A->pendingProjectileActive = true;
                A->pendingProjectileSpawned = false;
                A->pendingProjectileTargetId = targetId;
                A->pendingProjectileSpawnTimeSec = std::max(0.0f, spawnTimeClip);
                A->pendingProjectileTravelSec = std::max(0.01f, travelReal);
            }

            // Leech Seed does not deal damage on impact.
            return T->hp;
        }

        const int hitFrame = animCfg ? animCfg->getHitFrame(speciesLower, kindLower, moveLower) : -1;
        if (hitFrame > 0) {
            if (!A->pendingDamageActive) {
                const float fps = (A->animFps > 0.0f) ? A->animFps : 24.0f;
                float hitTimeSec = (float)hitFrame / fps;

                const float clipDurClamp = (A->model && A->currentAttackAnimIndex >= 0)
                    ? A->model->getAnimationDurationSec(A->currentAttackAnimIndex)
                    : 0.0f;
                if (clipDurClamp > 0.0f) {
                    const float maxT = std::max(0.0f, clipDurClamp - 0.0001f);
                    hitTimeSec = std::min(hitTimeSec, maxT);
                }

                A->pendingDamageActive     = true;
                A->pendingDamageApplied    = false;
                A->pendingDamageTargetId   = targetId;
                A->pendingDamageAmount     = std::max(0, amount);
                A->pendingDamageHitTimeSec = std::max(0.0f, hitTimeSec);
                A->pendingDamageIsGrass    = isGrassImpact;
                A->pendingDamageIsTackle   = isTackle;
            }

            return std::max(0, T->hp - std::max(0, amount));
        }
    }

    int dmg = std::max(0, amount);
    if (traceCombat) {
        trlog(std::string("damage_apply dmg=") + std::to_string(dmg) +
              " hp_before=" + std::to_string(T->hp));
    }
    T->hp = std::max(0, T->hp - dmg);
    if (dmg > 0 && isGrassImpact) {
        world_->emitGrassImpactAt(*T);
    }
    if (dmg > 0 && isTackle) {
        world_->emitTackleImpactAt(*T, &(*A));
    }
    if (traceCombat) {
        trlog(std::string("damage_result hp_after=") + std::to_string(T->hp) +
              " targetAlive=" + std::string(T->hp > 0 ? "true" : "false"));
    }

    if (T->hp <= 0) {
        world_->handleUnitFaint(*T);
    }

    return T->hp;
}
