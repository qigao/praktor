#include "actions/monitor.hpp"
#include "core/ast.hpp"
#include "core/executor.hpp"
#include "tinytest.h"
#include <thread>

using namespace actions;

suite("ExecutionMonitor - Basic Recording") {

    given("an execution monitor") {
        when("recording executions") {
            then("should track metrics correctly") {
                ExecutionMonitor monitor;
                
                monitor.recordExecution("TaskA", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                monitor.recordExecution("TaskA", NodeStatus::SUCCESS, std::chrono::milliseconds(150));
                monitor.recordExecution("TaskA", NodeStatus::FAILURE, std::chrono::milliseconds(50));
                
                NodeMetrics metrics = monitor.getMetrics("TaskA");
                
                check_str_eq(metrics.node_id.c_str(), "TaskA");
                check_int_eq(metrics.execution_count, 3);
                check_int_eq(metrics.success_count, 2);
                check_int_eq(metrics.failure_count, 1);
                check_int_eq(metrics.running_count, 0);
            }
        }
        
        when("tracking duration") {
            then("should calculate min/max/avg correctly") {
                ExecutionMonitor monitor;
                
                monitor.recordExecution("TaskB", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                monitor.recordExecution("TaskB", NodeStatus::SUCCESS, std::chrono::milliseconds(200));
                monitor.recordExecution("TaskB", NodeStatus::SUCCESS, std::chrono::milliseconds(300));
                
                NodeMetrics metrics = monitor.getMetrics("TaskB");
                
                check_int_eq(metrics.min_duration.count(), 100);
                check_int_eq(metrics.max_duration.count(), 300);
                check_int_eq(metrics.avg_duration.count(), 200); // (100+200+300)/3
                check_int_eq(metrics.total_duration.count(), 600);
            }
        }
        
        when("calculating success rate") {
            then("should return correct percentage") {
                ExecutionMonitor monitor;
                
                // 7 successes out of 10
                for (int i = 0; i < 7; ++i) {
                    monitor.recordExecution("TaskC", NodeStatus::SUCCESS, std::chrono::milliseconds(10));
                }
                for (int i = 0; i < 3; ++i) {
                    monitor.recordExecution("TaskC", NodeStatus::FAILURE, std::chrono::milliseconds(10));
                }
                
                NodeMetrics metrics = monitor.getMetrics("TaskC");
                
                check_double_eq(metrics.success_rate(), 70.0, 0.01);
                check_double_eq(metrics.failure_rate(), 30.0, 0.01);
            }
        }
    }
}

suite("ExecutionMonitor - Multiple Nodes") {

    given("monitoring multiple nodes") {
        when("tracking different nodes") {
            then("should keep metrics separate") {
                ExecutionMonitor monitor;
                
                monitor.recordExecution("Node1", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                monitor.recordExecution("Node2", NodeStatus::FAILURE, std::chrono::milliseconds(200));
                monitor.recordExecution("Node1", NodeStatus::SUCCESS, std::chrono::milliseconds(150));
                
                NodeMetrics metrics1 = monitor.getMetrics("Node1");
                NodeMetrics metrics2 = monitor.getMetrics("Node2");
                
                check_int_eq(metrics1.execution_count, 2);
                check_int_eq(metrics1.success_count, 2);
                
                check_int_eq(metrics2.execution_count, 1);
                check_int_eq(metrics2.failure_count, 1);
            }
        }
        
        when("getting all metrics") {
            then("should return all tracked nodes") {
                ExecutionMonitor monitor;
                
                monitor.recordExecution("NodeA", NodeStatus::SUCCESS, std::chrono::milliseconds(10));
                monitor.recordExecution("NodeB", NodeStatus::SUCCESS, std::chrono::milliseconds(20));
                monitor.recordExecution("NodeC", NodeStatus::SUCCESS, std::chrono::milliseconds(30));
                
                auto all_metrics = monitor.getAllMetrics();
                
                check_size_eq(all_metrics.size(), 3);
                check_true(all_metrics.count("NodeA") > 0);
                check_true(all_metrics.count("NodeB") > 0);
                check_true(all_metrics.count("NodeC") > 0);
            }
        }
    }
}

suite("ExecutionMonitor - Reset") {

    given("an execution monitor with data") {
        when("resetting specific node") {
            then("should clear only that node") {
                ExecutionMonitor monitor;
                
                monitor.recordExecution("Node1", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                monitor.recordExecution("Node2", NodeStatus::SUCCESS, std::chrono::milliseconds(200));
                
                monitor.reset("Node1");
                
                NodeMetrics metrics1 = monitor.getMetrics("Node1");
                NodeMetrics metrics2 = monitor.getMetrics("Node2");
                
                check_int_eq(metrics1.execution_count, 0); // Reset
                check_int_eq(metrics2.execution_count, 1); // Still there
            }
        }
        
        when("resetting all") {
            then("should clear all metrics") {
                ExecutionMonitor monitor;
                
                monitor.recordExecution("Node1", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                monitor.recordExecution("Node2", NodeStatus::SUCCESS, std::chrono::milliseconds(200));
                
                check_size_eq(monitor.getNodeCount(), 2);
                
                monitor.resetAll();
                
                check_size_eq(monitor.getNodeCount(), 0);
            }
        }
    }
}

suite("ExecutionMonitor - Summary") {

    given("an execution monitor") {
        when("getting summary") {
            then("should aggregate all metrics") {
                ExecutionMonitor monitor;
                
                // Node1: 2 successes
                monitor.recordExecution("Node1", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                monitor.recordExecution("Node1", NodeStatus::SUCCESS, std::chrono::milliseconds(100));
                
                // Node2: 1 success, 1 failure
                monitor.recordExecution("Node2", NodeStatus::SUCCESS, std::chrono::milliseconds(200));
                monitor.recordExecution("Node2", NodeStatus::FAILURE, std::chrono::milliseconds(200));
                
                auto summary = monitor.getSummary();
                
                check_int_eq(summary.total_executions, 4);
                check_int_eq(summary.total_successes, 3);
                check_int_eq(summary.total_failures, 1);
                check_int_eq(summary.total_duration.count(), 600);
                check_int_eq(summary.avg_duration.count(), 150); // 600/4
                check_double_eq(summary.overall_success_rate, 75.0, 0.01); // 3/4
            }
        }
    }
}

suite("ExecutionMonitor - Integration with Executor") {

    given("an executor with monitoring enabled") {
        when("executing nodes") {
            then("should automatically record metrics") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("TestTask", [](const auto&, Blackboard&) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    return NodeStatus::SUCCESS;
                });
                
                // Enable monitoring
                executor.enableMonitoring(true);
                
                Node node;
                node.id = "TestTask";
                
                // Execute multiple times
                for (int i = 0; i < 5; ++i) {
                    executor.execute(node, bb);
                }
                
                // Check metrics
                const auto& monitor = executor.getMonitor();
                NodeMetrics metrics = monitor.getMetrics("TestTask");
                
                check_int_eq(metrics.execution_count, 5);
                check_int_eq(metrics.success_count, 5);
                check_true(metrics.total_duration.count() >= 50); // At least 5*10ms
            }
        }
        
        when("monitoring is disabled") {
            then("should not record metrics") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("TestTask", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                // Monitoring disabled by default
                check_false(executor.isMonitoringEnabled());
                
                Node node;
                node.id = "TestTask";
                
                executor.execute(node, bb);
                
                // No metrics should be recorded
                const auto& monitor = executor.getMonitor();
                check_size_eq(monitor.getNodeCount(), 0);
            }
        }
        
        when("tracking different node types") {
            then("should record all node types") {
                Executor executor;
                Blackboard bb;
                
                executor.registerTask("Task1", [](const auto&, Blackboard&) {
                    return NodeStatus::SUCCESS;
                });
                
                executor.registerTask("Task2", [](const auto&, Blackboard&) {
                    return NodeStatus::FAILURE;
                });
                
                executor.enableMonitoring(true);
                
                // Execute Sequence
                Node seq;
                seq.id = "Sequence";
                Node child1;
                child1.id = "Task1";
                Node child2;
                child2.id = "Task2";
                seq.children.push_back(child1);
                seq.children.push_back(child2);
                
                executor.execute(seq, bb);
                
                // Check that all nodes were monitored
                const auto& monitor = executor.getMonitor();
                check_true(monitor.getNodeCount() >= 2); // At least Task1 and Task2
            }
        }
    }
}
