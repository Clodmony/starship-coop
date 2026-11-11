// The spdlog sink of the co-op log, see log_spdlog.h.

#include "log_spdlog.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include <spdlog/async.h>
#include <spdlog/async_logger.h>
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

// Holds spdlog's worker thread while the sink goes in (#180). The logger is asynchronous: its one worker
// (spdlog::init_thread_pool(8192, 1), libultraship/src/Context.cpp:104) walks logger->sinks() for every message,
// without a lock, and resource loads log from their own threads while the game starts. A push_back that grows that
// vector frees what the worker is reading: the worker then faulted in sink::should_log, and the crash handler's
// spdlog::shutdown() destroyed the pool from its own thread ("terminate called without an active exception").
// A message of a logger of our own on the same pool brings the worker here, and it stays until released: then nothing
// reads the sinks while they change.
class GateSink : public spdlog::sinks::sink {
  public:
    void log(const spdlog::details::log_msg&) override {
        std::unique_lock<std::mutex> lock(mMutex);
        mArrived = true;
        mChanged.notify_all();
        mChanged.wait(lock, [this] { return mReleased; });
    }
    void flush() override {
    }
    void set_pattern(const std::string&) override {
    }
    void set_formatter(std::unique_ptr<spdlog::formatter>) override {
    }

    // True once the worker is in log(); false if it did not come within `limit`.
    bool WaitForWorker(std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mMutex);
        return mChanged.wait_for(lock, limit, [this] { return mArrived; });
    }
    void Release() {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mReleased = true;
        }
        mChanged.notify_all();
    }

  private:
    std::mutex mMutex;
    std::condition_variable mChanged;
    bool mArrived = false;
    bool mReleased = false;
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
    auto pool = spdlog::thread_pool();
    if ((std::dynamic_pointer_cast<spdlog::async_logger>(logger) != nullptr) && (pool != nullptr)) {
        auto gate = std::make_shared<GateSink>();
        auto holder =
            std::make_shared<spdlog::async_logger>("coop-sink-gate", gate, pool, spdlog::async_overflow_policy::block);

        holder->set_level(spdlog::level::trace);
        holder->log(spdlog::level::info, "");
        // Far above what the queue of a starting game takes; past it the game's warnings stay out of the co-op log.
        if (!gate->WaitForWorker(std::chrono::seconds(10))) {
            gate->Release();
            return;
        }
        logger->sinks().push_back(std::make_shared<CoopSink>());
        gate->Release();
    } else {
        logger->sinks().push_back(std::make_shared<CoopSink>());
    }
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
