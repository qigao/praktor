#include "core/ast.hpp"
#include "core/executor.hpp"
#include "core/execution_context.hpp"
#include "tinytest.h"

using namespace actions;

suite("Advanced Decorators - KeepRunningUntilFailure") {

    given("a KeepRunningUntilFailure decorator") {
        when("child always succeeds") {
            then("should fail after max_iterations") {
                Executor executor;
                Blackboard bb;
                
                // Register a task that always succeeds
                executor.registerTask("AlwaysSuccess", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "KeepRunningUntilFailure";
                decorator.params["max_iterations"] = 5.0;
                
                Node child;
                child.id = "AlwaysSuccess";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
            }
        }
        
        when("child fails on third attempt") {
            then("should succeed when child fails") {
                Executor executor;
                Blackboard bb;
                
                // Register a task that fails on third call
                int call_count = 0;
                executor.registerTask("FailOnThird", [&call_count](const auto&, Blackboard&) {
                    call_count++;
                    return (call_count == 3) ? NodeStatus::FAILURE : NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "KeepRunningUntilFailure";
                decorator.params["max_iterations"] = 10.0;
                
                Node child;
                child.id = "FailOnThird";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((call_count) == (3));
            }
        }
        
        when("child returns RUNNING") {
            then("should return RUNNING") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("AlwaysRunning", [](const auto&, Blackboard&) {
                    return NodeStatus::RUNNING;
                });
                
                Node decorator;
                decorator.id = "KeepRunningUntilFailure";
                decorator.params["max_iterations"] = 5.0;
                
                Node child;
                child.id = "AlwaysRunning";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::RUNNING)));
            }
        }
    }
}

suite("Advanced Decorators - RunOnce") {

    given("a RunOnce decorator") {
        when("executed multiple times") {
            then("should execute child only once") {
                Executor executor;
                Blackboard bb;
                ExecutionContext ctx;
                
                int execution_count = 0;
                executor.registerTask("CountExecutions", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "RunOnce";
                
                Node child;
                child.id = "CountExecutions";
                decorator.children.push_back(child);
                
                // First execution
                NodeStatus status1 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
                
                // Second execution - should return cached result
                NodeStatus status2 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1)); // Still 1, not executed again
                
                // Third execution - still cached
                NodeStatus status3 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status3)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
            }
        }

        when("caller does not provide an execution context") {
            then("executor-owned context should still cache the result") {
                Executor executor;
                Blackboard bb;

                int execution_count = 0;
                executor.registerTask("CountExecutions", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });

                Node decorator;
                decorator.id = "RunOnce";

                Node child;
                child.id = "CountExecutions";
                decorator.children.push_back(child);

                NodeStatus status1 = executor.execute(decorator, bb);
                NodeStatus status2 = executor.execute(decorator, bb);

                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
            }
        }

        when("nested under another decorator") {
            then("state should be carried into child execution") {
                Executor executor;
                Blackboard bb;

                int execution_count = 0;
                executor.registerTask("CountExecutions", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });

                Node run_once;
                run_once.id = "RunOnce";

                Node child;
                child.id = "CountExecutions";
                run_once.children.push_back(child);

                Node force_success;
                force_success.id = "ForceSuccess";
                force_success.children.push_back(run_once);

                NodeStatus status1 = executor.execute(force_success, bb);
                NodeStatus status2 = executor.execute(force_success, bb);

                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
            }
        }
        
        when("child returns RUNNING") {
            then("should not cache result and re-execute") {
                Executor executor;
                Blackboard bb;
                ExecutionContext ctx;
                
                int execution_count = 0;
                executor.registerTask("RunningThenSuccess", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return (execution_count < 3) ? NodeStatus::RUNNING : NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "RunOnce";
                
                Node child;
                child.id = "RunningThenSuccess";
                decorator.children.push_back(child);
                
                // First execution - RUNNING
                NodeStatus status1 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::RUNNING)));
                check((execution_count) == (1));
                
                // Second execution - still RUNNING, re-executed
                NodeStatus status2 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::RUNNING)));
                check((execution_count) == (2));
                
                // Third execution - SUCCESS, now cached
                NodeStatus status3 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status3)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (3));
                
                // Fourth execution - cached, not re-executed
                NodeStatus status4 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status4)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (3)); // Still 3
            }
        }
        
        when("caching FAILURE result") {
            then("should return cached FAILURE") {
                Executor executor;
                Blackboard bb;
                ExecutionContext ctx;
                
                int execution_count = 0;
                executor.registerTask("AlwaysFail", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::FAILURE;
                });
                
                Node decorator;
                decorator.id = "RunOnce";
                
                Node child;
                child.id = "AlwaysFail";
                decorator.children.push_back(child);
                
                // First execution
                NodeStatus status1 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((execution_count) == (1));
                
                // Second execution - cached FAILURE
                NodeStatus status2 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((execution_count) == (1));
            }
        }
    }
}

suite("Advanced Decorators - ConsumeQueue") {

    given("a ConsumeQueue decorator") {
        when("queue has items") {
            then("should process all items") {
                Executor executor;
                Blackboard bb;
                
                // Setup queue with 3 items
                bb.set("task_queue", R"(["item1", "item2", "item3"])");
                
                int processed_count = 0;
                executor.registerTask("ProcessItem", [&processed_count](const auto&, Blackboard& bb) {
                    if (bb.has("current_item")) {
                        processed_count++;
                        return NodeStatus::SUCCESS;
                    }
                    return NodeStatus::FAILURE;
                });
                
                Node decorator;
                decorator.id = "ConsumeQueue";
                decorator.params["queue_key"] = std::string("task_queue");
                decorator.params["item_key"] = std::string("current_item");
                
                Node child;
                child.id = "ProcessItem";
                decorator.children.push_back(child);
                
                // First execution - processes first item, returns RUNNING (more items)
                NodeStatus status1 = executor.execute(decorator, bb);
                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::RUNNING)));
                check((processed_count) == (1));
                
                // Second execution - processes second item
                NodeStatus status2 = executor.execute(decorator, bb);
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::RUNNING)));
                check((processed_count) == (2));
                
                // Third execution - processes last item, returns SUCCESS (queue empty)
                NodeStatus status3 = executor.execute(decorator, bb);
                check((static_cast<int>(status3)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((processed_count) == (3));
            }
        }
        
        when("queue is empty") {
            then("should return FAILURE") {
                Executor executor;
                Blackboard bb;
                
                bb.set("task_queue", "[]");
                
                executor.registerTask("ProcessItem", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "ConsumeQueue";
                decorator.params["queue_key"] = std::string("task_queue");
                
                Node child;
                child.id = "ProcessItem";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
            }
        }
        
        when("queue key does not exist") {
            then("should return FAILURE") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("ProcessItem", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "ConsumeQueue";
                decorator.params["queue_key"] = std::string("nonexistent_queue");
                
                Node child;
                child.id = "ProcessItem";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
            }
        }
        
        when("child task fails") {
            then("should return FAILURE and stop processing") {
                Executor executor;
                Blackboard bb;
                
                bb.set("task_queue", R"(["item1", "item2", "item3"])");
                
                int processed_count = 0;
                executor.registerTask("FailingTask", [&processed_count](const auto&, Blackboard&) {
                    processed_count++;
                    return NodeStatus::FAILURE;
                });
                
                Node decorator;
                decorator.id = "ConsumeQueue";
                decorator.params["queue_key"] = std::string("task_queue");
                
                Node child;
                child.id = "FailingTask";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((processed_count) == (1)); // Only processed first item
            }
        }
    }
}
