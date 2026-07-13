#include "actions/thread_pool.hpp"
#include "actions/async_executor.hpp"
#include "actions/shell_executor.hpp"
#include "core/executor.hpp"
#include "tinytest.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace actions;

namespace {

std::string longRunningShellCommand() {
#ifdef _WIN32
    return "ping 127.0.0.1 -n 10 > nul";
#else
    return "sleep 10";
#endif
}

} // namespace

suite("ThreadPool - Basic Functionality") {

    given("a thread pool") {
        when("submitting a simple task") {
            then("should execute the task") {
                ThreadPool pool(2);
                
                std::atomic<bool> executed{false};
                auto future = pool.submit([&executed]() {
                    executed = true;
                    return 42;
                });
                
                int result = future.get();
                check_int_eq(result, 42);
                check_true(executed.load());
            }
        }
        
        when("submitting multiple tasks") {
            then("should execute all tasks") {
                ThreadPool pool(4);
                
                std::atomic<int> counter{0};
                std::vector<std::future<int>> futures;
                
                for (int i = 0; i < 10; ++i) {
                    futures.push_back(pool.submit([&counter, i]() {
                        counter++;
                        return i * 2;
                    }));
                }
                
                // Wait for all tasks and verify results
                for (int i = 0; i < 10; ++i) {
                    int result = futures[i].get();
                    check_int_eq(result, i * 2);
                }
                
                check_int_eq(counter.load(), 10);
            }
        }
        
        when("tasks have different execution times") {
            then("should handle them concurrently") {
                ThreadPool pool(4);
                
                auto start = std::chrono::steady_clock::now();
                
                std::vector<std::future<int>> futures;
                for (int i = 0; i < 4; ++i) {
                    futures.push_back(pool.submit([i]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        return i;
                    }));
                }
                
                // Wait for all
                for (auto& f : futures) {
                    f.get();
                }
                
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                
                // With 4 threads, 4 tasks of 100ms should complete in ~100ms, not 400ms
                check_true(duration.count() < 200); // Allow some overhead
            }
        }
    }
}

suite("ThreadPool - Thread Count") {

    given("a thread pool") {
        when("created with specific thread count") {
            then("should have correct number of threads") {
                ThreadPool pool(8);
                check_size_eq(pool.num_threads(), 8);
            }
        }
        
        when("created with default thread count") {
            then("should use hardware concurrency") {
                ThreadPool pool;
                size_t expected = std::thread::hardware_concurrency();
                if (expected == 0) expected = 1;
                check_size_eq(pool.num_threads(), expected);
            }
        }
    }
}

suite("ThreadPool - Shared Instance") {

    given("the shared thread pool instance") {
        when("accessing multiple times") {
            then("should return same instance") {
                ThreadPool& pool1 = ThreadPool::instance();
                ThreadPool& pool2 = ThreadPool::instance();
                
                // Same address = same instance
                check_true(&pool1 == &pool2);
            }
        }
        
        when("submitting tasks to shared instance") {
            then("should execute tasks correctly") {
                std::atomic<int> counter{0};
                
                auto future1 = ThreadPool::instance().submit([&counter]() {
                    counter++;
                    return 1;
                });
                
                auto future2 = ThreadPool::instance().submit([&counter]() {
                    counter++;
                    return 2;
                });
                
                check_int_eq(future1.get(), 1);
                check_int_eq(future2.get(), 2);
                check_int_eq(counter.load(), 2);
            }
        }
    }
}

suite("ThreadPool - Exception Handling") {

    given("a thread pool") {
        when("task throws exception") {
            then("should propagate exception through future") {
                ThreadPool pool(2);
                
                auto future = pool.submit([]() -> int {
                    throw std::runtime_error("Task failed");
                    return 0;
                });
                
                bool caught = false;
                try {
                    future.get();
                } catch (const std::runtime_error& e) {
                    caught = true;
                    check_str_eq(e.what(), "Task failed");
                }
                
                check_true(caught);
            }
        }
        
        when("one task throws, others should continue") {
            then("should not affect other tasks") {
                ThreadPool pool(4);
                
                std::atomic<int> success_count{0};
                
                auto future1 = pool.submit([&success_count]() {
                    success_count++;
                    return 1;
                });
                
                auto future2 = pool.submit([]() -> int {
                    throw std::runtime_error("Fail");
                });
                
                auto future3 = pool.submit([&success_count]() {
                    success_count++;
                    return 3;
                });
                
                // First and third should succeed
                check_int_eq(future1.get(), 1);
                check_int_eq(future3.get(), 3);
                
                // Second should throw
                bool caught = false;
                try {
                    future2.get();
                } catch (const std::runtime_error&) {
                    caught = true;
                }
                check_true(caught);
                
                check_int_eq(success_count.load(), 2);
            }
        }
    }
}

suite("ThreadPool - Stress Test") {

    given("a thread pool under load") {
        when("submitting many tasks") {
            then("should handle them all correctly") {
                ThreadPool pool(4);
                
                const int num_tasks = 100;
                std::atomic<int> counter{0};
                std::vector<std::future<int>> futures;
                
                for (int i = 0; i < num_tasks; ++i) {
                    futures.push_back(pool.submit([&counter, i]() {
                        counter++;
                        // Simulate some work
                        int sum = 0;
                        for (int j = 0; j < 1000; ++j) {
                            sum += j;
                        }
                        return i + sum;
                    }));
                }
                
                // Verify all tasks completed
                for (int i = 0; i < num_tasks; ++i) {
                    int result = futures[i].get();
                    // Just verify it returned something (exact value depends on computation)
                    check_true(result >= i);
                }
                
                check_int_eq(counter.load(), num_tasks);
            }
        }
    }
}

suite("AsyncExecutor - Process Cancellation") {

    given("an async task running a shell process") {
        when("all tasks are cancelled") {
            then("the managed process should terminate promptly") {
                AsyncExecutor executor;
                auto task = executor.submit([]() {
                    auto result = ShellExecutor::execute(
                        longRunningShellCommand(), "", "", 30000, {}, false);
                    return result.success() ? NodeStatus::SUCCESS : NodeStatus::FAILURE;
                });

                const auto start = std::chrono::steady_clock::now();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                executor.cancelAll();
                const NodeStatus status = task->getResult();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);

                check_int_eq(static_cast<int>(status), static_cast<int>(NodeStatus::FAILURE));
                check_true(elapsed.count() < 3000);
            }
        }
    }
}
