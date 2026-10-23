#include "praktor_test.hpp"

#include "data/structured_document_query.hpp"
#include "../src/yml/task_yaml_internal.hpp"
#include <new>

using Praktor::Data::StructuredDocumentQuery;
using Praktor::Data::StructuredFormat;

[[maybe_unused]] static void test_workflowvalue_copies_own_independent_turbo_json_trees_1() {
    WorkflowValue original = WorkflowValue::object();
    original["nested"] = WorkflowValue::object();
    original["nested"]["value"] = 7;

    WorkflowValue copy = original;
    copy["nested"]["value"] = 9;

    check((original["nested"]["value"].as<int>() == 7));
    check((copy["nested"]["value"].as<int>() == 9));
}

spec("WorkflowValue copies own independent Turbo JSON trees") {
    it("complete case") { test_workflowvalue_copies_own_independent_turbo_json_trees_1(); }
}

[[maybe_unused]] static void test_workflowvalue_distinguishes_object_keys_from_array_indexes_2() {
    WorkflowValue value = WorkflowValue::object();
    value["#0"] = "object member";

    check((value["#0"].as<std::string>() == "object member"));
}

spec("WorkflowValue distinguishes object keys from array indexes") {
    it("complete case") { test_workflowvalue_distinguishes_object_keys_from_array_indexes_2(); }
}

[[maybe_unused]] static void test_workflowvalue_promotes_null_to_an_object_on_member_assignment_3() {
    WorkflowValue value;
    value["task"] = "build";

    check((value.is_object()));
    check((value["task"].as<std::string>() == "build"));
}

spec("WorkflowValue promotes null to an object on member assignment") {
    it("complete case") { test_workflowvalue_promotes_null_to_an_object_on_member_assignment_3(); }
}

[[maybe_unused]] static void test_workflowvalue_transfers_rvalue_trees_into_containers_4() {
    WorkflowValue child = WorkflowValue::object({{"name", "Ada"}});
    WorkflowValue values = WorkflowValue::array();
    values.push_back(std::move(child));

    check((values.size() == 1));
    check((values[0]["name"].as<std::string>() == "Ada"));
}

spec("WorkflowValue transfers rvalue trees into containers") {
    it("complete case") { test_workflowvalue_transfers_rvalue_trees_into_containers_4(); }
}

[[maybe_unused]] static void test_structureddocumentquery_uses_jsonpath_5() {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Json,
        R"({"users":[{"name":"Ada"},{"name":"Lin"}]})",
        "$.users[*].name");

    check((result.is_array()));
    check((result.size() == 2));
    check((result[0].as<std::string>() == "Ada"));
    check((result[1].as<std::string>() == "Lin"));
}

spec("StructuredDocumentQuery uses JSONPath") {
    it("complete case") { test_structureddocumentquery_uses_jsonpath_5(); }
}

[[maybe_unused]] static void test_structureddocumentquery_uses_ypath_6() {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Yaml,
        "users:\n  - name: Ada\n  - name: Lin\n",
        "/users[*]/name");

    check((result.is_array()));
    check((result.size() == 2));
    check((result[0].as<std::string>() == "Ada"));
    check((result[1].as<std::string>() == "Lin"));
}

spec("StructuredDocumentQuery uses YPATH") {
    it("complete case") { test_structureddocumentquery_uses_ypath_6(); }
}

[[maybe_unused]] static void test_structureddocumentquery_uses_xpath_7() {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Xml,
        "<users><name>Ada</name><name>Lin</name></users>",
        "//name/text()");

    check((result.is_array()));
    check((result.size() == 2));
    check((result[0].as<std::string>() == "Ada"));
    check((result[1].as<std::string>() == "Lin"));
}

spec("StructuredDocumentQuery uses XPath") {
    it("complete case") { test_structureddocumentquery_uses_xpath_7(); }
}

[[maybe_unused]] static void test_structureddocumentquery_uses_csvpath_filters_8() {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Csv,
        "name,score_n\r\nAda,9\r\nLin,7\r\n",
        "score_n >= 8");

    check((result.is_array()));
    check((result.size() == 1));
    check((result[0]["name"].as<std::string>() == "Ada"));
    check((result[0]["score_n"].as<std::string>() == "9"));
}

