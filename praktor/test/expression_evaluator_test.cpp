#include "praktor_test.hpp"
#include "expressions/expression_evaluator.hpp"
#include "dag/workflow_context.hpp"


using namespace Praktor::Expressions;

[[maybe_unused]] static void test_expressionevaluator_tests_1(int section_index) {
    ExpressionEvaluator evaluator;
    WorkflowContext context;

    if (section_index == 0) {
        context.setValue("env", "prod");
        check((evaluator.evaluateAsBool("$env == 'prod'", context) == true));
        check((evaluator.evaluateAsBool("$env == 'dev'", context) == false));
    }

    if (section_index == 1) {
        context.setValue("env", "prod");
        check((evaluator.evaluateAsBool("$env != 'dev'", context) == true));
        check((evaluator.evaluateAsBool("$env != 'prod'", context) == false));
    }

    if (section_index == 2) {
        check((evaluator.evaluateAsBool("true == true", context) == true));
        check((evaluator.evaluateAsBool("true != false", context) == true));
        check((evaluator.evaluateAsBool("true", context) == true));
        check((evaluator.evaluateAsBool("false", context) == false));
    }

    if (section_index == 3) {
        check((evaluator.evaluateAsBool("123 == 123.0", context) == true));
        check((evaluator.evaluateAsBool("123 != 456", context) == true));
    }

    if (section_index == 4) {
        context.setValue("env", "prod");
        context.setValue("tag", "v1.0");
        check((evaluator.evaluateAsBool("$env == 'prod' && $tag == 'v1.0'", context) == true));
        check((evaluator.evaluateAsBool("$env == 'prod' && $tag == 'v2.0'", context) == false));
    }

    if (section_index == 5) {
        context.setValue("env", "staging");
        check((evaluator.evaluateAsBool("$env == 'prod' || $env == 'staging'", context) == true));
        check((evaluator.evaluateAsBool("$env == 'prod' || $env == 'dev'", context) == false));
    }

    if (section_index == 6) {
        context.setValue("a", "1");
        context.setValue("b", "2");
        context.setValue("c", "3");
        check((evaluator.evaluateAsBool("($a == '1' || $b == '1') && $c == '3'", context) == true));
        check((evaluator.evaluateAsBool("$a == '1' || ($b == '2' && $c == '3')", context) == true));
        check((evaluator.evaluateAsBool("$a == '2' || ($b == '2' && $c == '4')", context) == false));
    }

    if (section_index == 7) {
        context.setValue("my_var", "some_value");
        context.setValue("empty_var", "");
        check((evaluator.evaluateAsBool("$my_var", context) == true));
        check((evaluator.evaluateAsBool("$empty_var", context) == false));
        check((evaluator.evaluateAsBool("$non_existent_var", context) == false));
    }

    if (section_index == 8) {
        context.setValue("count", "5");
        check((evaluator.evaluateAsBool("$count < 10", context) == true));
        check((evaluator.evaluateAsBool("$count < 5", context) == false));
        check((evaluator.evaluateAsBool("$count < 3", context) == false));
    }

    if (section_index == 9) {
        context.setValue("count", "5");
        check((evaluator.evaluateAsBool("$count > 3", context) == true));
        check((evaluator.evaluateAsBool("$count > 5", context) == false));
        check((evaluator.evaluateAsBool("$count > 10", context) == false));
    }

    if (section_index == 10) {
        context.setValue("count", "5");
        check((evaluator.evaluateAsBool("$count <= 10", context) == true));
        check((evaluator.evaluateAsBool("$count <= 5", context) == true));
        check((evaluator.evaluateAsBool("$count <= 3", context) == false));
    }

    if (section_index == 11) {
        context.setValue("count", "5");
        check((evaluator.evaluateAsBool("$count >= 3", context) == true));
        check((evaluator.evaluateAsBool("$count >= 5", context) == true));
        check((evaluator.evaluateAsBool("$count >= 10", context) == false));
    }

    if (section_index == 12) {
        check((evaluator.evaluateAsBool("!true", context) == false));
        check((evaluator.evaluateAsBool("!false", context) == true));
        context.setValue("flag", "yes");
        check((evaluator.evaluateAsBool("!$flag", context) == false));
        context.setValue("empty", "");
        check((evaluator.evaluateAsBool("!$empty", context) == true));
    }

    if (section_index == 13) {
        context.setValue("age", "25");
        check((evaluator.evaluateAsBool("$age >= 18 && $age < 65", context) == true));
        check((evaluator.evaluateAsBool("$age < 18 || $age >= 65", context) == false));
        check((evaluator.evaluateAsBool("!($age < 18)", context) == true));
    }

    if (section_index == 14) {
        check((evaluator.evaluateAsBool("'hello world'.includes('world')", context) == true));
        check((evaluator.evaluateAsBool("'hello world'.startsWith('hello')", context) == true));
        check((evaluator.evaluateAsBool("'hello world'.endsWith('world')", context) == true));
        check((evaluator.evaluateAsBool("/.*@.*\\.com/.test('admin@test.com')", context) == true));
    }

    if (section_index == 15) {
        // TurboScript returns false on error, doesn't throw
        check((evaluator.evaluateAsBool("$env === 'prod'", context) == false));
        check((evaluator.evaluateAsBool("$env == 'prod", context) == false));
    }

    if (section_index == 16) {
        context.setValue("my_var", "value");
        check((evaluator.evaluateAsBool("$my_var == 'value'", context) == true));
        check((evaluator.evaluateAsBool("  true  &&  false  ", context) == false));
    }

    if (section_index == 17) {
        context.setValue("env.ENVIRONMENT", "production");
        check((evaluator.evaluateAsBool("$env.ENVIRONMENT == 'production'", context) == true));
    }
}

spec("ExpressionEvaluator Tests") {
    it("Simple Equality") { test_expressionevaluator_tests_1(0); }
    it("Not Equal") { test_expressionevaluator_tests_1(1); }
    it("Boolean Literals") { test_expressionevaluator_tests_1(2); }
    it("Number Literals") { test_expressionevaluator_tests_1(3); }
    it("Logical AND") { test_expressionevaluator_tests_1(4); }
    it("Logical OR") { test_expressionevaluator_tests_1(5); }
    it("Parentheses for Precedence") { test_expressionevaluator_tests_1(6); }
    it("Truthy evaluation of single variable") { test_expressionevaluator_tests_1(7); }
    it("Less Than") { test_expressionevaluator_tests_1(8); }
    it("Greater Than") { test_expressionevaluator_tests_1(9); }
    it("Less Than or Equal") { test_expressionevaluator_tests_1(10); }
    it("Greater Than or Equal") { test_expressionevaluator_tests_1(11); }
    it("Logical NOT") { test_expressionevaluator_tests_1(12); }
    it("Combined Comparison and Logical") { test_expressionevaluator_tests_1(13); }
    it("JavaScript String Methods") { test_expressionevaluator_tests_1(14); }
    it("Invalid Expression") { test_expressionevaluator_tests_1(15); }
    it("Whitespace Handling") { test_expressionevaluator_tests_1(16); }
    it("Dotted Path Variables") { test_expressionevaluator_tests_1(17); }
}
