#pragma once
#include "types.hpp"

#include <chrono>
#include <cstdint>
#include <fmt/format.h>
#include <mutex>
#include <stdexcept>
#include <string>

class snowflake {
    using lock_type = std::mutex;
    static constexpr int64_t EPOCH = 1735689600000LL;
    static constexpr int64_t WORKER_ID_BITS = 5L;
    static constexpr int64_t DATACENTER_ID_BITS = 5L;
    static constexpr int64_t MAX_WORKER_ID = (1 << WORKER_ID_BITS) - 1;
    static constexpr int64_t MAX_DATACENTER_ID = (1 << DATACENTER_ID_BITS) - 1;
    static constexpr int64_t SEQUENCE_BITS = 12L;
    static constexpr int64_t WORKER_ID_SHIFT = SEQUENCE_BITS;
    static constexpr int64_t DATACENTER_ID_SHIFT = SEQUENCE_BITS + WORKER_ID_BITS;
    static constexpr int64_t TIMESTAMP_LEFT_SHIFT = SEQUENCE_BITS + WORKER_ID_BITS + DATACENTER_ID_BITS;
    static constexpr int64_t SEQUENCE_MASK = (1 << SEQUENCE_BITS) - 1;

    using time_point = std::chrono::time_point<std::chrono::steady_clock>;

    time_point start_time_point_ = std::chrono::steady_clock::now();
    int64_t start_millisecond_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    int64_t last_timestamp_ = -1;
    int64_t worker_id_ = 0;
    int64_t datacenter_id_ = 0;
    int64_t sequence_ = 0;
    lock_type lock_;

public:
    snowflake() = default;

    snowflake(snowflake const&) = delete;

    snowflake& operator=(snowflake const&) = delete;

    void init(int64_t const worker_id, int64_t const datacenter_id) {
        if (worker_id > MAX_WORKER_ID || worker_id < 0) {
            throw std::runtime_error("worker Id can't be greater than 31 or less than 0");
        }

        if (datacenter_id > MAX_DATACENTER_ID || datacenter_id < 0) {
            throw std::runtime_error("datacenter Id can't be greater than 31 or less than 0");
        }

        worker_id_ = worker_id;
        datacenter_id_ = datacenter_id;
    }

    int64_t nextid() {
        std::lock_guard<lock_type> lock(lock_);
        // std::chrono::steady_clock  cannot decrease as physical time moves forward
        auto timestamp = millisecond();
        if (last_timestamp_ == timestamp) {
            sequence_ = (sequence_ + 1) & SEQUENCE_MASK;
            if (sequence_ == 0) { timestamp = wait_next_millis(last_timestamp_); }
        } else {
            sequence_ = 0;
        }

        last_timestamp_ = timestamp;

        return ((timestamp - EPOCH) << TIMESTAMP_LEFT_SHIFT) | (datacenter_id_ << DATACENTER_ID_SHIFT) |
               (worker_id_ << WORKER_ID_SHIFT) | sequence_;
    }

private:
    int64_t millisecond() const noexcept {
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time_point_);
        return start_millisecond_ + diff.count();
    }

    int64_t wait_next_millis(int64_t last) const noexcept {
        auto timestamp = millisecond();
        while (timestamp <= last) { timestamp = millisecond(); }
        return timestamp;
    }
};

/**
 * @brief Converts a given uint64_t value to an ISO 8601 formatted string.
 *
 * Note: This function is not thread-safe because it uses std::gmtime, which relies on a static buffer.
 * If thread-safety is required, consider using a thread-safe alternative or synchronizing access.
 *
 * The function can interpret the input `value_or_id` in two ways based on the
 * `is_value_a_raw_snowflake_id` flag:
 * 1. If `is_value_a_raw_snowflake_id` is true, `value_or_id` is treated as a raw Snowflake ID.
 *    The function will extract the timestamp component from this ID, convert it to
 *    an absolute Unix epoch timestamp (milliseconds), and then format it.
 * 2. If `is_value_a_raw_snowflake_id` is false (default), `value_or_id` is assumed to be
 *    an absolute Unix epoch timestamp (milliseconds) already.
 *
 * The Snowflake epoch (TWEPOCH from snow_flake.hpp) is January 1, 2025, 00:00:00 UTC.
 * The output format is "YYYY-MM-DDTHH:MM:SS.sssZ".
 *
 * @param value_or_id The uint64_t value to convert. Can be a raw Snowflake ID or an absolute timestamp.
 * @param is_value_a_raw_snowflake_id Flag indicating how to interpret `value_or_id`.
 * @return std::string The ISO 8601 formatted date-time string.
 */
inline std::string timestampToIso8601(uint64_t value_or_id, bool is_value_a_raw_snowflake_id = false) {
    // Pre-allocate string buffer
    std::string result;
    result.reserve(24);   // "YYYY-MM-DDTHH:MM:SS.sssZ"

    // SNOWFLAKE_EPOCH_MS corresponds to EPOCH: January 1, 2025, 00:00:00 UTC in milliseconds
    static constexpr int64_t SNOWFLAKE_EPOCH_MS = 1735689600000LL;
    // SNOWFLAKE_TIMESTAMP_SHIFT corresponds to TIMESTAMP_LEFT_SHIFT in snow_flake.hpp
    // (SEQUENCE_BITS + WORKER_ID_BITS + DATACENTER_ID_BITS = 12 + 5 + 5 = 22)
    static constexpr int SNOWFLAKE_TIMESTAMP_SHIFT = 22;
    // Add input validation
    uint64_t absolute_timestamp_ms = 0;
    if (is_value_a_raw_snowflake_id) {
        // `value_or_id` is a raw Snowflake ID. Extract the timestamp part and convert to absolute Unix epoch ms.
        // The timestamp in a Snowflake ID is relative to SNOWFLAKE_EPOCH_MS.
        absolute_timestamp_ms = (value_or_id >> SNOWFLAKE_TIMESTAMP_SHIFT) + SNOWFLAKE_EPOCH_MS;
    } else {
        // `value_or_id` is already an absolute Unix epoch timestamp in milliseconds.
        absolute_timestamp_ms = value_or_id;
    }

    // Convert milliseconds since epoch to a time_point
    auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(absolute_timestamp_ms));
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf;
    // clang-format off
    #ifdef _WIN32
        gmtime_s(&tm_buf, &time);
        auto& tm = tm_buf;
    #else
        gmtime_r(&time, &tm_buf);
        auto& tm = tm_buf;
    #endif
    // clang-format on
    auto ms_part = absolute_timestamp_ms % 1000;

    // Consider using a string view format for better performance
    static constexpr fmt::string_view format = "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z";
    return fmt::format(format, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ms_part);
}

inline u64 get_current_time_ms() {
    auto const now = std::chrono::high_resolution_clock::now();
    auto const duration = now.time_since_epoch();
    auto const millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return millis;
}