spec("StructuredDocumentQuery uses CSVPath filters") {
    it("complete case") { test_structureddocumentquery_uses_csvpath_filters_8(); }
}

spec("StructuredDocumentQuery C migration bridge") {
    it("owns query results independently of the borrowed WorkflowValue") {
        WorkflowValue result;
        {
            WorkflowValue root = WorkflowValue::parse(R"({"nested":{"value":7}})");
            result = StructuredDocumentQuery::queryJson(root, "nested");
            result["value"] = 9;
            check((root["nested"]["value"].as<int>() == 7));
        }
        check((result["value"].as<int>() == 9));
    }
    it("retains invalid-input diagnostics for existing callers") {
        check(praktor_test::throws_containing([] {
            (void)StructuredDocumentQuery::query(StructuredFormat::Json, "{", "$");
        }, {"Invalid JSON input"}));
        check(praktor_test::throws_containing([] {
            (void)StructuredDocumentQuery::query(StructuredFormat::Xml, "<root/>", "");
        }, {"XPath expression must not be empty"}));
        check(praktor_test::throws_containing([] {
            (void)StructuredDocumentQuery::query(StructuredFormat::Csv, "name\nAda\n", "");
        }, {"CSVPath expression must not be empty"}));
    }
}

spec("YAML C ownership through the temporary parser facade") {
    it("preserves borrowed nodes across moves and owns decoded strings") {
        TaskYamlDetail::YamlDocument document("name: 'Ada'\nvalues: [1, 2]\n");
        const auto root = document.root();
        const auto name = root["name"];
        TaskYamlDetail::YamlDocument moved(std::move(document));
        check(!document.root().valid());
        check(name.scalar() == "Ada" && root.num_children() == 2);
        TaskYamlDetail::YamlDocument replacement("old: discarded\n");
        replacement = std::move(moved);
        check(!moved.root().valid() && root["values"].num_children() == 2);
        check(name.location() == " at line 1, column 1");
        size_t count = 0;
        for (const auto& child : root) { check(!child.key().empty()); ++count; }
        check(count == 2);
    }
    it("preserves quoted decimal integers, boolean classification and error locations") {
        TaskYamlDetail::YamlDocument document("good: '7'\nbad: +1\nflag: 1\ntext: '1'\n");
        int result = 71;
        document.root()["good"] >> result;
        check(result == 7);
        check(document.root()["flag"].bool_integer_value() == true);
        check(!document.root()["text"].bool_integer_value().has_value());
        check(praktor_test::throws_containing([&] { document.root()["bad"] >> result; },
            {"Expected YAML integer at line 2, column 1"}));
        check(result == 7);
        check(praktor_test::throws_containing([] {
            TaskYamlDetail::YamlDocument invalid("value: 1\nvalue: 2\n");
        }, {"Duplicate mapping key at line 2, column 1"}));
    }
}

spec("YAML field grammar through the temporary exception boundary") {
    it("retains the strict and force-terminate grammar split") {
        TaskYamlDetail::YamlDocument document("strict: 'YeS'\ncompat: '0X__1_'\nstring: 'true'\nnumber: 7\n");
        const auto root = document.root();
        check(TaskYamlDetail::read_bool_or_throw(root["strict"], "silent"));
        check(TaskYamlDetail::read_force_terminate_or_throw(root["compat"]));
        check(TaskYamlDetail::read_system_action_string_or_throw(root["string"], "must be a string") == "true");
        check(TaskYamlDetail::read_scalar_or_throw(root["number"], "scalar") == "7");
        check(praktor_test::throws_containing([&] {
            (void)TaskYamlDetail::read_bool_or_throw(root["compat"], "silent");
        }, {"Parse error at line 2, column 1: 'silent' must be a boolean"}));
        check(praktor_test::throws_containing([&] {
            (void)TaskYamlDetail::read_system_action_string_or_throw(root["number"], "must be a string");
        }, {"Parse error at line 4, column 1: must be a string"}));
    }
    it("retains long caller diagnostics without bounded-buffer truncation") {
        TaskYamlDetail::YamlDocument document("value: []\n");
        const std::string message(13000, 'x');
        bool caught = false;
        try { (void)TaskYamlDetail::read_scalar_or_throw(document.root()["value"], message); }
        catch (const std::runtime_error& error) {
            caught = std::string(error.what()) == "Parse error at line 1, column 1: " + message;
        }
        check(caught);
    }
}

