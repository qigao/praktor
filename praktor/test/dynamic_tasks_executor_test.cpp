#include "executors/dynamic_tasks_executor.hpp"
#include "dag/workflow_context.hpp"

#include <catch2/catch_all.hpp>
#include <string>
#include <vector>

using namespace Praktor::Execution;

TEST_CASE("DynamicTasksExecutor placeholder substitution", "[dynamic_tasks]") {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    // Track generated tasks
    std::vector<Task> generated_tasks;
    dt_executor->setSubTaskCallback([&](const Task& task, WorkflowContext&) {
        generated_tasks.push_back(task);
        return true;
    });

    WorkflowContext context;

    SECTION("Simple string items with {{ item }}") {
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

        REQUIRE(result.success);
        REQUIRE(generated_tasks.size() == 3);
        CHECK(generated_tasks[0].name == "deploy_service_a");
        CHECK(generated_tasks[1].name == "deploy_service_b");
        CHECK(generated_tasks[2].name == "deploy_service_c");
    }

    SECTION("Object items with {{ item.field }}") {
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

        REQUIRE(result.success);
        REQUIRE(generated_tasks.size() == 2);

        CHECK(generated_tasks[0].name == "start_web");
        CHECK(generated_tasks[1].name == "start_api");

        // Check command contains substituted values
        const auto& bt0 = std::get<OrchParams>(generated_tasks[0].specifics);
        CHECK(bt0.root.children[0].params.at("cmd") == "./start.sh --name web --port 8080");

        const auto& bt1 = std::get<OrchParams>(generated_tasks[1].specifics);
        CHECK(bt1.root.children[0].params.at("cmd") == "./start.sh --name api --port 3000");
    }

    SECTION("{{ index }} placeholder") {
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

        REQUIRE(result.success);
        REQUIRE(generated_tasks.size() == 2);

        CHECK(generated_tasks[0].name == "task_0");
        CHECK(generated_tasks[1].name == "task_1");

        const auto& bt0 = std::get<OrchParams>(generated_tasks[0].specifics);
        CHECK(bt0.root.children[0].params.at("cmd") == "echo a at index 0");
    }
}

TEST_CASE("DynamicTasksExecutor error handling", "[dynamic_tasks]") {
    auto executor = createDynamicTasksExecutor();
    auto* dt_executor = static_cast<DynamicTasksExecutor*>(executor.get());

    WorkflowContext context;

    SECTION("Fails without callback") {
        Task task;
        task.name = "test";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "items";
        task.specifics = params;

        auto result = executor->execute(task, context);

        REQUIRE_FALSE(result.success);
        CHECK(result.error_message.find("callback") != std::string::npos);
    }

    SECTION("Fails when items_variable not found") {
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

        REQUIRE_FALSE(result.success);
        // Error could be "null", "array", or "not found" depending on what getValueByPath returns
        CHECK_FALSE(result.error_message.empty());
    }

    SECTION("Fails when items is not an array") {
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

        REQUIRE_FALSE(result.success);
        CHECK(result.error_message.find("array") != std::string::npos);
    }

    SECTION("Fails when subtask fails") {
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

        REQUIRE_FALSE(result.success);
        CHECK(call_count == 2);  // Should stop after second task fails
        CHECK(result.error_message.find("task_b") != std::string::npos);
    }
}

TEST_CASE("DynamicTasksExecutor template fields", "[dynamic_tasks]") {
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

    SECTION("Template with env vars") {
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

        REQUIRE(result.success);
        REQUIRE(generated.size() == 1);
        CHECK(generated[0].env["SERVICE_NAME"] == "x");
        CHECK(generated[0].env["INDEX"] == "0");
    }

    SECTION("Template with timeout") {
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

        REQUIRE(result.success);
        REQUIRE(generated[0].timeout.has_value());
        CHECK(generated[0].timeout.value() == "30s");
    }

    SECTION("Template with when condition") {
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

        REQUIRE(result.success);
        REQUIRE(generated[0].when.has_value());
        CHECK(generated[0].when.value() == "x != 'skip'");
    }

    SECTION("Template with command list") {
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

        REQUIRE(result.success);
        const auto& bt = std::get<OrchParams>(generated[0].specifics);
        REQUIRE(bt.root.children.size() == 3);
        CHECK(bt.root.children[0].params.at("cmd") == "bash");
        CHECK(bt.root.children[1].params.at("cmd") == "-c");
        CHECK(bt.root.children[2].params.at("cmd") == "echo x");
    }
}

