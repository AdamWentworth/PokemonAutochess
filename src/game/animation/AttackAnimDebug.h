// src/game/AttackAnimDebug.h
#pragma once
#include <iostream>
#include <string>

struct PokemonInstance;

// Minimal, opt-in attack animation debug.
// Enable at runtime by setting PokemonInstance::debugAnimLogs = true on the unit.
namespace AttackAnimDebug {

// Print one line with selection and indices.
// Keep it cheap: only called when debugAnimLogs is true.
inline void logSelection(const PokemonInstance& A,
                         const std::string& kindLower,
                         const std::string& moveLower,
                         const std::string& phase,
                         const std::string& clipName,
                         int animIdx,
                         float clipDur,
                         float windowSec,
                         int amount,
                         int hpBefore,
                         int hpAfter,
                         bool startedThisCall,
                         bool willKill,
                         float chainTimerSec)
{
    std::cout << "[AnimTrace][AttackSelect] "
              << "id=" << A.id
              << " name=" << A.name << " "
              << "kind=" << kindLower
              << " move=" << (moveLower.empty() ? "-" : moveLower)
              << " phase=" << phase
              << " amount=" << amount
              << " hp=" << hpBefore << "->" << hpAfter
              << " willKill=" << (willKill ? "true" : "false")
              << " started=" << (startedThisCall ? "true" : "false")
              << " chain=" << chainTimerSec
              << " clip='" << clipName << "'"
              << " idx=" << animIdx
              << " clipDur=" << clipDur
              << " window=" << windowSec
              << "\n";
}

} // namespace AttackAnimDebug