spec("YAML structured values through the temporary exception boundary") {
    it("transfers the native JSON owner and validates decoded mapping keys") {
        TaskYamlDetail::YamlDocument document("value: {count: 9223372036854775807, flags: [true, 'false']}\n");
        auto result = WorkflowValue::adoptJson(TaskYamlDetail::read_contract_json_or_throw(document.root()["value"]));
        check(result["count"].as<std::int64_t>() == INT64_MAX);
        check(result["flags"][0].as<bool>());
        check(result["flags"][1].as<std::string>() == "false");
        TaskYamlDetail::check_unknown_keys(document.root()["value"], {"count", "flags"});
        check(praktor_test::throws_containing([&] {
            TaskYamlDetail::check_unknown_keys(document.root()["value"], {"count"});
        }, {"Unknown key: 'flags'", "line 1"}));
    }
    it("preserves contract null refusal through the C conversion") {
        TaskYamlDetail::YamlDocument document("value: {good: 1, bad: null}\n");
        check(praktor_test::throws_containing([&] {
            (void)TaskYamlDetail::read_contract_json_or_throw(document.root()["value"]);
        }, {"contract value must be a scalar, sequence, or map"}));
    }
}

spec("Native YAML action parameter projection through the temporary facade") {
    it("copies typed fields and defaults into current callers") {
        TaskYamlDetail::YamlDocument document(
            "download: {url: true, path: file, overwrite: yes, timeout_ms: 7}\n"
            "service: {name: worker, operation: restart, arguments: ['a', '']}\n"
            "managed_process: {identity: {image_name: worker}, operation: start, executable: exe, "
            "arguments: [one, two], working_directory: work, force_terminate: '0X__1_'}\n");
        const auto download = parse_download_params(document.root());
        check(download.url == "true" && download.path == "file" && download.overwrite && download.timeout_ms == 7);
        check(download.sha256.empty());
        const auto service = parse_service_params(document.root());
        check(service.operation == SystemOperation::Restart && service.profile == "windows_scm");
        check(service.name == "worker" && service.arguments == StrList({"a", ""}));
        check(service.timeout_ms == 30000 && service.poll_interval_ms == 200);
        const auto process = parse_managed_process_params(document.root());
        check(process.operation == SystemOperation::Start && process.identity.image_name == "worker");
        check(process.executable == "exe" && process.arguments == StrList({"one", "two"}));
        check(process.working_directory == "work" && process.force_terminate);
        check(process.startup_timeout_ms == 5000 && process.stop_timeout_ms == 5000);
    }
    it("preserves the first failure when later fields are also invalid") {
        TaskYamlDetail::YamlDocument document(
            "download: {url: '', path: [], overwrite: maybe}\n"
            "managed_process: {identity: {image_name: worker}, operation: restart, arguments: 1}\n");
        check(praktor_test::throws_containing([&] { (void)parse_download_params(document.root()); },
            {"download.path must be a scalar"}));
        check(praktor_test::throws_containing([&] { (void)parse_managed_process_params(document.root()); },
            {"managed_process.restart requires 'executable'"}));
    }
}

