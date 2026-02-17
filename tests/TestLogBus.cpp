#include "game/logging/LogBus.h"

#include <cmath>
#include <string>

namespace {

bool approx(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

bool expectColor(const glm::vec3& actual, const glm::vec3& expected) {
    return approx(actual.r, expected.r) &&
           approx(actual.g, expected.g) &&
           approx(actual.b, expected.b);
}

} // namespace

bool test_logbus_recent_lines_contract(std::string& outFail) {
    LogBus::Logger log;
    log.setEchoToStdout(false);
    log.setFeedEnabled(false);

    for (int i = 0; i < 30; ++i) {
        log.info("line_" + std::to_string(i));
    }

    const auto capped = log.recentMainLines(64);
    if (capped.size() != 24u) {
        outFail = "recent main line cap should be 24";
        return false;
    }
    if (capped.front().text != "line_6" || capped.back().text != "line_29") {
        outFail = "recent main line order/cap mismatch";
        return false;
    }

    const auto lastThree = log.recentMainLines(3);
    if (lastThree.size() != 3u ||
        lastThree[0].text != "line_27" ||
        lastThree[1].text != "line_28" ||
        lastThree[2].text != "line_29") {
        outFail = "recentMainLines(maxCount) should return newest lines in order";
        return false;
    }

    log.warn("heads up");
    const auto warnTail = log.recentMainLines(1);
    if (warnTail.size() != 1u || warnTail[0].text != "[WARN] heads up") {
        outFail = "warn line text mismatch";
        return false;
    }
    if (!expectColor(warnTail[0].color, glm::vec3(1.0f, 0.9f, 0.2f))) {
        outFail = "warn line color mismatch";
        return false;
    }

    log.catchInfo("capture!", glm::vec3(0.2f, 0.4f, 0.6f));
    log.economyInfo("income +5g", glm::vec3(0.6f, 0.8f, 0.1f));

    const auto catchLines = log.recentCatchLines(4);
    if (catchLines.size() != 1u || catchLines[0].text != "capture!") {
        outFail = "catch feed snapshot mismatch";
        return false;
    }
    if (!expectColor(catchLines[0].color, glm::vec3(0.2f, 0.4f, 0.6f))) {
        outFail = "catch feed color mismatch";
        return false;
    }

    const auto economyLines = log.recentEconomyLines(4);
    if (economyLines.size() != 1u || economyLines[0].text != "income +5g") {
        outFail = "economy feed snapshot mismatch";
        return false;
    }
    if (!expectColor(economyLines[0].color, glm::vec3(0.6f, 0.8f, 0.1f))) {
        outFail = "economy feed color mismatch";
        return false;
    }

    const auto mainTail = log.recentMainLines(1);
    if (mainTail.size() != 1u || mainTail[0].text != "[WARN] heads up") {
        outFail = "catch/economy writes should not mutate main feed snapshot tail";
        return false;
    }

    return true;
}
