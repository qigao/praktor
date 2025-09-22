
#include "pubcxx/snow_flake.hpp"   // Adjust path as necessary

#include "catch2/catch_test_macros.hpp"

#include <limits>   // Required for std::numeric_limits
#include <set>
#include <thread>
#include <vector>

// Helper function to extract parts from a snowflake ID
// This is essentially the reverse of the ID generation logic
struct SnowflakeParts {
    int64_t timestamp;
    int64_t datacenter_id;
    int64_t worker_id;
    int64_t sequence;
};

SnowflakeParts extract_parts(int64_t id) {
    SnowflakeParts parts;
    parts.sequence = id & ((1 << 12L) - 1);                        // SEQUENCE_MASK
    parts.worker_id = (id >> 12L) & ((1 << 5L) - 1);               // WORKER_ID_SHIFT, MAX_WORKER_ID
    parts.datacenter_id = (id >> (12L + 5L)) & ((1 << 5L) - 1);    // DATACENTER_ID_SHIFT, MAX_DATACENTER_ID
    parts.timestamp = (id >> (12L + 5L + 5L)) + 1735689600000LL;   // TIMESTAMP_LEFT_SHIFT, TWEPOCH
    return parts;
}

TEST_CASE("Snowflake ID Generation", "[snowflake]") {
    snowflake generator;

    SECTION("Initialization") {
        REQUIRE_NOTHROW(generator.init(0, 0));
        REQUIRE_NOTHROW(generator.init(31, 31));   // MAX_WORKER_ID, MAX_DATACENTER_ID

        SECTION("Invalid worker ID") {
            REQUIRE_THROWS_AS(generator.init(-1, 0), std::runtime_error);
            REQUIRE_THROWS_AS(generator.init(32, 0), std::runtime_error);   // MAX_WORKER_ID + 1
        }

        SECTION("Invalid datacenter ID") {
            REQUIRE_THROWS_AS(generator.init(0, -1), std::runtime_error);
            REQUIRE_THROWS_AS(generator.init(0, 32), std::runtime_error);   // MAX_DATACENTER_ID + 1
        }
    }

    SECTION("Generate unique IDs") {
        generator.init(1, 1);
        std::set<int64_t> ids;
        int const num_ids = 1000;
        for (int i = 0; i < num_ids; ++i) { ids.insert(generator.nextid()); }
        REQUIRE(ids.size() == num_ids);
    }

    SECTION("IDs are generally increasing") {
        generator.init(2, 2);
        int64_t last_id = 0;   // Or std::numeric_limits<int64_t>::min();
        for (int i = 0; i < 100; ++i) {
            int64_t current_id = generator.nextid();
            if (i > 0) {   // Skip first comparison as last_id is 0
                REQUIRE(current_id > last_id);
            }
            last_id = current_id;
        }
    }

    SECTION("Sequence rollover") {
        generator.init(3, 3);
        // Generate enough IDs to roll over the sequence
        // SEQUENCE_MASK is (1 << 12) - 1 = 4095. So 4096 IDs will cause a rollover.
        int64_t first_id_in_millisecond = generator.nextid();
        SnowflakeParts parts_first = extract_parts(first_id_in_millisecond);

        int64_t id_before_rollover = 0;
        int64_t id_after_rollover = 0;

        // Assuming the test runs fast enough to stay in the same millisecond for 4096 generations
        // This might be flaky on some systems or under heavy load.
        // A more robust test would involve controlling the time source, which is complex.
        for (int i = 0; i < 4096 + 5; ++i) {
            int64_t current_id = generator.nextid();
            SnowflakeParts current_parts = extract_parts(current_id);
            if (current_parts.timestamp == parts_first.timestamp &&
                current_parts.sequence == ((1 << 12L) - 1)) {   // SEQUENCE_MASK
                id_before_rollover = current_id;
            }
            if (id_before_rollover != 0 && current_parts.timestamp > parts_first.timestamp &&
                current_parts.sequence == 0) {
                id_after_rollover = current_id;
                break;
            }
            // If we've generated a lot of IDs and haven't rolled over, the millisecond might have changed too soon
            if (i == 4095 && id_before_rollover == 0) {
                WARN("Sequence rollover test might be flaky: Millisecond changed before sequence maxed out.");
            }
        }
        REQUIRE(id_before_rollover != 0);
        REQUIRE(id_after_rollover != 0);

        SnowflakeParts parts_before = extract_parts(id_before_rollover);
        SnowflakeParts parts_after = extract_parts(id_after_rollover);

        REQUIRE(parts_before.sequence == ((1 << 12L) - 1));   // SEQUENCE_MASK
        REQUIRE(parts_after.sequence == 0);
        REQUIRE(parts_after.timestamp > parts_before.timestamp);
        REQUIRE(parts_after.timestamp == parts_before.timestamp + 1);   // Should be next millisecond
    }

    SECTION("ID components are correct") {
        int64_t worker_id = 5;
        int64_t datacenter_id = 10;
        generator.init(worker_id, datacenter_id);

        int64_t id = generator.nextid();
        SnowflakeParts parts = extract_parts(id);

        REQUIRE(parts.worker_id == worker_id);
        REQUIRE(parts.datacenter_id == datacenter_id);
        // Timestamp is harder to verify exactly without knowing the internal start time,
        // but we can check it's within a reasonable range of current time.
        int64_t current_time_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        REQUIRE(parts.timestamp >= 1735689600000LL);          // TWEPOCH
        REQUIRE(parts.timestamp <= current_time_ms + 1000);   // Allow some slack
    }

    SECTION("Thread safety") {
        generator.init(7, 7);
        std::vector<std::thread> threads;
        std::set<int64_t> all_ids;
        std::mutex set_mutex;
        int const ids_per_thread = 500;
        int const num_threads = 10;

        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([&]() {
                std::vector<int64_t> local_ids;
                local_ids.reserve(ids_per_thread);
                for (int j = 0; j < ids_per_thread; ++j) { local_ids.push_back(generator.nextid()); }
                std::lock_guard<std::mutex> lock(set_mutex);
                all_ids.insert(local_ids.begin(), local_ids.end());
            });
        }

        for (auto& t : threads) { t.join(); }
        REQUIRE(all_ids.size() == ids_per_thread * num_threads);
    }
}

