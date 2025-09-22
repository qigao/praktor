
#include "pubcxx/tiny_expr.hpp"

#include "catch2/catch_all.hpp"

#define _USE_MATH_DEFINES   // For M_PI and M_E on Windows
#include <cmath>
#include <stdexcept>
#include <tao/pegtl.hpp>   // For tao::pegtl::parse_error

using Catch::Approx;

TEST_CASE("Basic Arithmetic", "[calculator]") {
    REQUIRE(calculator::calculate("1 + 1") == 2);
    REQUIRE(calculator::calculate("10 - 5") == 5);
    REQUIRE(calculator::calculate("4 * 5") == 20);
    REQUIRE(calculator::calculate("100 / 20") == 5);
    REQUIRE(calculator::calculate("10 % 3") == 1);
}

TEST_CASE("Operator Precedence", "[calculator]") {
    REQUIRE(calculator::calculate("2 + 3 * 4") == 14);
    REQUIRE(calculator::calculate("2 * 3 + 4") == 10);
    REQUIRE(calculator::calculate("10 - 2 * 3") == 4);
    REQUIRE(calculator::calculate("100 / 10 * 2") == 20);   // Left-associative
}

TEST_CASE("Parentheses (Brackets)", "[calculator]") {
    REQUIRE(calculator::calculate("(2 + 3) * 4") == 20);
    REQUIRE(calculator::calculate("100 / (10 * 2)") == 5);
    REQUIRE(calculator::calculate("((2 + 3) * (1 + 1)) * 5") == 50);
    REQUIRE(calculator::calculate("10 - (5 - 2)") == 7);
}

TEST_CASE("Unary Sign and Whitespace", "[calculator]") {
    REQUIRE(calculator::calculate("-5") == -5);
    REQUIRE(calculator::calculate("+5") == 5);
    REQUIRE(calculator::calculate("-5 + 10") == 5);
    REQUIRE(calculator::calculate(" 10 *  -2") == -20);
    REQUIRE(calculator::calculate("\t-5\n*\t-5\n") == 25);
}

TEST_CASE("Bitwise and Logical Operators", "[calculator]") {
    REQUIRE(calculator::calculate("5 & 3") == 1);   // 101 & 011 = 001
    REQUIRE(calculator::calculate("5 | 3") == 7);   // 101 | 011 = 111
    REQUIRE(calculator::calculate("5 ^ 3") == 6);   // 101 ^ 011 = 110
    REQUIRE(calculator::calculate("4 << 2") == 16);
    REQUIRE(calculator::calculate("16 >> 2") == 4);
    REQUIRE(calculator::calculate("1 && 1") == 1);
    REQUIRE(calculator::calculate("1 && 0") == 0);
    REQUIRE(calculator::calculate("0 || 0") == 0);
    REQUIRE(calculator::calculate("0 || 1") == 1);
    REQUIRE(calculator::calculate("1 && 5") == 1);   // Non-zero values are true
    REQUIRE(calculator::calculate("0 || -5") == 1);
}

TEST_CASE("Comparison Operators", "[calculator]") {
    REQUIRE(calculator::calculate("10 > 5") == 1);
    REQUIRE(calculator::calculate("10 < 5") == 0);
    REQUIRE(calculator::calculate("10 == 10") == 1);
    REQUIRE(calculator::calculate("10 != 10") == 0);
    REQUIRE(calculator::calculate("5 >= 5") == 1);
    REQUIRE(calculator::calculate("5 <= 4") == 0);
}

TEST_CASE("Comments", "[calculator]") {
    REQUIRE(calculator::calculate("10 + 5 # This is a comment") == 15);
    REQUIRE(calculator::calculate("#-5\n 2 * 2") == 4);
    REQUIRE(calculator::calculate("10 + /* not a multiline comment */ 5") == 15);   // '*' is just multiplication
}

TEST_CASE("Complex Expressions", "[calculator]") {
    REQUIRE(calculator::calculate("100 / ( (1 + 1) * 5) - (3+4) * -1") == 17);
    REQUIRE(calculator::calculate("-1 * (2 - (3 * (4 + 5)))") == 25);
}

TEST_CASE("Invalid Expressions", "[calculator]") {
    // pegtl::parse throws tao::pegtl::parse_error on failure
    REQUIRE_THROWS(calculator::calculate("1 +"));
    REQUIRE_THROWS(calculator::calculate("1 * * 2"));
    REQUIRE_THROWS(calculator::calculate("(5 + 2"));
    REQUIRE_THROWS(calculator::calculate("5 + 2)"));
    REQUIRE_THROWS(calculator::calculate("five plus two"));
    REQUIRE_THROWS(calculator::calculate(""));
}

