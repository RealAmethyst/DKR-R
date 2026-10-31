#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace dkr::accessibility::speech {

// A running screen reader. The service uses its backend from the worker
// thread only, so implementations need no locking of their own.
class Backend {
public:
    virtual ~Backend() = default;
    // Connects to a running screen reader; false when none is available.
    virtual bool connect() = 0;
    virtual bool speak(const std::string& text, bool interrupt) = 0;
    virtual void disconnect() = 0;
};

// Speech requests from game and UI threads, spoken on one worker thread so a
// slow screen reader never blocks the caller. An interrupting request drops
// everything still waiting. Without a screen reader nothing is spoken; the
// backend is looked for again only after a failure, at most once per retry
// interval, so starting a screen reader after the game is picked up by the
// next announcement.
class Service {
public:
    using Clock = std::chrono::steady_clock;

    explicit Service(std::unique_ptr<Backend> backend,
                     std::chrono::milliseconds retry_interval = std::chrono::seconds(3),
                     std::function<Clock::time_point()> now = Clock::now);
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    void say(std::string text, bool interrupt);
    // Waits until every request so far has been spoken or dropped.
    void flush();

private:
    struct Request {
        std::string text;
        bool interrupt = false;
    };

    void run();
    void deliver(const Request& request);
    bool ensure_connected();

    std::unique_ptr<Backend> backend_;
    const std::chrono::milliseconds retry_interval_;
    const std::function<Clock::time_point()> now_;
    // Worker-thread state.
    bool connected_ = false;
    bool attempted_ = false;
    Clock::time_point last_attempt_{};

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<Request> pending_;
    bool busy_ = false;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace dkr::accessibility::speech
