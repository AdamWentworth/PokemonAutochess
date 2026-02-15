// src/game/config/GameDataDb.h
#pragma once

// Central place to bundle game data/config services.
// Goal: reduce direct singleton access from gameplay code by threading this through GameApp -> GameWorld.
// Ownership: GameSession owns the loaders and passes const access to gameplay.

#include "PokemonConfigLoader.h"
#include "MovesConfigLoader.h"
#include "AttackAnimConfigLoader.h"
#include "FlyerConfigLoader.h"
#include "EvolutionConfigLoader.h"

struct GameDataDb {
    PokemonConfigLoader   pokemon;
    MovesConfigLoader     moves;
    AttackAnimConfigLoader attackAnims;
    FlyerConfigLoader     flyers;
    EvolutionConfigLoader evolution;
};
