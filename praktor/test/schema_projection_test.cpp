#include "projection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <string>

namespace {
std::string readSource(const char* path) {
    std::ifstream input(std::string(PRAKTOR_SCHEMA_SOURCE_DIR) + "/" + path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read schema fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool equal(const WorkflowValue& left, const WorkflowValue& right) {
    if (left.is_object() && right.is_object()) {
        if (left.size() != right.size()) return false;
        for (const auto& member : left.object_range()) {
            if (!right.contains(member.key()) || !equal(member.value(), right[member.key()])) return false;
        }
        return true;
    }
    if (left.is_array() && right.is_array()) {
        if (left.size() != right.size()) return false;
        for (size_t i = 0; i < left.size(); ++i) {
            if (!equal(left[i], right[i])) return false;
        }
        return true;
    }
    return left.to_string() == right.to_string();
}
}

TEST_CASE("checked-in editor schema matches the published SDK IDL projection") {
    const auto generated = Praktor::Schema::project(readSource("schema/workflow.schema"),
        WorkflowValue::parse(readSource("schema/workflow.editor.json")));
    const auto checkedIn = WorkflowValue::parse(readSource("grammar.schema.json"));
    REQUIRE(equal(generated, checkedIn));
    const WorkflowValue task = generated["$defs"]["taskDefinition"];
    CHECK(task["properties"]["tool"]["minLength"].as<int>() == 1);
    CHECK(task["dependencies"]["with"][0].as<std::string>() == "tool");
    CHECK(task["properties"]["with"]["type"].as<std::string>() == "object");
    CHECK(generated["$defs"]["workflowInputField"]["properties"]["default"].size() == 0);
    CHECK(generated["$defs"]["eachBlock"]["properties"]["items"]["oneOf"][0]["minLength"].as<int>() == 1);
}

TEST_CASE("IDL shapes, aliases, presence, defaults and constraints drive projection") {
    const auto result = Praktor::Schema::project(R"(
        schema Projection [version(1)];
        message Workflow {
            @Size(min = 1, max = 9) [name("title")] string label;
            optional bool enabled default false;
            optional string policy default "strict";
            optional @Min(0) @Max(1024) int64 retries;
            optional @Size(min = 1) list<string> arguments;
            optional @Pattern("^[a-z]+$") string tag;
        }
    )", WorkflowValue::parse(R"({"$defs":{},"properties":{"title":{"description":"A title"}}})"));
    const auto properties = result["properties"];
    CHECK(properties["title"]["type"].as<std::string>() == "string");
    CHECK(properties["title"]["minLength"].as<int>() == 1);
    CHECK(properties["title"]["maxLength"].as<int>() == 9);
    CHECK(properties["title"]["description"].as<std::string>() == "A title");
    CHECK(result["required"].size() == 1);
    CHECK(result["required"][0].as<std::string>() == "title");
    CHECK_FALSE(properties["enabled"]["default"].as<bool>());
    CHECK(properties["policy"]["default"].as<std::string>() == "strict");
    CHECK(properties["retries"]["minimum"].as<int>() == 0);
    CHECK(properties["retries"]["maximum"].as<int>() == 1024);
    CHECK(properties["arguments"]["items"]["type"].as<std::string>() == "string");
    CHECK(properties["arguments"]["minItems"].as<int>() == 1);
    CHECK(properties["tag"]["pattern"].as<std::string>() == "^[a-z]+$");
}

TEST_CASE("schema projection rejects conflicting ownership and unsupported IDL") {
    const auto empty = WorkflowValue::parse(R"({"$defs":{}})");
    CHECK_THROWS(Praktor::Schema::project("message Workflow { string value;", empty));
    CHECK_THROWS(Praktor::Schema::project("message Workflow { Unknown value; }", empty));
    CHECK_THROWS(Praktor::Schema::project("message Workflow { bytes value; }", empty));
    CHECK_THROWS(Praktor::Schema::project("message Workflow { string value; }",
        WorkflowValue::parse(R"({"$defs":{},"properties":{"value":{"type":"integer"}}})")));
    CHECK_THROWS(Praktor::Schema::project("message Workflow { [alias(old)] string value; }", empty));
    CHECK_THROWS(Praktor::Schema::project("message Workflow { [name(x)] string a; [name(x)] string b; }", empty));
}
