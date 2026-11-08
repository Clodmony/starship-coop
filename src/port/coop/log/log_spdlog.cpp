// The spdlog sink of the co-op log, see log_spdlog.h.

#include "log_spdlog.h"

#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include "coop_log.h"

namespace {

constexpr size_t kQueueMax = 256;

struct Entry {
    int level;
    std::string text;
};

std::mutex sMutex;
std::deque<Entry> sQueue;
size_t sDropped = 0;
bool sInstalled = false;

class CoopSink : public spdlog::sinks::base_sink<std::mutex> {
  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        if (msg.level < spdlog::level::warn) {
            return;
        }
        std::string text(msg.payload.data(), msg.payload.size());
        std::string where;
        if (msg.source.filename != nullptr) {
            const char* name = msg.source.filename;
            for (const char* p = name; *p != '\0'; p++) {
                if (*p == '/' || *p == '\\') {
                    name = p + 1;
                }
            }
            where = std::string(name) + ":" + std::to_string(msg.source.line) + " ";
        }
        std::lock_guard<std::mutex> lock(sMutex);
        if (sQueue.size() >= kQueueMax) {
            sDropped++;
            return;
        }
        sQueue.push_back({ static_cast<int>(msg.level), where + text });
    }

    void flush_() override {
    }
};

} // namespace

extern "C" void CoopLogSpdlog_Install(void) {
    if (sInstalled) {
        return;
    }
    auto logger = spdlog::default_logger();
    if (logger == nullptr) {
        return;
    }
    logger->sinks().push_back(std::make_shared<CoopSink>());
    sInstalled = true;
}

extern "C" bool CoopLogSpdlog_Take(char* out, size_t cap, int* levelOut) {
    Entry entry;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (sQueue.empty()) {
            return false;
        }
        entry = std::move(sQueue.front());
        sQueue.pop_front();
        if (sDropped != 0 && sQueue.size() < kQueueMax) {
            entry.text += " [" + std::to_string(sDropped) + " more lines of the game log were dropped]";
            sDropped = 0;
        }
    }
    snprintf(out, cap, "%s", entry.text.c_str());
    CoopLog_Scrub(out, cap);
    *levelOut = entry.level;
    return true;
}

extern "C" void CoopLogSpdlog_Drop(void) {
    std::lock_guard<std::mutex> lock(sMutex);
    sQueue.clear();
    sDropped = 0;
}
