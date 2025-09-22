#include "util/expression_evaluator.hpp"
#include "dag/workflow_context.hpp"

#include <catch2/catch_all.hpp>
#include <stdexcept>

TEST_CASE("ExpressionEvaluator Tests", "[evaluator]") {
    Weave::Util::ExpressionEvaluator evaluator;
    WorkflowContext context;

    SECTION("Simple Equality") {
        context.setValue("env", "prod");
        REQUIRE(evaluator.evaluate("{{ env }} == 'prod'", context) == true);
        REQUIRE(evaluator.evaluate("{{ env }} == 'dev'", context) == false);
    }

    SECTION("Not Equal") {
        context.setValue("env", "prod");
        REQUIRE(evaluator.evaluate("{{ env }} != 'dev'", context) == true);
        REQUIRE(evaluator.evaluate("{{ env }} != 'prod'", context) == false);
    }

    SECTION("Boolean Literals") {
        REQUIRE(evaluator.evaluate("true == true", context) == true);
        REQUIRE(evaluator.evaluate("true != false", context) == true);
        REQUIRE(evaluator.evaluate("true", context) == true);
        REQUIRE(evaluator.evaluate("false", context) == false);
    }

    SECTION("Number Literals") {
        REQUIRE(evaluator.evaluate("123 == 123.0", context) == true);
        REQUIRE(evaluator.evaluate("123 != 456", context) == true);
    }

    SECTION("Logical AND") {
        context.setValue("env", "prod");
        context.setValue("tag", "v1.0");
        REQUIRE(evaluator.evaluate("{{ env }} == 'prod' and {{ tag }} == 'v1.0'", context) == true);
        REQUIRE(evaluator.evaluate("{{ env }} == 'prod' and {{ tag }} == 'v2.0'", context) == false);
    }

    SECTION("Logical OR") {
        context.setValue("env", "staging");
        REQUIRE(evaluator.evaluate("{{ env }} == 'prod' or {{ env }} == 'staging'", context) == true);
        REQUIRE(evaluator.evaluate("{{ env }} == 'prod' or {{ env }} == 'dev'", context) == false);
    }

    SECTION("Parentheses for Precedence") {
        context.setValue("a", "1");
        context.setValue("b", "2");
        context.setValue("c", "3");
        REQUIRE(evaluator.evaluate("({{ a }} == '1' or {{ b }} == '1') and {{ c }} == '3'", context) == true);
        REQUIRE(evaluator.evaluate("{{ a }} == '1' or ({{ b }} == '1' and {{ c }} == '3')", context) == true);
        REQUIRE(evaluator.evaluate("{{ a }} == '2' or ({{ b }} == '2' and {{ c }} == '4')", context) == false);
    }

    SECTION("Truthy evaluation of single variable") {
        context.setValue("my_var", "some_value");
        context.setValue("empty_var", "");
        REQUIRE(evaluator.evaluate("{{ my_var }}", context) == true);
        REQUIRE(evaluator.evaluate("{{ empty_var }}", context) == false);
        REQUIRE(evaluator.evaluate("{{ non_existent_var }}", context) == false);
    }

    SECTION("Invalid Expression") {
        REQUIRE_THROWS_AS(evaluator.evaluate("{{ env }} === 'prod'", context), std::runtime_error);
        REQUIRE_THROWS_AS(evaluator.evaluate("{{ env }} == 'prod", context), std::runtime_error); // Missing closing quote
        REQUIRE_THROWS_AS(evaluator.evaluate("{{ env }} and 'prod'", context), std::runtime_error);
    }

    SECTION("Whitespace Handling") {
        context.setValue("my var", "value");
        REQUIRE(evaluator.evaluate("{{ my var }} == 'value'", context) == true);
        REQUIRE(evaluator.evaluate("  true  and  false  ", context) == false);
    }
}
