#include "praktor_test.hpp"
#include "executors/dynamic_tasks_executor.hpp"
#include "dag/workflow_context.hpp"

#include <string>
#include <vector>

using namespace Praktor::Execution;

[[maybe_unused]] static void test_dynamictasksexecutor_placeholder_substitution_1(int section_index) {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    // Track generated tasks
    std::vector<Task> generated_tasks;
    dt_executor->setSubTaskCallback([&](const Task& task, WorkflowContext&) {
        generated_tasks.push_back(task);
        return true;
    });

    WorkflowContext context;

    if (section_index == 0) {
        // Set up items in context
        WorkflowValue items = WorkflowValue::array();
        items.push_back("service_a");
        items.push_back("service_b");
        items.push_back("service_c");

        context.setValue("items_array", items);

        // Create task with DynamicTasksParams
        Task task;
        task.name = "deploy_all";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items_array";
        params.task_template.name = "deploy_{{ item }}";
        params.task_template.command = std::string("./deploy.sh {{ item }}");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated_tasks.size() == 3));
        check((generated_tasks[0].name == "deploy_service_a"));
        check((generated_tasks[1].name == "deploy_service_b"));
        check((generated_tasks[2].name == "deploy_service_c"));
    }

    if (section_index == 1) {
        generated_tasks.clear();

        WorkflowValue items = WorkflowValue::array();
        WorkflowValue item1 = WorkflowValue::object();
        item1["name"] = "web";
        item1["port"] = 8080;
        items.push_back(item1);

        WorkflowValue item2 = WorkflowValue::object();
        item2["name"] = "api";
        item2["port"] = 3000;
        items.push_back(item2);

        context.setValue("services", items);

        Task task;
        task.name = "start_services";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "services";
        params.task_template.name = "start_{{ item.name }}";
        params.task_template.command = std::string("./start.sh --name {{ item.name }} --port {{ item.port }}");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated_tasks.size() == 2));

        check((generated_tasks[0].name == "start_web"));
        check((generated_tasks[1].name == "start_api"));

        // Check command contains substituted values
        const auto& bt0 = std::get<OrchParams>(generated_tasks[0].specifics);
        check((bt0.root.children[0].params.at("cmd") == "./start.sh --name web --port 8080"));

        const auto& bt1 = std::get<OrchParams>(generated_tasks[1].specifics);
        check((bt1.root.children[0].params.at("cmd") == "./start.sh --name api --port 3000"));
    }

    if (section_index == 2) {
        generated_tasks.clear();

        WorkflowValue items = WorkflowValue::array();
        items.push_back("a");
        items.push_back("b");

        context.setValue("items", items);

        Task task;
        task.name = "indexed_tasks";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task_{{ index }}";
        params.task_template.command = std::string("echo {{ item }} at index {{ index }}");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated_tasks.size() == 2));

        check((generated_tasks[0].name == "task_0"));
        check((generated_tasks[1].name == "task_1"));

        const auto& bt0 = std::get<OrchParams>(generated_tasks[0].specifics);
        check((bt0.root.children[0].params.at("cmd") == "echo a at index 0"));
    }
}