spec("Native Command and Program YAML projection through existing callers") {
    it("projects sequence commands and all postprocessor variants") {
        TaskYamlDetail::YamlDocument regex("command: [echo, 7]\noutput_format: JsOn\nparse_regex: {pattern: x, capture_group: -1, output_key: match}\n");
        const auto result = parse_run_command_params(regex.root());
        check(std::get<StrList>(result.command) == StrList({"echo", "7"}) && result.output_format == CommandOutputFormat::Json);
        check(result.working_directory.empty() && result.environment.empty());
        check(result.parse_regex && result.parse_regex->capture_group == -1 && result.parse_regex->pattern == "x");
        check(!result.parse_json && !result.parse_lines && !result.parse_keyvalue);
        TaskYamlDetail::YamlDocument json("command: true\nparse_json: {path: '', output_key: value}\n");
        const auto j = parse_run_command_params(json.root());
        check(std::get<std::string>(j.command) == "true" && j.parse_json && j.parse_json->path.empty());
        TaskYamlDetail::YamlDocument lines("command: echo\nparse_lines: {output_key: value}\n");
        check(parse_run_command_params(lines.root()).parse_lines->filter.empty());
        TaskYamlDetail::YamlDocument pairs("command: echo\nparse_keyvalue: {output_key: value}\n");
        const auto p = parse_run_command_params(pairs.root());
        check(p.parse_keyvalue && p.parse_keyvalue->delimiter == "=" && p.parse_keyvalue->line_separator == "\n");
    }
    it("projects Program values and preserves plain scalar-decode failures") {
        TaskYamlDetail::YamlDocument program("program: 7\nargs: ['a', '', true]\nstdin: 42\noutput_format: JSON\n");
        const auto p = parse_program_params(program.root());
        check(p.program == "7" && p.args == StrList({"a", "", "true"}) && p.input == "42" && p.output_format == CommandOutputFormat::Json);
        TaskYamlDetail::YamlDocument invalid("command: ['', {}]\n");
        check(praktor_test::throws_containing([&] { (void)parse_run_command_params(invalid.root()); }, {"Expected YAML scalar"}));
    }
}

spec("Native C task fields project into legacy task types") {
    it("preserves Each selection and duplicate dimension expansion order") {
        TaskYamlDetail::YamlDocument doc("items: []\nmatrix: {z: [one], x: [two], \"z\\0last\": [last]}\nas: ''\nindex_variable: n\n");
        const auto each = parse_each(doc.root());
        check(each.items.empty() && each.matrix.size() == 2 && each.as == "item" && each.index_variable == "n");
        check(each.matrix.at("z") == StrList({"last"}) && each.matrix.at("x") == StrList({"two"}));
        TaskYamlDetail::YamlDocument bad("matrix: {z: [], \"z\\0last\": [a]}\n");
        check(praktor_test::throws_containing([&] { (void)parse_each(bad.root()); }, {"dimension 'z' must not be empty"}));
    }
    it("projects shared containers triggers Uses and dynamic option presence") {
        TaskYamlDetail::YamlDocument list("['', true, 7]"), map("{z: old, x: a, \"z\\0last\": new}"), uses("true");
        check(node_to_string_vector(list.root()) == StrList({"", "true", "7"}));
        const auto vars = node_to_string_map(map.root());
        check(vars.size() == 2 && vars.at("z") == "new" && vars.at("x") == "a");
        check(parse_uses_params(uses.root()).path == "true");
        TaskYamlDetail::YamlDocument triggers("on_success: [a, true]\non_complete: [end]\n");
        const auto actions = parse_triggers(triggers.root());
        check(actions.on_success == StrList({"a", "true"}) && actions.on_failure.empty() && actions.on_complete == StrList({"end"}));
        TaskYamlDetail::YamlDocument dynamic("items_variable: jobs\ntemplate: {name: task, command: [echo, 7], timeout: '', when: '', depends_on: ready, env: {x: true}}\n");
        const auto params = parse_dynamic_tasks_params(dynamic.root());
        check(params.items_variable == "jobs" && params.task_template.name == "task");
        check(std::get<StrList>(params.task_template.command) == StrList({"echo", "7"}));
        check(params.task_template.timeout.has_value() && params.task_template.timeout->empty());
        check(params.task_template.when.has_value() && params.task_template.when->empty());
        check(params.task_template.depends_on == StrList({"ready"}) && params.task_template.env.at("x") == "true");
    }
}

