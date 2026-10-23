#include "praktor_test.hpp"

#include "data/structured_document_query.hpp"
#include "../src/yml/yaml_document.hpp"
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
}
#endif
