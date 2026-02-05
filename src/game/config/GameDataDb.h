// src/game/config/GameDataDb.h
#pragma once

// Central place to bundle game data/config services.
// Goal: reduce direct singleton access from gameplay code by threading this through GameApp -> GameWorld.

class PokemonConfigLoader;
class MovesConfigLoader;
class AttackAnimConfigLoader;
class FlyerConfigLoader;

struct GameDataDb {
    const PokemonConfigLoader*   pokemon = nullptr;
    const MovesConfigLoader*     moves = nullptr;
    const AttackAnimConfigLoader* attackAnims = nullptr;
    const FlyerConfigLoader*     flyers = nullptr;
};
