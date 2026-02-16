#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/PokemonInstance.h"

namespace {

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

PokemonInstance makeUnit(const std::string& name,
                         PokemonSide side,
                         bool alive,
                         int maxHp,
                         int hp) {
    PokemonInstance unit;
    unit.id = PokemonInstance::getNextUnitID();
    unit.name = name;
    unit.side = side;
    unit.alive = alive;
    unit.captureInProgress = false;
    unit.level = 1;
    unit.baseHp = std::max(1, maxHp);
    unit.baseAttack = 10;
    unit.baseMovementSpeed = 1.0f;
    unit.maxHP = std::max(1, maxHp);
    unit.hp = std::max(0, std::min(unit.maxHP, hp));
    unit.attack = 10;
    unit.movementSpeed = 1.0f;
    unit.position = glm::vec3(0.0f);
    return unit;
}

}  // namespace

bool test_gameworld_income_flow(std::string& outFail) {
    GameConfigData cfg;
    cfg.startingCash = 10;
    cfg.classicBaseIncome = 5;
    cfg.classicInterestPer10 = 1;
    cfg.classicInterestCap = 3;
    cfg.classicStreakBonus2To3 = 1;
    cfg.classicStreakBonus4To5 = 2;
    cfg.classicStreakBonus6Plus = 3;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    const GameWorld::ClassicRoundIncomeResult r1 = world.awardClassicRoundIncome(true);
    if (!expect(r1.baseIncome == 5 && r1.interestIncome == 1 && r1.streakIncome == 0 && r1.totalIncome == 6,
                "Round 1 income breakdown mismatch.", outFail)) return false;
    if (!expect(r1.winStreak == 1 && r1.lossStreak == 0 && r1.roundIndex == 1 && r1.won,
                "Round 1 streak/round state mismatch.", outFail)) return false;
    if (!expect(world.getMoney() == 16, "Round 1 money total mismatch.", outFail)) return false;

    const GameWorld::ClassicRoundIncomeResult r2 = world.awardClassicRoundIncome(true);
    if (!expect(r2.baseIncome == 5 && r2.interestIncome == 1 && r2.streakIncome == 1 && r2.totalIncome == 7,
                "Round 2 income breakdown mismatch.", outFail)) return false;
    if (!expect(r2.winStreak == 2 && r2.lossStreak == 0 && r2.roundIndex == 2 && r2.won,
                "Round 2 streak/round state mismatch.", outFail)) return false;
    if (!expect(world.getMoney() == 23, "Round 2 money total mismatch.", outFail)) return false;

    const GameWorld::ClassicRoundIncomeResult r3 = world.awardClassicRoundIncome(false);
    if (!expect(r3.baseIncome == 5 && r3.interestIncome == 2 && r3.streakIncome == 0 && r3.totalIncome == 7,
                "Round 3 income breakdown mismatch.", outFail)) return false;
    if (!expect(r3.winStreak == 0 && r3.lossStreak == 1 && r3.roundIndex == 3 && !r3.won,
                "Round 3 streak/round state mismatch.", outFail)) return false;
    if (!expect(world.getMoney() == 30, "Round 3 money total mismatch.", outFail)) return false;

    if (!expect(!world.spendMoney(31), "spendMoney should fail when funds are insufficient.", outFail)) return false;
    if (!expect(world.getMoney() == 30, "Failed spend should not mutate money.", outFail)) return false;
    if (!expect(world.spendMoney(30), "spendMoney should succeed with exact funds.", outFail)) return false;
    if (!expect(world.getMoney() == 0, "Money should be zero after exact spend.", outFail)) return false;
    if (!expect(world.spendMoney(0), "spendMoney(0) should be a no-op success.", outFail)) return false;

    world.addMoney(-5);
    if (!expect(world.getMoney() == 0, "addMoney should ignore non-positive values.", outFail)) return false;
    world.addMoney(4);
    if (!expect(world.getMoney() == 4, "addMoney should apply positive values.", outFail)) return false;

    return true;
}

