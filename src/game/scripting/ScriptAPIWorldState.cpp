#include "game/scripting/ScriptAPI.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "game/GameWorld.h"

#include "game/config/GameDataDb.h"
#include "game/config/PokemonConfigLoader.h"

#include "LuaBindings_Internal.h"

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
