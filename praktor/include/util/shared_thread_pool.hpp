#pragma once

#include "util/thread_pool.hpp"
#include <algorithm>
#include <thread>

namespace Praktor {

/**
 * @class SharedThreadPool
 * @brief Global shared thread pool for all workflow executors
 *
 * Prevents thread explosion when nested workflows each create their own pools.
 * All executors share the same pool, sized to hardware_concurrency.
 */
class SharedThreadPool {
public:
    // Non-copyable, non-movable
    SharedThreadPool(const SharedThreadPool&) = delete;
    SharedThreadPool& operator=(const SharedThreadPool&) = delete;

    static pubcxx::ThreadPool& instance() {
        static pubcxx::ThreadPool pool(optimalThreadCount());
        return pool;
    }

    static size_t optimalThreadCount() {
        // For I/O bound tasks, use more threads than cores
        // For CPU bound, use hardware_concurrency
        // Default: 2x cores, minimum 4, maximum 32
        size_t cores = std::thread::hardware_concurrency();
        if (cores == 0) cores = 2;
        return std::clamp(cores * 2, size_t(4), size_t(32));
    }

private:
    SharedThreadPool() = default;
};

} // namespace Praktor
