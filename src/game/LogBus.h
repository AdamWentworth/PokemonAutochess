// LogBus.h
#pragma once
#include <string>
#include <glm/glm.hpp>
class BattleFeed;
namespace LogBus {
  void attach(BattleFeed* feed); // call once in Application::init
  void info(const std::string& s);
  void warn(const std::string& s);
  void error(const std::string& s);
  void colored(const std::string& s, const glm::vec3& rgb, float lifetime=3.f);
}