TEST_CASE("timestampToIso8601 Conversion", "[timestampToIso8601]") {
    // TWEPOCH = 1735689600000LL (January 1, 2025, 00:00:00 UTC)
    // TIMESTAMP_LEFT_SHIFT = 22

    SECTION("Convert absolute timestamp") {
        // Example: January 1, 2025, 00:00:00.000 UTC
        uint64_t ts1_ms = 1735689600000ULL;
        REQUIRE(timestampToIso8601(ts1_ms, false) == "2025-01-01T00:00:00.000Z");

        // Example: January 1, 2025, 00:00:01.123 UTC
        uint64_t ts2_ms = 1735689601123ULL;
        REQUIRE(timestampToIso8601(ts2_ms, false) == "2025-01-01T00:00:01.123Z");

        // Example: A known date: 2023-10-26T10:30:45.500Z
        // (EpochConverter.com: 1698316245500)
        uint64_t ts3_ms = 1698316245500ULL;
        REQUIRE(timestampToIso8601(ts3_ms, false) == "2023-10-26T10:30:45.500Z");
    }

    SECTION("Convert raw Snowflake ID") {
        // Snowflake ID structure:
        // (timestamp_delta << 22) | (datacenter_id << 17) | (worker_id << 12) | sequence
        // TWEPOCH = 1735689600000LL

        // ID generated at TWEPOCH + 0ms, dc=1, w=1, seq=1
        // timestamp_delta = 0
        uint64_t id1_ts_delta = 0;
        uint64_t id1_dc = 1;
        uint64_t id1_w = 1;
        uint64_t id1_seq = 1;
        uint64_t id1 = (id1_ts_delta << 22) | (id1_dc << 17) | (id1_w << 12) | id1_seq;
        // Expected absolute timestamp: TWEPOCH + 0 = 1735689600000
        REQUIRE(timestampToIso8601(id1, true) == "2025-01-01T00:00:00.000Z");

        // ID generated at TWEPOCH + 1234ms, dc=5 (0b101), w=10 (0b01010), seq=100
        // timestamp_delta = 1234
        uint64_t id2_ts_delta = 1234;   // milliseconds since TWEPOCH
        uint64_t id2_dc = 5;
        uint64_t id2_w = 10;
        uint64_t id2_seq = 100;
        uint64_t id2 = (id2_ts_delta << 22) | (id2_dc << 17) | (id2_w << 12) | id2_seq;
        // Expected absolute timestamp: TWEPOCH + 1234 = 1735689600000 + 1234 = 1735689601234
        REQUIRE(timestampToIso8601(id2, true) == "2025-01-01T00:00:01.234Z");

        // A more complex ID
        // Let's say current time is 2025-01-01T01:02:03.456Z
        // Absolute ms: 1735689600000 (TWEPOCH) + 3600000 (1hr) + 120000 (2min) + 3000 (3s) + 456 (ms)
        // = 1735689600000 + 3723456 = 1735693323456
        // Timestamp delta = 3723456
        uint64_t id3_ts_delta = 3723456;
        uint64_t id3_dc = 15;
        uint64_t id3_w = 31;
        uint64_t id3_seq = 1024;
        uint64_t id3 = (id3_ts_delta << 22) | (id3_dc << 17) | (id3_w << 12) | id3_seq;
        REQUIRE(timestampToIso8601(id3, true) == "2025-01-01T01:02:03.456Z");
    }

    SECTION("Invalid inputs for timestampToIso8601") {
        // Test case for timestamp 0 (Unix epoch) when not a snowflake ID
        REQUIRE(timestampToIso8601(0, false) == "1970-01-01T00:00:00.000Z");

        // Test case for snowflake ID 0 (all parts zero, so timestamp delta is 0 relative to TWEPOCH)
        REQUIRE(timestampToIso8601(0, true) == "2025-01-01T00:00:00.000Z");

        // Snowflake ID with timestamp part being 0 (after shift)
        // This means (value_or_id >> SNOWFLAKE_TIMESTAMP_SHIFT) == 0
        // e.g., an ID where only datacenter, worker, sequence are set
        // This implies the timestamp is TWEPOCH.
        uint64_t id_at_epoch_custom_parts = (1ULL << 17) | (1ULL << 12) | 1ULL;   // dc=1, w=1, seq=1, ts_delta=0
        // This ID represents TWEPOCH + 0ms, with specific dc, worker, seq
        REQUIRE(timestampToIso8601(id_at_epoch_custom_parts, true) == "2025-01-01T00:00:00.000Z");

        // A valid snowflake ID (ts_delta > 0) should produce the correct time
        uint64_t valid_sf_id_delta_1 = (1ULL << 22);   // ts_delta = 1, rest 0 (dc=0, w=0, seq=0)
                                                       // This is TWEPOCH + 1ms
        REQUIRE(timestampToIso8601(valid_sf_id_delta_1, true) == "2025-01-01T00:00:00.001Z");
    }

    SECTION("Edge case milliseconds") {
        // .000Z
        uint64_t ts_000 = 1735689600000ULL;
        REQUIRE(timestampToIso8601(ts_000, false) == "2025-01-01T00:00:00.000Z");
        // .999Z
        uint64_t ts_999 = 1735689600999ULL;
        REQUIRE(timestampToIso8601(ts_999, false) == "2025-01-01T00:00:00.999Z");
    }
}