TEST_CASE("Mathematical Functions", "[calculator]") {
    REQUIRE(calculator::calculate("sin(0)") == Approx(0.0));
    REQUIRE(calculator::calculate("cos(0)") == Approx(1.0));
    REQUIRE(calculator::calculate("tan(0)") == Approx(0.0));
    REQUIRE(calculator::calculate("log(1)") == Approx(0.0));
    REQUIRE(calculator::calculate("exp(0)") == Approx(1.0));

    // Test with constants
    REQUIRE(calculator::calculate("PI") == Approx(M_PI));
    REQUIRE(calculator::calculate("E") == Approx(M_E));

    // Test with floating-point results
    REQUIRE(calculator::calculate("log(2)") == Approx(std::log(2)));
    REQUIRE(calculator::calculate("log(7)") == Approx(std::log(7)));
    REQUIRE(calculator::calculate("exp(1)") == Approx(std::exp(1)));
    REQUIRE(calculator::calculate("sin(1)") == Approx(std::sin(1.0)));   // Added simple sin test
    REQUIRE(calculator::calculate("sin(PI / 2)") == Approx(std::sin(M_PI / 2.0)));
    REQUIRE(calculator::calculate("cos(PI)") == Approx(std::cos(M_PI)));
    REQUIRE(calculator::calculate("tan(PI / 4)") == Approx(std::tan(M_PI / 4.0)));
    REQUIRE(calculator::calculate("log(exp(5))") == Approx(5.0));
    REQUIRE(calculator::calculate("exp(log(5))") == Approx(5.0));
    REQUIRE(calculator::calculate("sin(cos(0))") == Approx(std::sin(std::cos(0))));
    REQUIRE(calculator::calculate("log(1 + exp(0))") == Approx(std::log(1 + std::exp(0))));

    // Test sqr and sqrt
    REQUIRE(calculator::calculate("sqr(2)") == Approx(4.0));
    REQUIRE(calculator::calculate("sqrt(9)") == Approx(3.0));
    REQUIRE(calculator::calculate("sqrt(0)") == Approx(0.0));

    // Test error cases for log and sqrt
    REQUIRE_THROWS_AS(calculator::calculate("log(0)"), std::runtime_error);
    REQUIRE_THROWS_AS(calculator::calculate("log(-1)"), std::runtime_error);
    REQUIRE_THROWS_AS(calculator::calculate("sqrt(-1)"), std::runtime_error);

    // Combined expressions
    REQUIRE(calculator::calculate("sin(0) + cos(0)") == Approx(1.0));
    REQUIRE(calculator::calculate("sin(0.5)") == Approx(std::sin(0.5)));
    REQUIRE(calculator::calculate("exp(log(10))") == Approx(10.0));   // Now expects precise result

    // Test csc, sec, cot
    REQUIRE(calculator::calculate("csc(PI / 2)") == Approx(1.0 / std::sin(M_PI / 2.0)));
    REQUIRE(calculator::calculate("sec(0)") == Approx(1.0 / std::cos(0.0)));
    REQUIRE(calculator::calculate("cot(PI / 4)") == Approx(1.0 / std::tan(M_PI / 4.0)));

    // Test error cases for csc, sec, cot
    REQUIRE_THROWS_AS(calculator::calculate("csc(0)"), std::runtime_error);
    REQUIRE_THROWS_AS(calculator::calculate("sec(PI / 2)"), std::runtime_error);
    REQUIRE_THROWS_AS(calculator::calculate("cot(0)"), std::runtime_error);
}

TEST_CASE("Advanced Combined Expressions", "[calculator]") {
    REQUIRE(calculator::calculate("2 * sin(PI / 6) + 3 * cos(PI / 3)") ==
            Approx(2 * std::sin(M_PI / 6.0) + 3 * std::cos(M_PI / 3.0)));
    REQUIRE(calculator::calculate("log(10) / exp(1) + 5") == Approx(std::log(10) / std::exp(1) + 5));
    REQUIRE(calculator::calculate("(sin(0.5) + cos(0.5)) * log(2)") ==
            Approx((std::sin(0.5) + std::cos(0.5)) * std::log(2)));
    REQUIRE(calculator::calculate("10 + log(exp(5) * 2) - sin(PI)") ==
            Approx(10 + std::log(std::exp(5) * 2) - std::sin(M_PI)));
    REQUIRE(calculator::calculate("csc(PI / 6) + sec(PI / 3)") ==
            Approx(1.0 / std::sin(M_PI / 6.0) + 1.0 / std::cos(M_PI / 3.0)));
}
