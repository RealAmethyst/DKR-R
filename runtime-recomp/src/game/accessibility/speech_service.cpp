#include "speech_service.hpp"

#include <utility>

namespace dkr::accessibility::speech {

Service::Service(std::unique_ptr<Backend> backend, std::chrono::milliseconds retry_interval,
                 std::function<Clock::time_point()> now)
    : backend_(std::move(backend)), retry_interval_(retry_interval), now_(std::move(now)),
      worker_([this] { run(); }) {}

Service::~Service() {
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
        pending_.clear();
    }
    wake_.notify_all();
    worker_.join();
    if (connected_) {
        backend_->disconnect();
    }
    idle_.notify_all();
}

void Service::say(std::string text, bool interrupt) {
    if (text.empty()) {
        return;
    }
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) {
            return;
        }
        if (interrupt) {
            pending_.clear();
        }
        pending_.push_back({std::move(text), interrupt});
    }
    wake_.notify_one();
}

void Service::flush() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return stopping_ || (pending_.empty() && !busy_); });
}

void Service::run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
        if (stopping_) {
            return;
        }
        Request request = std::move(pending_.front());
        pending_.pop_front();
        busy_ = true;
        lock.unlock();
        deliver(request);
        lock.lock();
        busy_ = false;
        if (pending_.empty()) {
            idle_.notify_all();
        }
    }
}

void Service::deliver(const Request& request) {
    if (!ensure_connected()) {
        return;
    }
    if (backend_->speak(request.text, request.interrupt)) {
        return;
    }
    // The screen reader went away or restarted: look for it again at once and
    // repeat this request once. Further failures wait for the retry interval.
    backend_->disconnect();
    connected_ = false;
    attempted_ = false;
    if (ensure_connected() && !backend_->speak(request.text, request.interrupt)) {
        backend_->disconnect();
        connected_ = false;
    }
}

bool Service::ensure_connected() {
    if (connected_) {
        return true;
    }
    const Clock::time_point now = now_();
    if (attempted_ && now - last_attempt_ < retry_interval_) {
        return false;
    }
    attempted_ = true;
    last_attempt_ = now;
    connected_ = backend_->connect();
    return connected_;
}

} // namespace dkr::accessibility::speech
