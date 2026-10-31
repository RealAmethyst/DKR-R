#include "speech_service.hpp"

#include <prism.h>

#include <array>
#include <cstdio>
#include <memory>
#include <string>

namespace dkr::accessibility::speech {
namespace {

// Screen readers only, in order of preference. Plain text-to-speech backends
// (speech-dispatcher, SAPI, OneCore, AVSpeech) are deliberately absent: with
// no screen reader running the game stays silent. Backends Prism was built
// without are skipped.
constexpr std::array kScreenReaders{
    PRISM_BACKEND_ORCA,     PRISM_BACKEND_NVDA,      PRISM_BACKEND_JAWS,
    PRISM_BACKEND_VOICE_OVER, PRISM_BACKEND_ZDSR,    PRISM_BACKEND_ZOOM_TEXT,
    PRISM_BACKEND_PC_TALKER, PRISM_BACKEND_BOY_PC_READER, PRISM_BACKEND_SENSE_READER,
};

void PRISM_CALL LogPrism(void*, PrismLogLevel level, const char* source, const char* message) {
    std::fprintf(stderr, "[speech][prism] %s %s: %s\n",
                 level >= PRISM_LOG_LEVEL_ERROR ? "error" : "warning",
                 source != nullptr ? source : "-", message != nullptr ? message : "");
}

class PrismScreenReader final : public Backend {
public:
    ~PrismScreenReader() override {
        disconnect();
        if (context_ != nullptr) {
            prism_shutdown(context_);
        }
    }

    bool connect() override {
        if (context_ == nullptr) {
            prism_set_log_handler({LogPrism, nullptr});
            prism_set_log_level(PRISM_LOG_LEVEL_WARN);
            PrismConfig config = prism_config_init();
            context_ = prism_init(&config);
            if (context_ == nullptr) {
                Report("Prism could not be initialised");
                return false;
            }
        }
        std::string reasons;
        for (const PrismBackendId id : kScreenReaders) {
            if (!prism_registry_exists(context_, id)) {
                continue;
            }
            const char* name = prism_registry_name(context_, id);
            PrismBackend* candidate = prism_registry_create(context_, id);
            if (candidate == nullptr) {
                Append(reasons, name, "could not be created");
                continue;
            }
            const PrismError error = prism_backend_initialize(candidate);
            if (error == PRISM_OK) {
                backend_ = candidate;
                last_report_.clear();
                std::fprintf(stderr, "[speech] using %s\n", name != nullptr ? name : "screen reader");
                return true;
            }
            Append(reasons, name, prism_error_string(error));
            prism_backend_free(candidate);
        }
        Report(reasons.empty() ? "no screen reader backend in this build"
                               : "no screen reader available (" + reasons + ")");
        return false;
    }

    bool speak(const std::string& text, bool interrupt) override {
        if (backend_ == nullptr) {
            return false;
        }
        const PrismError error = prism_backend_speak(backend_, text.c_str(), interrupt);
        if (error == PRISM_OK) {
            return true;
        }
        std::fprintf(stderr, "[speech] %s speak failed: %s\n", prism_backend_name(backend_),
                     prism_error_string(error));
        return false;
    }

    void disconnect() override {
        if (backend_ != nullptr) {
            prism_backend_free(backend_);
            backend_ = nullptr;
        }
    }

private:
    static void Append(std::string& reasons, const char* name, const char* reason) {
        if (!reasons.empty()) {
            reasons += "; ";
        }
        reasons += name != nullptr ? name : "unnamed backend";
        reasons += ": ";
        reasons += reason != nullptr ? reason : "unknown error";
    }

    // Retries repeat until a screen reader appears; log each reason once.
    void Report(const std::string& reason) {
        if (reason != last_report_) {
            std::fprintf(stderr, "[speech] silent: %s\n", reason.c_str());
            last_report_ = reason;
        }
    }

    PrismContext* context_ = nullptr;
    PrismBackend* backend_ = nullptr;
    std::string last_report_;
};

} // namespace

std::unique_ptr<Backend> make_screen_reader_backend() {
    return std::make_unique<PrismScreenReader>();
}

} // namespace dkr::accessibility::speech
