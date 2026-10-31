#include "speech.hpp"
#include "speech_service.hpp"

#include <memory>
#include <mutex>
#include <utility>

namespace dkr::accessibility::speech {

#if DKR_ACCESSIBILITY_HAS_PRISM
std::unique_ptr<Backend> make_screen_reader_backend();
#else
namespace {
// Builds without Prism have no screen reader output.
class NoScreenReader final : public Backend {
public:
    bool connect() override { return false; }
    bool speak(const std::string&, bool) override { return false; }
    void disconnect() override {}
};
} // namespace

std::unique_ptr<Backend> make_screen_reader_backend() {
    return std::make_unique<NoScreenReader>();
}
#endif

namespace {
std::mutex g_mutex;
std::unique_ptr<Service> g_service;
} // namespace

void start() {
    std::scoped_lock lock(g_mutex);
    if (!g_service) {
        g_service = std::make_unique<Service>(make_screen_reader_backend());
    }
}

void stop() {
    std::unique_ptr<Service> service;
    {
        std::scoped_lock lock(g_mutex);
        service = std::move(g_service);
    }
}

void say(std::string text, bool interrupt) {
    std::scoped_lock lock(g_mutex);
    if (g_service) {
        g_service->say(std::move(text), interrupt);
    }
}

} // namespace dkr::accessibility::speech
