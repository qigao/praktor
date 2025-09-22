#include "pubcxx/simple_xxhash.hpp"

#include "catch2/catch_all.hpp"

#include <string>
#include <vector>

TEST_CASE("XXHasher::xxh3_64_mem", "[xxhasher]") {
    std::vector<char> data = {'a', 'b', 'c', 'd'};
    size_t len = data.size();
    uint64_t hash = XXHash::xxh3_64_sum_buf(data.data(), len);
    REQUIRE(hash != 0);
}

TEST_CASE("XXHasher::xxh3_64_mem_str", "[xxhasher]") {
    std::vector<char> data = {'a', 'b', 'c', 'd'};
    size_t len = data.size();
    std::string hash_str = XXHash::xxh3_64_sum_buf_str(data.data(), len);
    REQUIRE(hash_str.length() == 16);
}

TEST_CASE("XXHasher::xxh3_128_mem", "[xxhasher]") {
    std::vector<char> data = {'a', 'b', 'c', 'd'};
    size_t len = data.size();
    XXH128_hash_t hash = XXHash::xxh3_128_sum_buf(data.data(), len);
    REQUIRE(hash.high64 != 0);
    REQUIRE(hash.low64 != 0);
}

TEST_CASE("XXHasher::xxh3_128_mem_str", "[xxhasher]") {
    std::vector<char> data = {'a', 'b', 'c', 'd'};
    size_t const len = data.size();
    auto const hash_str = XXHash::xxh3_128_sum_str(data.data(), len);
    REQUIRE(hash_str.length() == 32);
}