spec("DynamicTasksExecutor placeholder substitution") {
    it("Simple string items with {{ item }}") { test_dynamictasksexecutor_placeholder_substitution_1(0); }
    it("Object items with {{ item.field }}") { test_dynamictasksexecutor_placeholder_substitution_1(1); }
    it("{{ index }} placeholder") { test_dynamictasksexecutor_placeholder_substitution_1(2); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_error_handling_2(int section_index) {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    WorkflowContext context;

    if (section_index == 0) {
        Task task;
        task.name = "test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        task.specifics = params;

        auto result = executor->execute(task, context);

        check_false(result.success);
        check((result.error_message.find("callback") != std::string::npos));
    }

    if (section_index == 1) {
        dt_executor->setSubTaskCallback([](const Task&, WorkflowContext&) { return true; });

        Task task;
        task.name = "test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "nonexistent";
        params.task_template.name = "task_{{ item }}";
        params.task_template.command = std::string("echo");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check_false(result.success);
        // Error could be "null", "array", or "not found" depending on what getValueByPath returns
        check((!(result.error_message.empty())));
    }

    if (section_index == 2) {
        dt_executor->setSubTaskCallback([](const Task&, WorkflowContext&) { return true; });

        context.setValue("items", std::string("not an array"));

        Task task;
        task.name = "test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task";
        params.task_template.command = std::string("echo");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check_false(result.success);
        check((result.error_message.find("array") != std::string::npos));
    }

    if (section_index == 3) {
        int call_count = 0;
        dt_executor->setSubTaskCallback([&](const Task&, WorkflowContext&) {
            call_count++;
            return call_count < 2;  // Fail on second task
        });

        WorkflowValue items = WorkflowValue::array();
        items.push_back("a");
        items.push_back("b");
        items.push_back("c");

        context.setValue("items", items);

        Task task;
        task.name = "test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task_{{ item }}";
        params.task_template.command = std::string("echo");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check_false(result.success);
        check((call_count == 2));  // Should stop after second task fails
        check((result.error_message.find("task_b") != std::string::npos));
    }
}

spec("DynamicTasksExecutor error handling") {
    it("Fails without callback") { test_dynamictasksexecutor_error_handling_2(0); }
    it("Fails when items_variable not found") { test_dynamictasksexecutor_error_handling_2(1); }
    it("Fails when items is not an array") { test_dynamictasksexecutor_error_handling_2(2); }
    it("Fails when subtask fails") { test_dynamictasksexecutor_error_handling_2(3); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_template_fields_3(int section_index) {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    std::vector<Task> generated;
    dt_executor->setSubTaskCallback([&](const Task& task, WorkflowContext&) {
        generated.push_back(task);
        return true;
    });

    WorkflowContext context;

    WorkflowValue items = WorkflowValue::array();
    items.push_back("x");

    context.setValue("items", items);

    if (section_index == 0) {
        Task task;
        task.name = "env_test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task_{{ item }}";
        params.task_template.command = std::string("echo");
        params.task_template.env["SERVICE_NAME"] = "{{ item }}";
        params.task_template.env["INDEX"] = "{{ index }}";
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated.size() == 1));
        check((generated[0].env["SERVICE_NAME"] == "x"));
        check((generated[0].env["INDEX"] == "0"));
    }

    if (section_index == 1) {
        generated.clear();

        Task task;
        task.name = "timeout_test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task";
        params.task_template.command = std::string("echo");
        params.task_template.timeout = "30s";
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated[0].timeout.has_value()));
        check((generated[0].timeout.value() == "30s"));
    }

    if (section_index == 2) {
        generated.clear();

        Task task;
        task.name = "when_test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task_{{ item }}";
        params.task_template.command = std::string("echo");
        params.task_template.when = "{{ item }} != 'skip'";
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated[0].when.has_value()));
        check((generated[0].when.value() == "x != 'skip'"));
    }

    if (section_index == 3) {
        generated.clear();

        Task task;
        task.name = "cmdlist_test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        params.task_template.name = "task";
        params.task_template.command = StrList{"bash", "-c", "echo {{ item }}"};
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        const auto& bt = std::get<OrchParams>(generated[0].specifics);
        check((bt.root.children.size() == 3));
        check((bt.root.children[0].params.at("cmd") == "bash"));
        check((bt.root.children[1].params.at("cmd") == "-c"));
        check((bt.root.children[2].params.at("cmd") == "echo x"));
    }
}

