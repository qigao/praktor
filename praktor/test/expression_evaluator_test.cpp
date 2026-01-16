#include "expressions/expression_evaluator.hpp"
#include "dag/workflow_context.hpp"

#include <catch2/catch_all.hpp>
#include <stdexcept>

using namespace Praktor::Expressions;

TEST_CASE("ExpressionEvaluator Tests", "[evaluator]") {
    ExpressionEvaluator evaluator;
    WorkflowContext context;

    SECTION("Simple Equality") {
        context.setValue("env", "prod");
        REQUIRE(evaluator.evaluateAsBool("{{ env }} == 'prod'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ env }} == 'dev'", context) == false);
    }

    SECTION("Not Equal") {
        context.setValue("env", "prod");
        REQUIRE(evaluator.evaluateAsBool("{{ env }} != 'dev'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ env }} != 'prod'", context) == false);
    }

    SECTION("Boolean Literals") {
        REQUIRE(evaluator.evaluateAsBool("true == true", context) == true);
        REQUIRE(evaluator.evaluateAsBool("true != false", context) == true);
        REQUIRE(evaluator.evaluateAsBool("true", context) == true);
        REQUIRE(evaluator.evaluateAsBool("false", context) == false);
    }

    SECTION("Number Literals") {
        REQUIRE(evaluator.evaluateAsBool("123 == 123.0", context) == true);
        REQUIRE(evaluator.evaluateAsBool("123 != 456", context) == true);
    }

    SECTION("Logical AND") {
        context.setValue("env", "prod");
        context.setValue("tag", "v1.0");
        REQUIRE(evaluator.evaluateAsBool("{{ env }} == 'prod' and {{ tag }} == 'v1.0'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ env }} == 'prod' and {{ tag }} == 'v2.0'", context) == false);
    }

    SECTION("Logical OR") {
        context.setValue("env", "staging");
        REQUIRE(evaluator.evaluateAsBool("{{ env }} == 'prod' or {{ env }} == 'staging'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ env }} == 'prod' or {{ env }} == 'dev'", context) == false);
    }

    SECTION("Parentheses for Precedence") {
        context.setValue("a", "1");
        context.setValue("b", "2");
        context.setValue("c", "3");
        REQUIRE(evaluator.evaluateAsBool("({{ a }} == '1' or {{ b }} == '1') and {{ c }} == '3'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ a }} == '1' or ({{ b }} == '1' and {{ c }} == '3')", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ a }} == '2' or ({{ b }} == '2' and {{ c }} == '4')", context) == false);
    }

    SECTION("Truthy evaluation of single variable") {
        context.setValue("my_var", "some_value");
        context.setValue("empty_var", "");
        REQUIRE(evaluator.evaluateAsBool("{{ my_var }}", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ empty_var }}", context) == false);
        REQUIRE(evaluator.evaluateAsBool("{{ non_existent_var }}", context) == false);
    }

    SECTION("Less Than") {
        context.setValue("count", "5");
        REQUIRE(evaluator.evaluateAsBool("{{ count }} < 10", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} < 5", context) == false);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} < 3", context) == false);
    }

    SECTION("Greater Than") {
        context.setValue("count", "5");
        REQUIRE(evaluator.evaluateAsBool("{{ count }} > 3", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} > 5", context) == false);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} > 10", context) == false);
    }

    SECTION("Less Than or Equal") {
        context.setValue("count", "5");
        REQUIRE(evaluator.evaluateAsBool("{{ count }} <= 10", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} <= 5", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} <= 3", context) == false);
    }

    SECTION("Greater Than or Equal") {
        context.setValue("count", "5");
        REQUIRE(evaluator.evaluateAsBool("{{ count }} >= 3", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} >= 5", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ count }} >= 10", context) == false);
    }

    SECTION("Logical NOT") {
        REQUIRE(evaluator.evaluateAsBool("not true", context) == false);
        REQUIRE(evaluator.evaluateAsBool("not false", context) == true);
        context.setValue("flag", "yes");
        REQUIRE(evaluator.evaluateAsBool("not {{ flag }}", context) == false);
        context.setValue("empty", "");
        REQUIRE(evaluator.evaluateAsBool("not {{ empty }}", context) == true);
    }

    SECTION("Combined Comparison and Logical") {
        context.setValue("age", "25");
        REQUIRE(evaluator.evaluateAsBool("{{ age }} >= 18 and {{ age }} < 65", context) == true);
        REQUIRE(evaluator.evaluateAsBool("{{ age }} < 18 or {{ age }} >= 65", context) == false);
        REQUIRE(evaluator.evaluateAsBool("not ({{ age }} < 18)", context) == true);
    }

    SECTION("New Operators") {
        REQUIRE(evaluator.evaluateAsBool("'hello world' contains 'world'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("'hello world' starts_with 'hello'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("'hello world' endswith 'world'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("'world' in 'hello world'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("'admin@test.com' matches '.*@.*\\.com'", context) == true);
    }

    SECTION("Invalid Expression") {
        REQUIRE_THROWS_AS(evaluator.evaluateAsBool("{{ env }} === 'prod'", context), std::runtime_error);
        REQUIRE_THROWS_AS(evaluator.evaluateAsBool("{{ env }} == 'prod", context), std::runtime_error); // Missing closing quote
    }

    SECTION("Whitespace Handling") {
        context.setValue("my var", "value");
        REQUIRE(evaluator.evaluateAsBool("{{ my var }} == 'value'", context) == true);
        REQUIRE(evaluator.evaluateAsBool("  true  and  false  ", context) == false);
    }
}
