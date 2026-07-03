#pragma once

#include <mutex>

namespace Praktor::util {

/**
 * @brief Serialize all TurboScript API entry points.
 *
 * TurboScript currently behaves as if parts of its runtime are process-global.
 * Concurrent init/bind/run/free sequences can corrupt internal state, so all
 * callers must share one lock until the upstream runtime is proven thread-safe.
 */
inline std::mutex& turboScriptRuntimeMutex() {
    static std::mutex mutex;
    return mutex;
}

class TurboScriptRuntimeGuard {
public:
    TurboScriptRuntimeGuard() : lock_(turboScriptRuntimeMutex()) {}

private:
    std::lock_guard<std::mutex> lock_;
};

} // namespace Praktor::util
