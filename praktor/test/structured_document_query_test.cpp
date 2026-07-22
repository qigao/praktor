#include <catch2/catch_test_macros.hpp>

#include "data/structured_document_query.hpp"

using Praktor::Data::StructuredDocumentQuery;
using Praktor::Data::StructuredFormat;

TEST_CASE("WorkflowValue copies own independent Turbo JSON trees", "[workflow-value]") {
    WorkflowValue original = WorkflowValue::object();
    original["nested"] = WorkflowValue::object();
    original["nested"]["value"] = 7;

    WorkflowValue copy = original;
    copy["nested"]["value"] = 9;

    CHECK(original["nested"]["value"].as<int>() == 7);
    CHECK(copy["nested"]["value"].as<int>() == 9);
}

TEST_CASE("WorkflowValue distinguishes object keys from array indexes", "[workflow-value]") {
    WorkflowValue value = WorkflowValue::object();
    value["#0"] = "object member";

    CHECK(value["#0"].as<std::string>() == "object member");
}

TEST_CASE("WorkflowValue promotes null to an object on member assignment", "[workflow-value]") {
    WorkflowValue value;
    value["task"] = "build";

    REQUIRE(value.is_object());
    CHECK(value["task"].as<std::string>() == "build");
}

TEST_CASE("WorkflowValue transfers rvalue trees into containers", "[workflow-value]") {
    WorkflowValue child = WorkflowValue::object({{"name", "Ada"}});
    WorkflowValue values = WorkflowValue::array();
    values.push_back(std::move(child));

    REQUIRE(values.size() == 1);
    CHECK(values[0]["name"].as<std::string>() == "Ada");
}

TEST_CASE("StructuredDocumentQuery uses JSONPath", "[structured-query]") {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Json,
        R"({"users":[{"name":"Ada"},{"name":"Lin"}]})",
        "$.users[*].name");

    REQUIRE(result.is_array());
    REQUIRE(result.size() == 2);
    CHECK(result[0].as<std::string>() == "Ada");
    CHECK(result[1].as<std::string>() == "Lin");
}

TEST_CASE("StructuredDocumentQuery uses YPATH", "[structured-query]") {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Yaml,
        "users:\n  - name: Ada\n  - name: Lin\n",
        "/users[*]/name");

    REQUIRE(result.is_array());
    REQUIRE(result.size() == 2);
    CHECK(result[0].as<std::string>() == "Ada");
    CHECK(result[1].as<std::string>() == "Lin");
}

TEST_CASE("StructuredDocumentQuery uses XPath", "[structured-query]") {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Xml,
        "<users><name>Ada</name><name>Lin</name></users>",
        "//name/text()");

    REQUIRE(result.is_array());
    REQUIRE(result.size() == 2);
    CHECK(result[0].as<std::string>() == "Ada");
    CHECK(result[1].as<std::string>() == "Lin");
}

TEST_CASE("StructuredDocumentQuery uses CSVPath filters", "[structured-query]") {
    const auto result = StructuredDocumentQuery::query(
        StructuredFormat::Csv,
        "name,score_n\r\nAda,9\r\nLin,7\r\n",
        "score_n >= 8");

    REQUIRE(result.is_array());
    REQUIRE(result.size() == 1);
    CHECK(result[0]["name"].as<std::string>() == "Ada");
    CHECK(result[0]["score_n"].as<std::string>() == "9");
}
