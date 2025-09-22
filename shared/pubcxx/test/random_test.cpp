#include "pubcxx/random.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("RandomTest generate string of len(10)", "[random_string]") {
    std::string str = random_string(10);
    REQUIRE(str.size() == 10);
}

TEST_CASE("RandomTest generate string of len(0)", "[random_string]") {
    std::string str = random_string(0);
    REQUIRE(str.empty());
}
