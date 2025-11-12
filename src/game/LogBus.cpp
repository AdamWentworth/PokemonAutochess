// LogBus.cpp
#include "LogBus.h"
#include "../engine/ui/BattleFeed.h"
static BattleFeed* g_feed = nullptr;
void LogBus::attach(BattleFeed* f){ g_feed = f; }
static void push(const std::string& s, const glm::vec3& c, float life=3.f){
  if (g_feed) g_feed->push(s, c, life);
}
void LogBus::info(const std::string& s){ push(s, {1,1,1}); }
void LogBus::warn(const std::string& s){ push(s, {1,0.9f,0.2f}); }
void LogBus::error(const std::string& s){ push(s, {1,0.3f,0.3f}); }
void LogBus::colored(const std::string& s, const glm::vec3& rgb, float life){ push(s, rgb, life); }