#include "praktor_test.hpp"
#include "projection.hpp"


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

[[maybe_unused]] static void test_checked_in_editor_schema_matches_the_published_sdk_idl_projection_1() {
    const auto generated = Praktor::Schema::project(readSource("schema/workflow.schema"),
        WorkflowValue::parse(readSource("schema/workflow.editor.json")));
    const auto checkedIn = WorkflowValue::parse(readSource("grammar.schema.json"));
    check((equal(generated, checkedIn)));
    const WorkflowValue task = generated["$defs"]["taskDefinition"];
    check((task["properties"]["tool"]["minLength"].as<int>() == 1));
    check((task["dependencies"]["with"][0].as<std::string>() == "tool"));
    check((task["properties"]["with"]["type"].as<std::string>() == "object"));
    check((generated["$defs"]["workflowInputField"]["properties"]["default"].size() == 0));
    check((generated["$defs"]["eachBlock"]["properties"]["items"]["oneOf"][0]["minLength"].as<int>() == 1));
}

spec("checked-in editor schema matches the published SDK IDL projection") {
    it("complete case") { test_checked_in_editor_schema_matches_the_published_sdk_idl_projection_1(); }
}

[[maybe_unused]] static void test_idl_shapes_aliases_presence_defaults_and_constraints_drive_projection_2() {
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
    check((properties["title"]["type"].as<std::string>() == "string"));
    check((properties["title"]["minLength"].as<int>() == 1));
    check((properties["title"]["maxLength"].as<int>() == 9));
    check((properties["title"]["description"].as<std::string>() == "A title"));
    check((result["required"].size() == 1));
    check((result["required"][0].as<std::string>() == "title"));
    check((!(properties["enabled"]["default"].as<bool>())));
    check((properties["policy"]["default"].as<std::string>() == "strict"));
    check((properties["retries"]["minimum"].as<int>() == 0));
    check((properties["retries"]["maximum"].as<int>() == 1024));
    check((properties["arguments"]["items"]["type"].as<std::string>() == "string"));
    check((properties["arguments"]["minItems"].as<int>() == 1));
    check((properties["tag"]["pattern"].as<std::string>() == "^[a-z]+$"));
}

spec("IDL shapes, aliases, presence, defaults and constraints drive projection") {
    it("complete case") { test_idl_shapes_aliases_presence_defaults_and_constraints_drive_projection_2(); }
}

[[maybe_unused]] static void test_schema_projection_rejects_conflicting_ownership_and_unsupported_idl_3() {
    const auto empty = WorkflowValue::parse(R"({"$defs":{}})");
    check_throws(Praktor::Schema::project("message Workflow { string value;", empty));
    check_throws(Praktor::Schema::project("message Workflow { Unknown value; }", empty));
    check_throws(Praktor::Schema::project("message Workflow { bytes value; }", empty));
    check_throws(Praktor::Schema::project("message Workflow { string value; }",
        WorkflowValue::parse(R"({"$defs":{},"properties":{"value":{"type":"integer"}}})")));
    check_throws(Praktor::Schema::project("message Workflow { [alias(old)] string value; }", empty));
    check_throws(Praktor::Schema::project("message Workflow { [name(x)] string a; [name(x)] string b; }", empty));
}

spec("schema projection rejects conflicting ownership and unsupported IDL") {
    it("complete case") { test_schema_projection_rejects_conflicting_ownership_and_unsupported_idl_3(); }
}