bool test_gameworld_inventory_healing(std::string& outFail) {
    GameConfigData cfg;
    cfg.potionHealPct = 0.25f;
    cfg.potionHealFlat = 5;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    const PokemonInstance player = makeUnit("bulbasaur", PokemonSide::Player, true, 100, 20);
    const PokemonInstance enemy = makeUnit("rattata", PokemonSide::Enemy, true, 80, 25);
    const int playerId = player.id;
    const int enemyId = enemy.id;

    world.getPokemons().push_back(player);
    world.getPokemons().push_back(enemy);

    world.addItem("potion", 2);
    world.addItem("antidote", 1);
    world.addItem("", 4);
    if (!expect(world.getItemCount("potion") == 2, "potion inventory count mismatch.", outFail)) return false;
    if (!expect(world.getItemCount("") == 0, "Empty item id should not be stored.", outFail)) return false;

    const std::vector<std::pair<std::string, int>> items = world.listItems();
    if (!expect(items.size() == 2, "Expected exactly two distinct item ids.", outFail)) return false;
    if (!expect(items[0].first == "antidote" && items[0].second == 1 &&
                items[1].first == "potion" && items[1].second == 2,
                "Item list should be sorted and contain expected counts.", outFail)) return false;

    world.setSelectedItem("potion");
    if (!expect(world.getSelectedItem() == "potion", "Selected item should be set.", outFail)) return false;
    world.clearSelectedItem();
    if (!expect(world.getSelectedItem().empty(), "Selected item should clear.", outFail)) return false;

    if (!expect(world.tryUseHealingItem("potion", playerId), "Healing item should work on allied alive unit.", outFail)) return false;
    const PokemonInstance* healed = world.findUnitById(playerId);
    if (!expect(healed != nullptr && healed->hp == 50, "Healing amount should be pct + flat and clamped.", outFail)) return false;
    if (!expect(world.getItemCount("potion") == 1, "Healing should consume one item.", outFail)) return false;

    if (!expect(!world.tryUseHealingItem("potion", enemyId), "Healing should fail on enemy unit.", outFail)) return false;
    if (!expect(world.getItemCount("potion") == 1, "Failed heal should not consume item.", outFail)) return false;

    if (!expect(world.tryUseHealingItem("potion", playerId), "Second heal should still succeed.", outFail)) return false;
    healed = world.findUnitById(playerId);
    if (!expect(healed != nullptr && healed->hp == 80, "Second heal should stack correctly.", outFail)) return false;
    if (!expect(world.getItemCount("potion") == 0, "Second heal should consume last potion.", outFail)) return false;

    if (!expect(!world.tryUseHealingItem("potion", playerId), "Heal should fail with no inventory remaining.", outFail)) return false;

    PokemonInstance* playerMut = world.findUnitById(playerId);
    if (!expect(playerMut != nullptr, "Expected to find player unit by id.", outFail)) return false;
    playerMut->alive = false;
    world.addItem("potion", 1);
    if (!expect(!world.tryUseHealingItem("potion", playerId), "Heal should fail on fainted ally.", outFail)) return false;
    if (!expect(world.getItemCount("potion") == 1, "Fainted-target heal should not consume item.", outFail)) return false;

    return true;
}

bool test_gameworld_reset_economy_state(std::string& outFail) {
    GameConfigData cfg;
    cfg.startingCash = 42;
    cfg.classicBaseIncome = 5;
    cfg.classicInterestPer10 = 1;
    cfg.classicInterestCap = 5;

    GameWorld world(cfg);
    world.setRenderEnabled(false);

    world.addMoney(9);
    world.addItem("potion", 3);
    world.setSelectedItem("potion");
    world.setClassicShopCards({
        {"pikachu", 0, -2},
        {"", 3, 2},
    });
    world.setUnitDropZoneLayoutHint(3, true);
    world.setUnitSellRewardsEnabled(false);
    world.setBoardInteractionLocked(true);
    world.setUnitDragActive(true);
    world.blockUiClicks(3);

    world.getPokemons().push_back(makeUnit("bulbasaur", PokemonSide::Player, true, 100, 100));
    world.getBenchPokemons().push_back(makeUnit("charmander", PokemonSide::Player, true, 100, 100));

    if (!expect(world.getClassicShopCards().size() == 1, "Shop cards should drop empty names.", outFail)) return false;
    if (!expect(world.getClassicShopCards()[0].level == 1 && world.getClassicShopCards()[0].cost == 0,
                "Shop card sanitization mismatch.", outFail)) return false;

    world.awardClassicRoundIncome(true);
    world.awardClassicRoundIncome(true);

    world.resetForNewGame();

    if (!expect(world.getMoney() == 42, "resetForNewGame should restore configured starting money.", outFail)) return false;
    if (!expect(world.getPokemons().empty() && world.getBenchPokemons().empty(),
                "resetForNewGame should clear board and bench.", outFail)) return false;
    if (!expect(world.getClassicShopCards().empty(), "resetForNewGame should clear shop cards.", outFail)) return false;
    if (!expect(world.getUnitDropZoneCardCount() == 0 && !world.getUnitDropZoneUsesItemLayout(),
                "resetForNewGame should clear drop-zone hints.", outFail)) return false;
    if (!expect(world.isUnitSellRewardsEnabled(), "resetForNewGame should re-enable sell rewards.", outFail)) return false;
    if (!expect(world.getItemCount("potion") == 0 && world.listItems().empty(),
                "resetForNewGame should clear item inventory.", outFail)) return false;
    if (!expect(world.getSelectedItem().empty(), "resetForNewGame should clear selected item.", outFail)) return false;
    if (!expect(!world.isBoardInteractionLocked() && !world.isUnitDragActive(),
                "resetForNewGame should clear interaction lock and drag state.", outFail)) return false;
    if (!expect(!world.consumeUiClickBlocked(), "resetForNewGame should clear queued UI click blocks.", outFail)) return false;

    const GameWorld::ClassicRoundIncomeResult postResetIncome = world.awardClassicRoundIncome(false);
    if (!expect(postResetIncome.roundIndex == 1 && postResetIncome.winStreak == 0 && postResetIncome.lossStreak == 1,
                "resetForNewGame should reset round counters and streaks.", outFail)) return false;

    world.resetForNewGame(7);
    if (!expect(world.getMoney() == 7, "resetForNewGame(explicit) should override starting money.", outFail)) return false;

    return true;
}
