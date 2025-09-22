#include "pubcxx/str_algo.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("join_range tests", "[join_range]") {
    SECTION("Joining two strings with comma") {
        std::vector<std::string> strings = {"hello", "world"};
        std::string result = join_range(strings, ",");
        REQUIRE(result == "hello,world");
    }

    SECTION("Joining three strings with hyphen") {
        std::vector<std::string> strings = {"a", "b", "c"};
        std::string result = join_range(strings, "-");
        REQUIRE(result == "a-b-c");
    }

    SECTION("Joining one string with comma") {
        std::vector<std::string> strings = {"one"};
        std::string result = join_range(strings, ",");
        REQUIRE(result == "one");
    }

    SECTION("Joining empty vector with comma") {
        std::vector<std::string> strings = {};
        std::string result = join_range(strings, ",");
        REQUIRE(result == "");
    }

    SECTION("Joining three strings with space") {
        std::vector<std::string> strings = {"first", "second", "third"};
        std::string result = join_range(strings, " ");
        REQUIRE(result == "first second third");
    }

    SECTION("Joining strings with empty delimiter") {
        std::vector<std::string> strings = {"a", "b", "c"};
        std::string result = join_range(strings, "");
        REQUIRE(result == "abc");
    }

    SECTION("Joining strings with multi-character delimiter") {
        std::vector<std::string> strings = {"one", "two", "three"};
        std::string result = join_range(strings, " and ");
        REQUIRE(result == "one and two and three");
    }

    SECTION("Joining strings with special characters in delimiter") {
        std::vector<std::string> strings = {"apple", "banana", "cherry"};
        std::string result = join_range(strings, " | ");
        REQUIRE(result == "apple | banana | cherry");
    }
}
