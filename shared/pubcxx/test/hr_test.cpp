#include "pubcxx/readable.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

constexpr size_t IntegerRepresentableBoundary() { return size_t{2} << (std::numeric_limits<double>::digits - 1); }

TEST_CASE("HumanReadable 1K convert Test", "[HumanReadableTest]") {
    HumanReadable hr;
    hr.size = 1024;

    std::ostringstream oss;
    oss << hr;

    REQUIRE_THAT(oss.str(), Catch::Matchers::Equals("1KB (1024)"));
}

TEST_CASE("HumanReadable Cast Test", "[HumanReadableTest]") {
    CHECK(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() - 2})) ==
          size_t{IntegerRepresentableBoundary() - 2});
    CHECK(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() - 1})) ==
          size_t{IntegerRepresentableBoundary() - 1});
    CHECK(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary()})) ==
          size_t{IntegerRepresentableBoundary()});
    CHECK_THROWS(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 1}));
    CHECK(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 2})) ==
          size_t{IntegerRepresentableBoundary() + 2});
    CHECK_THROWS(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 3}));
    CHECK(static_cast<size_t>(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 4})) ==
          size_t{IntegerRepresentableBoundary() + 4});
    CHECK_THROWS(NarrowCast<double>(size_t{IntegerRepresentableBoundary() + 5}));
}
