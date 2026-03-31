#include <sstream>
#include <string>

#include "engine/utils/LogSink.h"

bool test_log_sink_contract(std::string& outFail) {
    {
        std::ostringstream info;
        std::ostringstream err;
        engine::log::Sink sink("TEST", &info, &err);
        sink.info("hello");
        sink.warn("careful");
        sink.error("boom");

        if (info.str() != "hello\n" ||
            err.str() != "careful\nboom\n") {
            outFail = "LogSink should route info to the info stream and warnings/errors to the error stream.";
            return false;
        }
    }

    {
        std::ostringstream shared;
        engine::log::Sink sink("TEST", &shared, nullptr);
        sink.info("line1");
        sink.warn("line2");
        if (shared.str() != "line1\nline2\n") {
            outFail = "LogSink should fall back to the shared stream when only one stream is provided.";
            return false;
        }
    }

    return true;
}
