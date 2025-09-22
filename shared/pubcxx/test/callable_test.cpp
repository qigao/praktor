#include "pubcxx/callable.hpp"

#include <catch2/catch_all.hpp>
#include <concepts>
#include <functional>
#include <string>

namespace Test {
    // Unconstrained version
    template <typename Fn, typename... Args>
    decltype(auto) TestCallable(Fn&& fun, Args&&... args) {
        return std::invoke(std::forward<Fn>(fun), std::forward<Args>(args)...);
    }

    // Constrained version
    template <typename Fn, typename... Args>
        requires std::invocable<Fn, Args...>
    decltype(auto) TestCallableWithConstraint(Fn&& fun, Args&&... args) {
        return std::invoke(std::forward<Fn>(fun), std::forward<Args>(args)...);
    }
}   // namespace Test

// Helper structs and functions
struct Functor {
    int operator()(int x, int y) const { return x + y; }
};

struct TestClass {
    int value = 10;

    int add(int x) const { return value + x; }

    std::string& append(std::string& s) {
        s += " appended";
        return s;
    }
};

int freeFunction(int x, int y) { return x * y; }

TEST_CASE("TestCallable function template tests", "[TestCallable]") {
    SECTION("Free function") {
        REQUIRE(Test::TestCallable(freeFunction, 3, 4) == 12);
        REQUIRE(Test::TestCallableWithConstraint(freeFunction, 3, 4) == 12);
    }

    SECTION("Lambda without capture") {
        auto lambda = [](int x, int y) { return x + y; };
        REQUIRE(Test::TestCallable(lambda, 5, 6) == 11);
        REQUIRE(Test::TestCallableWithConstraint(lambda, 5, 6) == 11);
    }

    SECTION("Lambda with capture") {
        int offset = 10;
        auto lambda = [offset](int x) { return x + offset; };
        REQUIRE(Test::TestCallable(lambda, 5) == 15);
        REQUIRE(Test::TestCallableWithConstraint(lambda, 5) == 15);
    }

    SECTION("Functor") {
        Functor functor;
        REQUIRE(Test::TestCallable(functor, 7, 8) == 15);
        REQUIRE(Test::TestCallableWithConstraint(functor, 7, 8) == 15);
    }

    SECTION("Const member function") {
        TestClass obj;
        REQUIRE(Test::TestCallable(&TestClass::add, obj, 5) == 15);
        REQUIRE(Test::TestCallableWithConstraint(&TestClass::add, obj, 5) == 15);
    }

    SECTION("Member function with reference return") {
        TestClass obj;
        std::string str = "test";
        REQUIRE(&Test::TestCallable(&TestClass::append, obj, str) == &str);
        REQUIRE(str == "test appended");
        str = "test";
        REQUIRE(&Test::TestCallableWithConstraint(&TestClass::append, obj, str) == &str);
        REQUIRE(str == "test appended");
    }

    SECTION("Reference return from lambda") {
        int x = 42;
        auto lambda = [&x]() -> int& { return x; };
        Test::TestCallable(lambda) = 100;
        REQUIRE(x == 100);
        x = 42;
        Test::TestCallableWithConstraint(lambda) = 200;
        REQUIRE(x == 200);
    }

    SECTION("No arguments") {
        auto lambda = []() { return 42; };
        REQUIRE(Test::TestCallable(lambda) == 42);
        REQUIRE(Test::TestCallableWithConstraint(lambda) == 42);
    }
}

TEST_CASE("TestCallable invalid cases", "[TestCallable][Invalid]") {
    SECTION("Non-callable type") {
        int non_callable = 42;
        // Test::TestCallable(non_callable, 1); // Should fail
        // Test::TestCallableWithConstraint(non_callable, 1); // Should fail
    }

    SECTION("Mismatched arguments") {
        auto lambda = [](int x, int y) { return x + y; };
        // Test::TestCallable(lambda, 1); // Should fail
        // Test::TestCallableWithConstraint(lambda, 1); // Should fail
    }
}

TEST_CASE("TestCallableWithConstraint ensures invocable", "[TestCallable][Concepts]") {
    static_assert(std::invocable<decltype(freeFunction), int, int>, "Free function should be invocable");
    static_assert(std::invocable<Functor, int, int>, "Functor should be invocable");
    static_assert(std::invocable<decltype(&TestClass::add), TestClass, int>, "Member function should be invocable");
    static_assert(!std::invocable<decltype(freeFunction), int>,
                  "Free function with wrong args should not be invocable");
    static_assert(!std::invocable<int, int>, "Non-callable type should not be invocable");
}