#include "praktor_test.hpp"
#include "data/workflow_value.hpp"
#include <cstdint>
#include <limits>

template<class Exception, class Function>
static void expect_error(Function function, const char* message) {
    bool caught = false;
    try { function(); }
    catch (const Exception& error) {
        caught = true;
        check(std::string(error.what()) == message);
    }
    check(caught);
}

spec("WorkflowValue temporary C++ adapter to the C value core") {
    it("preserves exact integer and floating conversions and exception types") {
        check(WorkflowValue::parse("18446744073709551615").as<uint64_t>() == UINT64_MAX);
        check(WorkflowValue::parse("-9223372036854775808").as<int64_t>() == INT64_MIN);
        check(WorkflowValue::parse("7.0").as<int>() == 7);
        check(WorkflowValue::parse("-0.0").as<unsigned>() == 0);
        expect_error<std::out_of_range>([] { (void)WorkflowValue::parse("256").as<uint8_t>(); },
            "WorkflowValue integer is out of range");
        expect_error<std::out_of_range>([] { (void)WorkflowValue::parse("-0").as<unsigned>(); },
            "WorkflowValue integer is out of range");
        expect_error<std::runtime_error>([] { (void)WorkflowValue("7").as<int>(); },
            "WorkflowValue is not a number");
        expect_error<std::out_of_range>([] { (void)WorkflowValue::parse("9223372036854775808.0").as<int64_t>(); },
            "WorkflowValue number cannot be represented as an integer");
        expect_error<std::out_of_range>([] { (void)WorkflowValue::parse("18446744073709551616.0").as<uint64_t>(); },
            "WorkflowValue number cannot be represented as an integer");
    }
    it("keeps nested copy, move, self insertion and erase behavior") {
        WorkflowValue root;
        WorkflowValue source = WorkflowValue::array({1, 2});
        root["a"]["b"] = std::move(source);
        check(source.raw() == nullptr);
        root["a"]["b"].push_back(3);
        WorkflowValue copy = root;
        root["a"]["b"].push_back(std::move(root));
        check(copy.to_string() == "{\"a\":{\"b\":[1,2,3]}}");
        WorkflowValue array = root["a"]["b"].get();
        array.erase_at(0);
        check(array.at(0).as<int>() == 2 && array.size() == 3);
        expect_error<std::out_of_range>([&] { array.erase_at(3); }, "WorkflowValue array index out of range");
    }
    it("retains legacy C-string key semantics and binary value lengths") {
        const std::string key("a\0b", 3), text("x\0y", 3);
        WorkflowValue root = WorkflowValue::object();
        root[key] = text;
        check(root.contains("a") && root.at(key).as<std::string>() == text);
        check(root.at("a").size() == 3);
        expect_error<std::runtime_error>([&] { root[key]["bad"] = WorkflowValue::null(); },
            "Failed to assign WorkflowValue path");
    }
    it("fails closed for null children and bounded proxy paths") {
        WorkflowValue root = WorkflowValue::object({{"a", nullptr}});
        expect_error<std::runtime_error>([&] { root["a"]["b"] = 7; }, "Failed to assign WorkflowValue path");
        check(root.to_string() == "{\"a\":null}");
        WorkflowValue::Proxy proxy(root, std::vector<std::string>(PRAKTOR_VALUE_MAX_PATH + 1, "x"));
        expect_error<std::runtime_error>([&] { proxy = 7; }, "Invalid WorkflowValue path or source");
        check(root.to_string() == "{\"a\":null}");
    }
}
