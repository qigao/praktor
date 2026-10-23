#include "praktor_test.hpp"

#include "data/structured_document_query.hpp"

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
