#include "actions/shell_executor.hpp"
#include "core/ast.hpp"
#include "core/executor.hpp"
#include "tinytest.h"
#include <atomic>
#include <chrono>
#include <thread>

using namespace actions;

suite("Parallel Node - Performance") {

    given("a Parallel node with many children") {
        when("executing with thread pool") {
            then("should not create excessive threads") {
                Executor executor;
                Blackboard bb;
                
                std::atomic<int> concurrent_count{0};
                std::atomic<int> max_concurrent{0};
                
                // Register a task that tracks concurrency
                executor.registerTask("ConcurrentTask", [&concurrent_count, &max_concurrent](const auto&, Blackboard&) {
                    int current = ++concurrent_count;
                    
                    // Update max if needed
                    int expected = max_concurrent.load();
                    while (current > expected && !max_concurrent.compare_exchange_weak(expected, current)) {
                        // Retry if another thread updated it
                    }
                    
                    // Simulate work
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    
                    --concurrent_count;
                    return NodeStatus::SUCCESS;
                });
                
                // Create Parallel node with 20 children
                Node parallel;
                parallel.id = "Parallel";
                
                for (int i = 0; i < 20; ++i) {
                    Node child;
                    child.id = "ConcurrentTask";
                    parallel.children.push_back(child);
                }
                
                auto start = std::chrono::steady_clock::now();
                NodeStatus status = executor.execute(parallel, bb);
                auto end = std::chrono::steady_clock::now();
                
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                
                // Max concurrent should be limited by thread pool size (not 20)
                // Thread pool uses 2x cores, clamped to [4, 32]
                size_t cores = std::thread::hardware_concurrency();
                if (cores == 0) cores = 2;
                size_t expected_max = std::min(cores * 2, size_t(32));
                
                check_true(max_concurrent.load() <= static_cast<int>(expected_max) + 2); // Allow small overhead
                
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                // With proper parallelism, should complete much faster than sequential (20 * 50ms = 1000ms)
                check_true(duration.count() < 500); // Should be ~100-200ms with good parallelism
            }
        }
        
        when("children have varying execution times") {
            then("should complete efficiently") {
                Executor executor;
                Blackboard bb;
                
                std::atomic<int> completed{0};
                
                // Fast task
                executor.registerTask("FastTask", [&completed](const auto&, Blackboard&) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    completed++;
                    return NodeStatus::SUCCESS;
                });
                
                // Slow task
                executor.registerTask("SlowTask", [&completed](const auto&, Blackboard&) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    completed++;
                    return NodeStatus::SUCCESS;
                });
                
                Node parallel;
                parallel.id = "Parallel";
                
                // Mix of fast and slow tasks
                for (int i = 0; i < 5; ++i) {
                    Node fast;
                    fast.id = "FastTask";
                    parallel.children.push_back(fast);
                    
                    Node slow;
                    slow.id = "SlowTask";
                    parallel.children.push_back(slow);
                }
                
                auto start = std::chrono::steady_clock::now();
                NodeStatus status = executor.execute(parallel, bb);
                auto end = std::chrono::steady_clock::now();
                
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((completed.load()) == (10));
                
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                // Should complete in time dominated by slow tasks, not sum of all
                check_true(duration.count() < 300); // Much less than sequential (5*10 + 5*100 = 550ms)
            }
        }
    }
}

suite("Parallel Node - Correctness") {

    given("a Parallel node") {
        when("all children succeed") {
            then("should return SUCCESS") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("SuccessTask", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                Node parallel;
                parallel.id = "Parallel";
                
                for (int i = 0; i < 5; ++i) {
                    Node child;
                    child.id = "SuccessTask";
                    parallel.children.push_back(child);
                }
                
                NodeStatus status = executor.execute(parallel, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
            }
        }
        
        when("one child fails") {
            then("should return FAILURE") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("SuccessTask", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                executor.registerTask("FailTask", [](const auto&, Blackboard&) {
                    return NodeStatus::FAILURE;
                });
                
                Node parallel;
                parallel.id = "Parallel";
                
                for (int i = 0; i < 3; ++i) {
                    Node child;
                    child.id = "SuccessTask";
                    parallel.children.push_back(child);
                }
                
                Node fail_child;
                fail_child.id = "FailTask";
                parallel.children.push_back(fail_child);
                
                NodeStatus status = executor.execute(parallel, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
            }
        }
        
        when("one child returns RUNNING") {
            then("should return RUNNING") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("SuccessTask", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                executor.registerTask("RunningTask", [](const auto&, Blackboard&) {
                    return NodeStatus::RUNNING;
                });
                
                Node parallel;
                parallel.id = "Parallel";
                
                for (int i = 0; i < 3; ++i) {
                    Node child;
                    child.id = "SuccessTask";
                    parallel.children.push_back(child);
                }
                
                Node running_child;
                running_child.id = "RunningTask";
                parallel.children.push_back(running_child);
                
                NodeStatus status = executor.execute(parallel, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::RUNNING)));
            }
        }
    }
}

suite("Parallel Node - Thread Safety") {

    given("a Parallel node with shared state") {
        when("children access blackboard concurrently") {
            then("should handle concurrent access safely") {
                Executor executor;
                Blackboard bb;
                
                std::atomic<int> counter{0};
                
                // Task that increments counter and writes to blackboard
                executor.registerTask("ConcurrentWrite", [&counter](const auto&, Blackboard& bb) {
                    int id = counter++;
                    bb.set("task_" + std::to_string(id), std::to_string(id));
                    return NodeStatus::SUCCESS;
                });
                
                Node parallel;
                parallel.id = "Parallel";
                
                for (int i = 0; i < 10; ++i) {
                    Node child;
                    child.id = "ConcurrentWrite";
                    parallel.children.push_back(child);
                }
                
                NodeStatus status = executor.execute(parallel, bb);
                
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((counter.load()) == (10));
                
                // Verify all writes succeeded
                for (int i = 0; i < 10; ++i) {
                    check_true(bb.has("task_" + std::to_string(i)));
                }
            }
        }
    }

    given("a stream callback configured by the submitting thread") {
        when("parallel children emit output from worker threads") {
            then("each line should reach the submitting thread's callback") {
                Executor executor;
                Blackboard bb;
                std::atomic<int> streamed_lines{0};

                ShellExecutor::setStreamCallback([&streamed_lines](const std::string&) {
                    streamed_lines.fetch_add(1, std::memory_order_relaxed);
                });
                executor.registerTask("StreamLine", [](const auto&, Blackboard&) {
                    ShellExecutor::emitStreamLine("parallel output");
                    return NodeStatus::SUCCESS;
                });

                Node parallel;
                parallel.id = "Parallel";
                for (int i = 0; i < 10; ++i) {
                    Node child;
                    child.id = "StreamLine";
                    parallel.children.push_back(child);
                }

                NodeStatus status = executor.execute(parallel, bb);
                ShellExecutor::setStreamCallback({});

                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((streamed_lines.load(std::memory_order_relaxed)) == (10));
            }
        }
    }
}
