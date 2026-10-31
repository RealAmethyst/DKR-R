#include "accessibility/speech_service.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using dkr::accessibility::speech::Backend;
using dkr::accessibility::speech::Service;
using namespace std::chrono_literals;

namespace {

struct Spoken {
    std::string text;
    bool interrupt;
};

// A scripted screen reader. Shared state outlives the service that owns it.
struct Script {
    std::mutex mutex;
    bool available = true;
    bool fail_next_speak = false;
    int connects = 0;
    int disconnects = 0;
    std::vector<Spoken> spoken;
};

class FakeReader final : public Backend {
public:
    explicit FakeReader(std::shared_ptr<Script> script) : script_(std::move(script)) {}
    bool connect() override {
        std::scoped_lock lock(script_->mutex);
        ++script_->connects;
        return script_->available;
    }
    bool speak(const std::string& text, bool interrupt) override {
        std::scoped_lock lock(script_->mutex);
        if (!script_->available || script_->fail_next_speak) {
            script_->fail_next_speak = false;
            return false;
        }
        script_->spoken.push_back({text, interrupt});
        return true;
    }
    void disconnect() override {
        std::scoped_lock lock(script_->mutex);
        ++script_->disconnects;
    }

private:
    std::shared_ptr<Script> script_;
};

struct FakeClock {
    std::atomic<std::int64_t> milliseconds{1000};
    Service::Clock::time_point now() const {
        return Service::Clock::time_point(std::chrono::milliseconds(milliseconds.load()));
    }
};

void spoken_in_order_with_interrupt_flags() {
    auto script = std::make_shared<Script>();
    FakeClock clock;
    Service service(std::make_unique<FakeReader>(script), 3s, [&] { return clock.now(); });
    service.say("OPTIONS", true);
    service.say("AUDIO OPTIONS, 3 of 6", false);
    service.say("", false); // empty text is ignored
    service.flush();
    std::scoped_lock lock(script->mutex);
    assert(script->spoken.size() == 2U);
    assert(script->spoken[0].text == "OPTIONS" && script->spoken[0].interrupt);
    assert(script->spoken[1].text == "AUDIO OPTIONS, 3 of 6" && !script->spoken[1].interrupt);
    assert(script->connects == 1);
}

void silent_without_screen_reader_and_retries_after_interval() {
    auto script = std::make_shared<Script>();
    script->available = false;
    FakeClock clock;
    Service service(std::make_unique<FakeReader>(script), 3s, [&] { return clock.now(); });
    service.say("START, 1 of 2", true);
    service.flush();
    service.say("OPTIONS, 2 of 2", true);
    service.flush();
    {
        std::scoped_lock lock(script->mutex);
        assert(script->spoken.empty());
        // The second announcement is inside the retry interval: no new attempt.
        assert(script->connects == 1);
        script->available = true;
    }
    service.say("START, 1 of 2", true);
    service.flush();
    {
        std::scoped_lock lock(script->mutex);
        assert(script->spoken.empty());
        assert(script->connects == 1);
    }
    // A screen reader started after the game is used by the next announcement
    // once the interval has passed.
    clock.milliseconds += 3000;
    service.say("OPTIONS, 2 of 2", true);
    service.flush();
    std::scoped_lock lock(script->mutex);
    assert(script->connects == 2);
    assert(script->spoken.size() == 1U && script->spoken[0].text == "OPTIONS, 2 of 2");
}

void failed_speech_reconnects_at_once_and_repeats() {
    auto script = std::make_shared<Script>();
    FakeClock clock;
    Service service(std::make_unique<FakeReader>(script), 3s, [&] { return clock.now(); });
    service.say("START, 1 of 2", true);
    service.flush();
    {
        std::scoped_lock lock(script->mutex);
        script->fail_next_speak = true;
    }
    service.say("OPTIONS, 2 of 2", true);
    service.flush();
    std::scoped_lock lock(script->mutex);
    assert(script->disconnects == 1);
    assert(script->connects == 2);
    assert(script->spoken.size() == 2U && script->spoken[1].text == "OPTIONS, 2 of 2");
}

void interrupt_drops_waiting_requests() {
    // A backend that blocks its first request lets the queue fill up behind it.
    struct Gate {
        std::mutex mutex;
        std::condition_variable opened;
        bool open = false;
        bool entered = false;
        std::condition_variable entered_signal;
        std::vector<std::string> spoken;
    };
    class GatedReader final : public Backend {
    public:
        explicit GatedReader(Gate& gate) : gate_(gate) {}
        bool connect() override { return true; }
        bool speak(const std::string& text, bool) override {
            std::unique_lock lock(gate_.mutex);
            gate_.entered = true;
            gate_.entered_signal.notify_all();
            gate_.opened.wait(lock, [&] { return gate_.open; });
            gate_.spoken.push_back(text);
            return true;
        }
        void disconnect() override {}

    private:
        Gate& gate_;
    };
    Gate gate;
    Service service(std::make_unique<GatedReader>(gate));
    service.say("first", true);
    {
        std::unique_lock lock(gate.mutex);
        gate.entered_signal.wait(lock, [&] { return gate.entered; });
    }
    service.say("stale item", false);
    service.say("stale hint", false);
    service.say("new screen", true);
    service.say("its first item", false);
    {
        std::scoped_lock lock(gate.mutex);
        gate.open = true;
    }
    gate.opened.notify_all();
    service.flush();
    std::scoped_lock lock(gate.mutex);
    assert((gate.spoken == std::vector<std::string>{"first", "new screen", "its first item"}));
}

void shutdown_with_waiting_requests_returns() {
    auto script = std::make_shared<Script>();
    {
        Service service(std::make_unique<FakeReader>(script));
        for (int index = 0; index < 100; ++index) service.say("item", false);
    }
    std::scoped_lock lock(script->mutex);
    assert(script->spoken.size() <= 100U);
}

} // namespace

int main() {
    spoken_in_order_with_interrupt_flags();
    silent_without_screen_reader_and_retries_after_interval();
    failed_speech_reconnects_at_once_and_repeats();
    interrupt_drops_waiting_requests();
    shutdown_with_waiting_requests_returns();
    std::puts("[test][accessibility-speech-service] PASS");
    return 0;
}
