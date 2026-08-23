#include "core/ast.hpp"
#include "core/executor.hpp"
#include "core/execution_context.hpp"
#include "tinytest.h"

using namespace actions;

suite("Extended Decorators - Precondition") {

    given("a Precondition decorator") {
        when("condition is true") {
            then("should execute child") {
                Executor executor;
                Blackboard bb;
                
                bb.set("ready", "true");
                
                int execution_count = 0;
                executor.registerTask("Task", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "Precondition";
                decorator.params["condition"] = std::string("{ready}");
                
                Node child;
                child.id = "Task";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
            }
        }
        
        when("condition is false") {
            then("should not execute child and return FAILURE") {
                Executor executor;
                Blackboard bb;
                
                bb.set("ready", "false");
                
                int execution_count = 0;
                executor.registerTask("Task", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "Precondition";
                decorator.params["condition"] = std::string("{ready}");
                
                Node child;
                child.id = "Task";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((execution_count) == (0)); // Child not executed
            }
        }
        
        when("condition uses boolean literal") {
            then("should evaluate correctly") {
                Executor executor;
                Blackboard bb;
                
                int execution_count = 0;
                executor.registerTask("Task", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });
                
                // Test with true
                Node decorator1;
                decorator1.id = "Precondition";
                decorator1.params["condition"] = true;
                Node child1;
                child1.id = "Task";
                decorator1.children.push_back(child1);
                
                NodeStatus status1 = executor.execute(decorator1, bb);
                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
                
                // Test with false
                Node decorator2;
                decorator2.id = "Precondition";
                decorator2.params["condition"] = false;
                Node child2;
                child2.id = "Task";
                decorator2.children.push_back(child2);
                
                NodeStatus status2 = executor.execute(decorator2, bb);
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((execution_count) == (1)); // Still 1, not executed again
            }
        }
        
        when("child fails") {
            then("should propagate failure") {
                Executor executor;
                Blackboard bb;
                
                bb.set("ready", "true");
                
                executor.registerTask("FailTask", [](const auto&, Blackboard&) {
                    return NodeStatus::FAILURE;
                });
                
                Node decorator;
                decorator.id = "Precondition";
                decorator.params["condition"] = std::string("{ready}");
                
                Node child;
                child.id = "FailTask";
                decorator.children.push_back(child);
                
                NodeStatus status = executor.execute(decorator, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
            }
        }
    }
}

suite("Extended Decorators - EntryUpdated") {

    given("an EntryUpdated decorator") {
        when("watched value changes") {
            then("should re-execute child") {
                Executor executor;
                Blackboard bb;
                ExecutionContext ctx;
                
                bb.set("counter", "1");
                
                int execution_count = 0;
                executor.registerTask("Task", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "EntryUpdated";
                decorator.params["watch_key"] = std::string("counter");
                
                Node child;
                child.id = "Task";
                decorator.children.push_back(child);
                
                // First execution
                NodeStatus status1 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1));
                
                // Second execution with same value - should not re-execute
                NodeStatus status2 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (1)); // Still 1
                
                // Change value
                bb.set("counter", "2");
                
                // Third execution with new value - should re-execute
                NodeStatus status3 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status3)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (2)); // Now 2
                
                // Fourth execution with same value - should not re-execute
                NodeStatus status4 = executor.execute(decorator, bb, ctx);
                check((static_cast<int>(status4)) == (static_cast<int>(NodeStatus::SUCCESS)));
                check((execution_count) == (2)); // Still 2
            }
        }

        when("child returns failure") {
            then("should cache and replay failure while watched value is unchanged") {
                Executor executor;
                Blackboard bb;
                ExecutionContext ctx;

                bb.set("counter", "1");

                int execution_count = 0;
                executor.registerTask("FailingTask", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::FAILURE;
                });

                Node decorator;
                decorator.id = "EntryUpdated";
                decorator.params["watch_key"] = std::string("counter");

                Node child;
                child.id = "FailingTask";
                decorator.children.push_back(child);

                NodeStatus status1 = executor.execute(decorator, bb, ctx);
                NodeStatus status2 = executor.execute(decorator, bb, ctx);

                check((static_cast<int>(status1)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((static_cast<int>(status2)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((execution_count) == (1));
            }
        }
         
        when("watched value is removed") {
            then("should detect change") {
                Executor executor;
                Blackboard bb;
                ExecutionContext ctx;
                
                bb.set("flag", "present");
                
                int execution_count = 0;
                executor.registerTask("Task", [&execution_count](const auto&, Blackboard&) {
                    execution_count++;
                    return NodeStatus::SUCCESS;
                });
                
                Node decorator;
                decorator.id = "EntryUpdated";
                decorator.params["watch_key"] = std::string("flag");
                
                Node child;
                child.id = "Task";
                decorator.children.push_back(child);
                
                // First execution
                executor.execute(decorator, bb, ctx);
                check((execution_count) == (1));
                
                // Value unchanged
                executor.execute(decorator, bb, ctx);
                check((execution_count) == (1));
                
                // Note: Blackboard doesn't have remove(), so we simulate by setting empty
                bb.set("flag", "");
                
                // Value changed (to empty)
                executor.execute(decorator, bb, ctx);
                check((execution_count) == (2));
            }
        }
    }
}

suite("Extended Control Flow - PipelineSequence") {

    given("a PipelineSequence node") {
        when("processing data through stages") {
            then("should pass output to next stage") {
                Executor executor;
                Blackboard bb;
                
                // Stage 1: Append "A"
                executor.registerTask("AppendA", [](const auto&, Blackboard& bb) {
                    std::string input = bb.has("pipeline_input") ? bb.get("pipeline_input") : "";
                    bb.set("pipeline_output", input + "A");
                    return NodeStatus::SUCCESS;
                });
                
                // Stage 2: Append "B"
                executor.registerTask("AppendB", [](const auto&, Blackboard& bb) {
                    std::string input = bb.has("pipeline_input") ? bb.get("pipeline_input") : "";
                    bb.set("pipeline_output", input + "B");
                    return NodeStatus::SUCCESS;
                });
                
                // Stage 3: Append "C"
                executor.registerTask("AppendC", [](const auto&, Blackboard& bb) {
                    std::string input = bb.has("pipeline_input") ? bb.get("pipeline_input") : "";
                    bb.set("pipeline_output", input + "C");
                    return NodeStatus::SUCCESS;
                });
                
                Node pipeline;
                pipeline.id = "PipelineSequence";
                
                Node stage1;
                stage1.id = "AppendA";
                pipeline.children.push_back(stage1);
                
                Node stage2;
                stage2.id = "AppendB";
                pipeline.children.push_back(stage2);
                
                Node stage3;
                stage3.id = "AppendC";
                pipeline.children.push_back(stage3);
                
                NodeStatus status = executor.execute(pipeline, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                
                // Check final result
                check_true(bb.has("pipeline_result"));
                const std::string pipeline_result = bb.get("pipeline_result");
                check(strcmp((pipeline_result.c_str()), ("ABC")) == 0);
            }
        }
        
        when("starting with initial input") {
            then("should use initial input for first stage") {
                Executor executor;
                Blackboard bb;
                
                bb.set("pipeline_input", "START:");
                
                executor.registerTask("AppendData", [](const auto&, Blackboard& bb) {
                    std::string input = bb.has("pipeline_input") ? bb.get("pipeline_input") : "";
                    bb.set("pipeline_output", input + "DATA");
                    return NodeStatus::SUCCESS;
                });
                
                Node pipeline;
                pipeline.id = "PipelineSequence";
                
                Node stage;
                stage.id = "AppendData";
                pipeline.children.push_back(stage);
                
                NodeStatus status = executor.execute(pipeline, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                
                const std::string pipeline_result = bb.get("pipeline_result");
                check(strcmp((pipeline_result.c_str()), ("START:DATA")) == 0);
            }
        }
        
        when("a stage fails") {
            then("should stop pipeline and return FAILURE") {
                Executor executor;
                Blackboard bb;
                
                int stage2_executed = 0;
                
                executor.registerTask("Stage1", [](const auto&, Blackboard& bb) {
                    bb.set("pipeline_output", "stage1");
                    return NodeStatus::SUCCESS;
                });
                
                executor.registerTask("FailStage", [](const auto&, Blackboard&) {
                    return NodeStatus::FAILURE;
                });
                
                executor.registerTask("Stage3", [&stage2_executed](const auto&, Blackboard& bb) {
                    stage2_executed++;
                    bb.set("pipeline_output", "stage3");
                    return NodeStatus::SUCCESS;
                });
                
                Node pipeline;
                pipeline.id = "PipelineSequence";
                
                Node stage1;
                stage1.id = "Stage1";
                pipeline.children.push_back(stage1);
                
                Node fail_stage;
                fail_stage.id = "FailStage";
                pipeline.children.push_back(fail_stage);
                
                Node stage3;
                stage3.id = "Stage3";
                pipeline.children.push_back(stage3);
                
                NodeStatus status = executor.execute(pipeline, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::FAILURE)));
                check((stage2_executed) == (0)); // Stage 3 not executed
            }
        }
        
        when("using custom input/output keys") {
            then("should respect custom keys") {
                Executor executor;
                Blackboard bb;
                
                bb.set("custom_input", "INIT");
                
                executor.registerTask("Transform", [](const auto&, Blackboard& bb) {
                    std::string input = bb.has("pipeline_input") ? bb.get("pipeline_input") : "";
                    bb.set("pipeline_output", input + "_TRANSFORMED");
                    return NodeStatus::SUCCESS;
                });
                
                Node pipeline;
                pipeline.id = "PipelineSequence";
                pipeline.params["input_key"] = std::string("custom_input");
                pipeline.params["output_key"] = std::string("custom_output");
                
                Node stage;
                stage.id = "Transform";
                pipeline.children.push_back(stage);
                
                NodeStatus status = executor.execute(pipeline, bb);
                check((static_cast<int>(status)) == (static_cast<int>(NodeStatus::SUCCESS)));
                
                check_true(bb.has("custom_output"));
                const std::string custom_output = bb.get("custom_output");
                check(strcmp((custom_output.c_str()), ("INIT_TRANSFORMED")) == 0);
            }
        }
    }
}
