// LogBus.h
#pragma once
#include <string>
#include <glm/glm.hpp>
class BattleFeed;

namespace LogBus {
  void attach(BattleFeed* feed);
  void info(const std::string& s);
  void warn(const std::string& s);
  void error(const std::string& s);
  void colored(const std::string& s, const glm::vec3& rgb, float lifetime=3.f);

  // NEW: enable/disable mirroring to stdout (default: on)
  void setEchoToStdout(bool enabled);
  void setFeedEnabled(bool enabled);      // optional toggle (useful for debugging)
  void infoTerminalOnly(const std::string& s); // print to stdout, never to BattleFeed
}
