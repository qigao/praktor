#include "actions/shell_executor.hpp"
#include "core/ast.hpp"
#include "core/executor.hpp"
#include "tinytest.h"
#include "concurrency_gate.hpp"
#include "actions/thread_pool.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

using namespace actions;

suite("Parallel Node - Scheduling") {

    given("a Parallel node with many children") {
        when("executing with thread pool") {
            then("should not create excessive threads") {
                Executor executor;
                Blackboard bb;
                
                std::atomic<int> concurrent_count{0};
                std::atomic<int> max_concurrent{0};
                ConcurrencyGate gate(std::min(std::size_t(20), ThreadPool::instance().num_threads()));
                std::atomic<bool> all_arrived{true};
                
                // Register a task that tracks concurrency
                executor.registerTask("ConcurrentTask", [&concurrent_count, &max_concurrent, &gate, &all_arrived](const auto&, Blackboard&) {
                    int current = ++concurrent_count;
                    
                    // Update max if needed
                    int expected = max_concurrent.load();
                    while (current > expected && !max_concurrent.compare_exchange_weak(expected, current)) {
                        // Retry if another thread updated it
                    }
                    
                    if (!gate.arriveAndWait()) all_arrived = false;
                    
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
                
                NodeStatus status = executor.execute(parallel, bb);
                
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                
                check_true(max_concurrent.load() > 1);
                check_true(max_concurrent.load() <= static_cast<int>(ThreadPool::instance().num_threads()));
                check_true(all_arrived.load());
            }
        }
        
        when("children have varying execution times") {
            then("should overlap fast and slow tasks") {
                Executor executor;
                Blackboard bb;
                
                std::atomic<int> completed{0};
                ConcurrencyGate gate(2);
                std::atomic<bool> all_arrived{true};
                
                // Fast task
                executor.registerTask("FastTask", [&completed, &gate, &all_arrived](const auto&, Blackboard&) {
                    if (!gate.arriveAndWait()) all_arrived = false;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    completed++;
                    return NodeStatus::SUCCESS;
                });
                
                // Slow task
                executor.registerTask("SlowTask", [&completed, &gate, &all_arrived](const auto&, Blackboard&) {
                    if (!gate.arriveAndWait()) all_arrived = false;
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
                
                NodeStatus status = executor.execute(parallel, bb);
                
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((completed.load()) == (10));
                
                check_true(all_arrived.load());
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
