#pragma once

#include <string>

namespace dkr::accessibility::speech {

// Starts the speech worker. Nothing connects to a screen reader until the
// first announcement.
void start();
void stop();

// Speaks through the running screen reader, or nothing when there is none.
// An interrupting announcement stops current speech and drops queued speech.
// Never blocks the caller.
void say(std::string text, bool interrupt);

} // namespace dkr::accessibility::speech
