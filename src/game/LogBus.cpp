// LogBus.cpp
#include "LogBus.h"
#include "../engine/ui/BattleFeed.h"
#include <iostream>                   // NEW
static BattleFeed* g_feed = nullptr;
static bool g_echo = true;
static bool g_feed_enabled = true; // NEW

void LogBus::attach(BattleFeed* f){ g_feed = f; }

static void push(const std::string& s, const glm::vec3& c, float life=3.f){
  if (g_feed_enabled && g_feed) g_feed->push(s, c, life); // gate on-screen feed
  if (g_echo) std::cout << s << "\n";
}

void LogBus::setEchoToStdout(bool enabled){ g_echo = enabled; }
void LogBus::setFeedEnabled(bool enabled){ g_feed_enabled = enabled; } // NEW

void LogBus::infoTerminalOnly(const std::string& s){                  // NEW
  std::cout << s << "\n"; // stdout only, never touches BattleFeed
}

void LogBus::info (const std::string& s){ push(s, {1,1,1}); }
void LogBus::warn (const std::string& s){ push("[WARN] "  + s, {1,0.9f,0.2f}); }
void LogBus::error(const std::string& s){ push("[ERROR] " + s, {1,0.3f,0.3f}); }
void LogBus::colored(const std::string& s, const glm::vec3& rgb, float life){ push(s, rgb, life); }
