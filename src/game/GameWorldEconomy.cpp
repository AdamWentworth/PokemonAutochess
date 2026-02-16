#include "GameWorld.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "GameConfig.h"

#include "config/GameDataDb.h"
#include "config/PokemonConfigLoader.h"

#include "logging/LoggerUtil.h"

namespace {

std::string capitalize(std::string s) {
    if (s.empty()) return s;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

}  // namespace

void GameWorld::resetForNewGame(int startingMoney) {
    pokemons.clear();
    benchPokemons.clear();
    battleStartPositions.clear();
    captureAttempts.clear();
    pendingLeechHeals.clear();
    classicShopCards.clear();
    unitDropZoneCardCount = 0;
    unitDropZoneAllItems = false;
    unitSellRewardsEnabled = true;

    selectedItemId.clear();
    items.clear();
    const int resetMoney = (startingMoney >= 0) ? startingMoney : config.startingCash;
    money = std::max(0, resetMoney);

    classicWinStreak = 0;
    classicLossStreak = 0;
    classicRoundsCompleted = 0;
    boardScaleMul = 1.0f;
    boardResizePauseSec = 0.0f;

    boardInteractionLocked = false;
    unitDragActive = false;
    uiClickBlockFrames = 0;
    resetCombatBalance();

    sharedLoopAnimTimeSec = 0.0f;
}

GameWorld::ClassicRoundIncomeResult GameWorld::awardClassicRoundIncome(bool playerWon) {
    ClassicRoundIncomeResult result{};
    result.won = playerWon;

    if (playerWon) {
        classicWinStreak = std::max(0, classicWinStreak + 1);
        classicLossStreak = 0;
    } else {
        classicLossStreak = std::max(0, classicLossStreak + 1);
        classicWinStreak = 0;
    }

    const int activeStreak = std::max(classicWinStreak, classicLossStreak);

    result.baseIncome = std::max(0, config.classicBaseIncome);
    const int per10 = std::max(0, config.classicInterestPer10);
    const int interestCap = std::max(0, config.classicInterestCap);
    const int interest = (std::max(0, money) / 10) * per10;
    result.interestIncome = std::min(interestCap, interest);
    if (activeStreak >= 6) {
        result.streakIncome = std::max(0, config.classicStreakBonus6Plus);
    } else if (activeStreak >= 4) {
        result.streakIncome = std::max(0, config.classicStreakBonus4To5);
    } else if (activeStreak >= 2) {
        result.streakIncome = std::max(0, config.classicStreakBonus2To3);
    } else {
        result.streakIncome = 0;
    }

    result.totalIncome = std::max(0, result.baseIncome + result.interestIncome + result.streakIncome);
    addMoney(result.totalIncome);

    classicRoundsCompleted += 1;
    result.roundIndex = classicRoundsCompleted;
    result.winStreak = classicWinStreak;
    result.lossStreak = classicLossStreak;
    return result;
}

void GameWorld::addMoney(int amount) {
    if (amount <= 0) return;
    money = std::max(0, money + amount);
}

bool GameWorld::spendMoney(int amount) {
    if (amount <= 0) return true;
    if (money < amount) return false;
    money -= amount;
    return true;
}

int GameWorld::getSellValueForSpecies(const std::string& pokemonName) const {
    if (!data) return 1;
    const PokemonStats* stats = data->pokemon.getStats(pokemonName);
    if (!stats) return 1;
    return std::max(1, stats->shopBaseCost);
}

void GameWorld::setClassicShopCards(const std::vector<ClassicShopCard>& cards) {
    classicShopCards.clear();
    classicShopCards.reserve(cards.size());
    for (const auto& c : cards) {
        if (c.name.empty()) continue;
        ClassicShopCard out = c;
        out.level = std::max(1, out.level);
        out.cost = std::max(0, out.cost);
        classicShopCards.push_back(std::move(out));
    }
}

void GameWorld::clearClassicShopCards() {
    classicShopCards.clear();
}

void GameWorld::setUnitDropZoneLayoutHint(int cardCount, bool allItems) {
    unitDropZoneCardCount = std::max(0, cardCount);
    unitDropZoneAllItems = allItems;
}

int GameWorld::getItemCount(const std::string& item) const {
    auto it = items.find(item);
    if (it == items.end()) return 0;
    return std::max(0, it->second);
}

void GameWorld::addItem(const std::string& item, int amount) {
    if (item.empty() || amount <= 0) return;
    items[item] = std::max(0, items[item] + amount);
}

bool GameWorld::consumeItem(const std::string& item, int amount) {
    if (item.empty() || amount <= 0) return true;
    auto it = items.find(item);
    if (it == items.end() || it->second < amount) return false;
    it->second -= amount;
    return true;
}

std::vector<std::pair<std::string, int>> GameWorld::listItems() const {
    std::vector<std::pair<std::string, int>> out;
    out.reserve(items.size());
    for (const auto& kv : items) {
        if (kv.second <= 0) continue;
        out.emplace_back(kv.first, kv.second);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    return out;
}

void GameWorld::setSelectedItem(const std::string& itemId) {
    selectedItemId = itemId;
}

void GameWorld::clearSelectedItem() {
    selectedItemId.clear();
}

bool GameWorld::tryUseHealingItem(const std::string& itemId, int targetId) {
    auto* target = findUnitById(targetId);
    if (!target) return false;
    if (target->side != PokemonSide::Player) return false;
    if (!target->alive) return false;

    if (!consumeItem(itemId, 1)) return false;

    const float pct = std::max(0.0f, config.potionHealPct);
    const int flat = std::max(0, config.potionHealFlat);
    const int healAmount = std::max(1, static_cast<int>(std::round(static_cast<float>(target->maxHP) * pct)) + flat);
    target->hp = std::min(target->maxHP, target->hp + healAmount);

    if (log) {
        game::log::info(log, "Used " + itemId + " on " + capitalize(target->name) +
            " (+" + std::to_string(healAmount) + " HP)");
    }
    return true;
}