spec("Native C orchestration projects into existing task consumers") {
    it("preserves catalog metadata lookup and node classification") {
        check(NODE_REGISTRY.size() == 31);
        const auto* shell = findNodeSpec("__sHeLl_");
        check(shell && shell->name == "Shell" && !shell->isControl && !shell->allowsChildren);
        check(shell->requiredParams == StrList({"cmd"}) && shell->optionalParams.back() == "stream_output");
        check(findNodeSpec("keep_running_until_failure")->name == "KeepRunningUntilFailure");
        check(!findNodeSpec(std::string("Shell\0tail", 10)) && !findNodeSpec("unknown"));
        OrchNode control; control.type = "Sequence";
        check(control.isControlNode() && !control.isLeafNode());
        control.type = "SubTree"; check(!control.isControlNode() && control.isLeafNode());
    }
    it("preserves binary parameters command order and postprocessor priority") {
        RunCommandParams input;
        input.command = StrList({std::string("a\0tail", 6), "", "last"});
        input.parse_regex = ParseRegexConfig{"x", -7, "match"};
        input.parse_json = ParseJsonConfig{"$", "ignored"};
        const auto result = desugarCommandToorch(input);
        check(result.root.type == "Sequence" && result.root.children.size() == 4);
        check(result.root.children[0].params.at("cmd") == std::string("a\0tail", 6));
        check(result.root.children[0].params.count("output_key") == 0);
        check(result.root.children[2].params.at("output_key") == "stdout");
        check(result.root.children[3].type == "ParseRegex" && result.root.children[3].params.at("capture_group") == "-7");
        input.command = StrList{}; input.parse_regex.reset(); input.parse_json.reset();
        check(desugarCommandToorch(input).root.children.empty());
        input.output_format = CommandOutputFormat::Json;
        check(desugarCommandToorch(input).root.children[0].params.at("output_key") == "data");
    }
}

#ifdef PRAKTOR_ORCH_FAULT_TEST
extern "C" void praktor_orch_test_fail(unsigned point);
extern "C" size_t praktor_orch_test_live(void);
spec("Native orchestration owned-allocation exception projection") {
    it("maps every C allocation boundary to bad_alloc and releases the complete tree") {
        RunCommandParams input;
        input.command = StrList({"one", "two"}); input.parse_regex = ParseRegexConfig{"(x)", -7, "match"};
        unsigned point;
        for (point = 1; point < 200; ++point) {
            bool allocation = false;
            praktor_orch_test_fail(point);
            try { (void)desugarCommandToorch(input); }
            catch (const std::bad_alloc&) { allocation = true; }
            catch (...) { praktor_orch_test_fail(0); throw; }
            praktor_orch_test_fail(0);
            check(praktor_orch_test_live() == 0);
            check(std::get<StrList>(input.command) == StrList({"one", "two"}));
            if (!allocation) break;
        }
        check(point > 1 && point < 200);
    }
}
#endif

#ifdef PRAKTOR_YAML_FAULT_TEST
extern "C" void praktor_yaml_test_fail(unsigned point);

// Restore fault injection even if an assertion unwinds the TinyTest case.
struct YamlFaultScope {
    explicit YamlFaultScope(unsigned point) { praktor_yaml_test_fail(point); }
    ~YamlFaultScope() { praktor_yaml_test_fail(0); }
};

template<class Operation>
static void yaml_failure_kind(unsigned point, bool allocation, Operation operation) {
    bool caught = false;
    try { YamlFaultScope fault(point); operation(); }
    catch (const std::bad_alloc&) { caught = allocation; }
    catch (const std::runtime_error&) { caught = !allocation; }
    check(caught);
}