TEST_CASE("DynamicTasksExecutor items_variable with {{ }} wrapper", "[dynamic_tasks]") {
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

    SECTION("Unwraps {{ config.services.list }}") {
        Task task;
        task.name = "deploy";
        task.action = TaskAction::DynamicTasks;

        DynamicTasksParams params;
        params.items_variable = "{{ config.services.list }}";
        params.task_template.name = "deploy_{{ item }}";
        params.task_template.command = std::string("echo {{ item }}");
        task.specifics = params;

        auto result = executor->execute(task, context);

        REQUIRE(result.success);
        REQUIRE(generated.size() == 2);
        CHECK(generated[0].name == "deploy_item1");
        CHECK(generated[1].name == "deploy_item2");
    }
}

TEST_CASE("DynamicTasksExecutor empty items array", "[dynamic_tasks]") {
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

    REQUIRE(result.success);
    CHECK(callback_count == 0);  // No tasks generated
}

TEST_CASE("DynamicTasksExecutor aggregates generated task results", "[dynamic_tasks]") {
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

    REQUIRE_FALSE(result.success);
    REQUIRE(generated_tasks.size() == 2);
    CHECK(generated_tasks[0].declared_runner == "command");
    CHECK(generated_tasks[0].source_path == "C:/tmp/workflow.yml");

    auto aggregated = context.getValueByPath("tasks.deploy_all.outputs.generated_tasks");
    REQUIRE(aggregated.is_array());
    REQUIRE(aggregated.size() == 2);
    CHECK(aggregated[0]["name"].as<std::string>() == "deploy_a");
    CHECK(aggregated[0]["status"].as<std::string>() == "success");
    CHECK(aggregated[0]["outputs"]["stdout"].as<std::string>() == "deploy_a_done");
    CHECK(aggregated[1]["name"].as<std::string>() == "deploy_b");
    CHECK(aggregated[1]["status"].as<std::string>() == "failed");
    CHECK(context.getValueByPath("tasks.deploy_all.outputs.generated_count").as<int64_t>() == 2);
    CHECK(context.getValueByPath("tasks.deploy_all.outputs.success_count").as<int64_t>() == 1);
    CHECK(context.getValueByPath("tasks.deploy_all.outputs.failed_count").as<int64_t>() == 1);
    CHECK(context.getValueByPath("tasks.deploy_all.outputs.skipped_count").as<int64_t>() == 0);
}

TEST_CASE("DynamicTasksExecutor rejects duplicate generated task names", "[dynamic_tasks]") {
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

    REQUIRE_FALSE(result.success);
    CHECK(result.error_message.find("collision") != std::string::npos);
}

TEST_CASE("DynamicTasksExecutor stops at first generated task failure", "[dynamic_tasks]") {
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

    REQUIRE_FALSE(result.success);
    CHECK(result.error_message.find("Generated task 'job_b' failed") != std::string::npos);
    REQUIRE(executed.size() == 2);
    CHECK(executed[0] == "job_a");
    CHECK(executed[1] == "job_b");
    CHECK(context.getValueByPath("tasks.fanout_jobs.outputs.generated_count").as<int64_t>() == 2);
    CHECK(context.getValueByPath("tasks.fanout_jobs.outputs.success_count").as<int64_t>() == 1);
    CHECK(context.getValueByPath("tasks.fanout_jobs.outputs.failed_count").as<int64_t>() == 1);
    CHECK(context.getValueByPath("tasks.fanout_jobs.outputs.skipped_count").as<int64_t>() == 0);
}