spec("DynamicTasksExecutor template fields") {
    it("Template with env vars") { test_dynamictasksexecutor_template_fields_3(0); }
    it("Template with timeout") { test_dynamictasksexecutor_template_fields_3(1); }
    it("Template with when condition") { test_dynamictasksexecutor_template_fields_3(2); }
    it("Template with command list") { test_dynamictasksexecutor_template_fields_3(3); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_items_variable_with_wrapper_4(int section_index) {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    std::vector<Task> generated;
    dt_executor->setSubTaskCallback([&](const Task& task, WorkflowContext&) {
        generated.push_back(task);
        return true;
    });

    WorkflowContext context;

    // Set up nested path: config.services.list
    // Note: We avoid "tasks.*" paths as those are special-cased to use TaskRegistry
    WorkflowValue config_services_list = WorkflowValue::array({"item1", "item2"});
    context.setValue("config.services.list", config_services_list);

    if (section_index == 0) {
        Task task;
        task.name = "deploy";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "{{ config.services.list }}";
        params.task_template.name = "deploy_{{ item }}";
        params.task_template.command = std::string("echo {{ item }}");
        task.specifics = params;

        auto result = executor->execute(task, context);

        check((result.success));
        check((generated.size() == 2));
        check((generated[0].name == "deploy_item1"));
        check((generated[1].name == "deploy_item2"));
    }
}

spec("DynamicTasksExecutor items_variable with {{ }} wrapper") {
    it("Unwraps {{ config.services.list }}") { test_dynamictasksexecutor_items_variable_with_wrapper_4(0); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_empty_items_array_5() {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    int callback_count = 0;
    dt_executor->setSubTaskCallback([&](const Task&, WorkflowContext&) {
        callback_count++;
        return true;
    });

    WorkflowContext context;
    context.setValue("items", WorkflowValue::array());

    Task task;
    task.name = "empty_test";
    task.action = TaskAction::DynamicTasks;

    DynamicTasksParams params;
    params.items_variable = "items";
    params.task_template.name = "task";
    params.task_template.command = std::string("echo");
    task.specifics = params;

    auto result = executor->execute(task, context);

    check((result.success));
    check((callback_count == 0));  // No tasks generated
}

spec("DynamicTasksExecutor empty items array") {
    it("complete case") { test_dynamictasksexecutor_empty_items_array_5(); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_aggregates_generated_task_results_6() {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    std::vector<Task> generated_tasks;
    dt_executor->setSubTaskCallback([&](const Task& task, WorkflowContext& context) {
        generated_tasks.push_back(task);
        context.setTaskStatus(task.name, "running");
        context.setTaskOutput(task.name, "stdout", task.name + "_done");
        context.setTaskStatus(task.name, task.name == "deploy_b" ? "failed" : "success");
        return task.name != "deploy_b";
    });

    WorkflowContext context;
    context.setTaskStatus("deploy_all", "running");
    context.pushTaskScope("deploy_all");

    WorkflowValue items = WorkflowValue::array({"a", "b", "c"});
    context.setValue("items", items);

    Task task;
    task.name = "deploy_all";
    task.action = TaskAction::DynamicTasks;
    task.source_path = "C:/tmp/workflow.yml";

    DynamicTasksParams params;
    params.items_variable = "items";
    params.task_template.name = "deploy_{{ item }}";
    params.task_template.command = std::string("./deploy.sh {{ item }}");
    task.specifics = params;

    auto result = executor->execute(task, context);
    context.popTaskScope();

    check_false(result.success);
    check((generated_tasks.size() == 2));
    check((generated_tasks[0].declared_runner == "command"));
    check((generated_tasks[0].source_path == "C:/tmp/workflow.yml"));

    auto aggregated = context.getValueByPath("tasks.deploy_all.outputs.generated_tasks");
    check((aggregated.is_array()));
    check((aggregated.size() == 2));
    check((aggregated[0]["name"].as<std::string>() == "deploy_a"));
    check((aggregated[0]["status"].as<std::string>() == "success"));
    check((aggregated[0]["outputs"]["stdout"].as<std::string>() == "deploy_a_done"));
    check((aggregated[1]["name"].as<std::string>() == "deploy_b"));
    check((aggregated[1]["status"].as<std::string>() == "failed"));
    check((context.getValueByPath("tasks.deploy_all.outputs.generated_count").as<int64_t>() == 2));
    check((context.getValueByPath("tasks.deploy_all.outputs.success_count").as<int64_t>() == 1));
    check((context.getValueByPath("tasks.deploy_all.outputs.failed_count").as<int64_t>() == 1));
    check((context.getValueByPath("tasks.deploy_all.outputs.skipped_count").as<int64_t>() == 0));
}

spec("DynamicTasksExecutor aggregates generated task results") {
    it("complete case") { test_dynamictasksexecutor_aggregates_generated_task_results_6(); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_rejects_duplicate_generated_task_names_7() {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());
    dt_executor->setSubTaskCallback([](const Task&, WorkflowContext&) { return true; });

    WorkflowContext context;
    context.setTaskStatus("deploy_all", "running");
    context.pushTaskScope("deploy_all");
    context.setValue("items", WorkflowValue::array({"dup", "dup"}));

    Task task;
    task.name = "deploy_all";
    task.action = TaskAction::DynamicTasks;

    DynamicTasksParams params;
    params.items_variable = "items";
    params.task_template.name = "deploy_{{ item }}";
    params.task_template.command = std::string("echo {{ item }}");
    task.specifics = params;

    auto result = executor->execute(task, context);
    context.popTaskScope();

    check_false(result.success);
    check((result.error_message.find("collision") != std::string::npos));
}

spec("DynamicTasksExecutor rejects duplicate generated task names") {
    it("complete case") { test_dynamictasksexecutor_rejects_duplicate_generated_task_names_7(); }
}

[[maybe_unused]] static void test_dynamictasksexecutor_stops_at_first_generated_task_failure_8() {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    std::vector<std::string> executed;
    dt_executor->setSubTaskCallback([&](const Task& task, WorkflowContext& context) {
        executed.push_back(task.name);
        context.setTaskStatus(task.name, "running");
        context.setTaskOutput(task.name, "stdout", task.name);
        if (task.name == "job_b") {
            context.setTaskStatus(task.name, "failed");
            return false;
        }
        context.setTaskStatus(task.name, "success");
        return true;
    });

    WorkflowContext context;
    context.setTaskStatus("fanout_jobs", "running");
    context.pushTaskScope("fanout_jobs");
    context.setValue("items", WorkflowValue::array({"a", "b", "c"}));

    Task task;
    task.name = "fanout_jobs";
    task.action = TaskAction::DynamicTasks;

    DynamicTasksParams params;
    params.items_variable = "items";
    params.task_template.name = "job_{{ item }}";
    params.task_template.command = std::string("echo {{ item }}");
    task.specifics = params;

    auto result = executor->execute(task, context);
    context.popTaskScope();

    check_false(result.success);
    check((result.error_message.find("Generated task 'job_b' failed") != std::string::npos));
    check((executed.size() == 2));
    check((executed[0] == "job_a"));
    check((executed[1] == "job_b"));
    check((context.getValueByPath("tasks.fanout_jobs.outputs.generated_count").as<int64_t>() == 2));
    check((context.getValueByPath("tasks.fanout_jobs.outputs.success_count").as<int64_t>() == 1));
    check((context.getValueByPath("tasks.fanout_jobs.outputs.failed_count").as<int64_t>() == 1));
    check((context.getValueByPath("tasks.fanout_jobs.outputs.skipped_count").as<int64_t>() == 0));
}

spec("DynamicTasksExecutor stops at first generated task failure") {
    it("complete case") { test_dynamictasksexecutor_stops_at_first_generated_task_failure_8(); }
}