spec("YAML typed failure origin preserves legacy allocation exceptions") {
    it("distinguishes owner/source allocation from provider parse failure") {
        for (unsigned point = 1; point <= 3; ++point)
            yaml_failure_kind(point, point != 3, [] {
                TaskYamlDetail::YamlDocument document("first: text\nvalue: '7'\n");
            });
    }
    it("distinguishes provider scalar decoding from owned text allocation") {
        TaskYamlDetail::YamlDocument document("first: text\nvalue: '7'\n");
        const auto value = *++document.root().begin();
        yaml_failure_kind(1, false, [&] { (void)value.scalar(); });
        yaml_failure_kind(2, true, [&] { (void)value.scalar(); });
        yaml_failure_kind(1, false, [&] { (void)value.key(); });
        yaml_failure_kind(2, true, [&] { (void)value.key(); });
        check(value.scalar() == "7");
    }
    it("preserves lookup, integer and location allocation categories") {
        TaskYamlDetail::YamlDocument document("first: text\nvalue: '7'\n");
        const auto root = document.root(), value = *++root.begin();
        for (unsigned point = 1; point <= 4; ++point)
            yaml_failure_kind(point, point % 2 == 0, [&] { (void)root["value"]; });
        int number = 71;
        yaml_failure_kind(1, false, [&] { value >> number; });
        yaml_failure_kind(2, true, [&] { value >> number; });
        check(number == 71);
        yaml_failure_kind(1, true, [&] { (void)value.location(); });
        value >> number;
        check(number == 7);
    }
    it("preserves field decoding and diagnostic-location exception categories") {
        TaskYamlDetail::YamlDocument document("value: maybe\nshape: []\n");
        const auto value = *document.root().begin(), shape = *++document.root().begin();
        yaml_failure_kind(1, false, [&] { (void)TaskYamlDetail::read_scalar_or_throw(value, "scalar"); });
        yaml_failure_kind(2, true, [&] { (void)TaskYamlDetail::read_scalar_or_throw(value, "scalar"); });
        yaml_failure_kind(1, false, [&] { (void)TaskYamlDetail::read_bool_or_throw(value, "silent"); });
        yaml_failure_kind(2, true, [&] { (void)TaskYamlDetail::read_bool_or_throw(value, "silent"); });
        yaml_failure_kind(3, true, [&] { (void)TaskYamlDetail::read_bool_or_throw(value, "silent"); });
        yaml_failure_kind(1, true, [&] { (void)TaskYamlDetail::read_scalar_or_throw(shape, "scalar"); });
        check(TaskYamlDetail::read_scalar_or_throw(value, "scalar") == "maybe");
    }
    it("preserves value and whitelist provider-vs-owned decoding failures") {
        TaskYamlDetail::YamlDocument document("value: 7\n");
        const auto root = document.root(), value = *root.begin();
        yaml_failure_kind(1, false, [&] { (void)TaskYamlDetail::read_contract_json_or_throw(value); });
        yaml_failure_kind(2, true, [&] { (void)TaskYamlDetail::read_contract_json_or_throw(value); });
        yaml_failure_kind(1, false, [&] { TaskYamlDetail::check_unknown_keys(root, {"value"}); });
        yaml_failure_kind(2, true, [&] { TaskYamlDetail::check_unknown_keys(root, {"value"}); });
        auto result = WorkflowValue::adoptJson(TaskYamlDetail::read_contract_json_or_throw(value));
        check(result.as<int>() == 7);
    }
    it("preserves provider versus owned-text failures through action constructors") {
        TaskYamlDetail::YamlDocument document("service: {name: worker}\n");
        const auto root = document.root();
        yaml_failure_kind(1, false, [&] { (void)parse_service_params(root); });
        yaml_failure_kind(2, true, [&] { (void)parse_service_params(root); });
        const auto result = parse_service_params(root);
        check(result.name == "worker" && result.timeout_ms == 30000);
    }
    it("retains Command and Program constructor allocation/provider categories") {
        TaskYamlDetail::YamlDocument command("command: echo\nparse_regex: {pattern: x, output_key: match}\n");
        TaskYamlDetail::YamlDocument program("program: tool\nargs: [one, two]\n");
        yaml_failure_kind(1, false, [&] { (void)parse_run_command_params(command.root()); });
        yaml_failure_kind(2, true, [&] { (void)parse_run_command_params(command.root()); });
        yaml_failure_kind(1, false, [&] { (void)parse_program_params(program.root()); });
        yaml_failure_kind(2, true, [&] { (void)parse_program_params(program.root()); });
        check(parse_run_command_params(command.root()).parse_regex->output_key == "match");
        check(parse_program_params(program.root()).args.size() == 2);
    }
    it("retains provider and owned text exception categories through task fields") {
        TaskYamlDetail::YamlDocument each("items: [a]"), dynamic("items_variable: jobs\ntemplate: {name: task, command: echo}"), uses("path");
        yaml_failure_kind(1, false, [&] { (void)parse_each(each.root()); });
        yaml_failure_kind(2, true, [&] { (void)parse_each(each.root()); });
        yaml_failure_kind(1, false, [&] { (void)parse_dynamic_tasks_params(dynamic.root()); });
        yaml_failure_kind(2, true, [&] { (void)parse_dynamic_tasks_params(dynamic.root()); });
        yaml_failure_kind(1, false, [&] { (void)parse_uses_params(uses.root()); });
        yaml_failure_kind(2, true, [&] { (void)parse_uses_params(uses.root()); });
        check(parse_each(each.root()).items == StrList({"a"}));
    }

}
#endif
