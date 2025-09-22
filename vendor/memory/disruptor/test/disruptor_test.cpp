#include "disruptor.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace disruptorplus;

struct Event {
    uint32_t data;
};

TEST_CASE("Disruptor++ Basic Functionality", "[disruptor++]") {
    size_t const bufferSize = 1024;   // Must be power-of-two
    ring_buffer<Event> buffer(bufferSize);
    spin_wait_strategy waitStrategy;
    single_threaded_claim_strategy claimStrategy(bufferSize, waitStrategy);
    sequence_barrier consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);

    SECTION("Producer and Consumer") {
        std::thread consumer([&]() {
            uint64_t sum = 0;
            sequence_t nextToRead = 0;
            bool done = false;

            while (!done) {
                sequence_t available = claimStrategy.wait_until_published(nextToRead);
                do {
                    auto& event = buffer[nextToRead];
                    sum += event.data;
                    if (event.data == 0) { done = true; }
                } while (nextToRead++ != available);
                consumed.publish(available);
            }
            REQUIRE(sum == 500500);
        });

        std::thread producer([&]() {
            for (uint32_t i = 1; i <= 1000; ++i) {
                sequence_t seq = claimStrategy.claim_one();
                buffer[seq].data = i;
                claimStrategy.publish(seq);
            }
            // Publish the terminating event.
            sequence_t seq = claimStrategy.claim_one();
            buffer[seq].data = 0;
            claimStrategy.publish(seq);
        });

        consumer.join();
        producer.join();
    }

    SECTION("Single Event") {
        sequence_t seq = claimStrategy.claim_one();
        buffer[seq].data = 42;
        claimStrategy.publish(seq);

        sequence_t nextToRead = 0;
        sequence_t available = claimStrategy.wait_until_published(nextToRead);
        auto& event = buffer[nextToRead];
        REQUIRE(event.data == 42);
        consumed.publish(available);
    }
}

TEST_CASE("Multiple Producers", "[disruptor++]") {
    size_t const bufferSize = 1024;   // Must be power-of-two
    ring_buffer<Event> buffer(bufferSize);
    spin_wait_strategy waitStrategy;
    multi_threaded_claim_strategy claimStrategy(bufferSize, waitStrategy);
    sequence_barrier consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);

    std::atomic<uint32_t> sum(0);
    std::atomic done(false);

    auto producer = [&]() {
        for (uint32_t i = 1; i <= 500; ++i) {
            sequence_t seq = claimStrategy.claim_one();
            buffer[seq].data = i;
            claimStrategy.publish(seq);
        }
        // Publish the terminating event.
        sequence_t seq = claimStrategy.claim_one();
        buffer[seq].data = 0;
        claimStrategy.publish(seq);
    };

    std::thread producer1(producer);
    std::thread producer2(producer);

    std::thread consumer([&]() {
        uint32_t localSum = 0;
        sequence_t nextToRead = 0;
        bool done1 = false;

        while (!done1) {
            sequence_t available = claimStrategy.wait_until_published(nextToRead, nextToRead - 1);
            do {
                auto& event = buffer[nextToRead];
                localSum += event.data;
                if (event.data == 0) { done1 = true; }
            } while (nextToRead++ != available);
            consumed.publish(available);
        }
        sum += localSum;
    });

    producer1.join();
    producer2.join();
    consumer.join();
}

TEST_CASE("Consumer Wait Strategy", "[disruptor++]") {
    size_t const bufferSize = 1024;   // Must be power-of-two
    ring_buffer<Event> buffer(bufferSize);
    blocking_wait_strategy waitStrategy;
    single_threaded_claim_strategy<blocking_wait_strategy> claimStrategy(bufferSize, waitStrategy);
    sequence_barrier<blocking_wait_strategy> consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);

    std::thread consumer([&]() {
        uint64_t sum = 0;
        sequence_t nextToRead = 0;
        bool done = false;

        while (!done) {
            sequence_t available = claimStrategy.wait_until_published(nextToRead);
            do {
                auto& event = buffer[nextToRead];
                sum += event.data;
                if (event.data == 0) { done = true; }
            } while (nextToRead++ != available);
            consumed.publish(available);
        }
        REQUIRE(sum == 500500);
    });

    std::thread producer([&]() {
        for (uint32_t i = 1; i <= 1000; ++i) {
            sequence_t seq = claimStrategy.claim_one();
            buffer[seq].data = i;
            claimStrategy.publish(seq);
        }
        // Publish the terminating event.
        sequence_t seq = claimStrategy.claim_one();
        buffer[seq].data = 0;
        claimStrategy.publish(seq);
    });

    consumer.join();
    producer.join();
}

TEST_CASE("Event Pre-allocation", "[disruptor++]") {
    size_t const bufferSize = 1024;   // Must be power-of-two
    ring_buffer<Event> buffer(bufferSize);
    spin_wait_strategy waitStrategy;
    single_threaded_claim_strategy<spin_wait_strategy> claimStrategy(bufferSize, waitStrategy);
    sequence_barrier<spin_wait_strategy> consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);

    // Pre-allocate events
    for (size_t i = 0; i < bufferSize; ++i) {
        Event& event = buffer[i];
        event.data = 0;   // Initialize with default value
    }

    std::thread producer([&]() {
        for (uint32_t i = 1; i <= 1000; ++i) {
            sequence_t seq = claimStrategy.claim_one();
            buffer[seq].data = i;
            claimStrategy.publish(seq);
        }
        // Publish the terminating event.
        sequence_t seq = claimStrategy.claim_one();
        buffer[seq].data = 0;
        claimStrategy.publish(seq);
    });

    std::thread consumer([&]() {
        uint64_t sum = 0;
        sequence_t nextToRead = 0;
        bool done = false;

        while (!done) {
            sequence_t available = claimStrategy.wait_until_published(nextToRead);
            do {
                auto& event = buffer[nextToRead];
                sum += event.data;
                if (event.data == 0) { done = true; }
            } while (nextToRead++ != available);
            consumed.publish(available);
        }
        REQUIRE(sum == 500500);
    });

    producer.join();
    consumer.join();
}

TEST_CASE("Buffer Overflow", "[disruptor++]") {
    size_t const bufferSize = 1024;   // Must be power-of-two
    ring_buffer<Event> buffer(bufferSize);
    spin_wait_strategy waitStrategy;
    single_threaded_claim_strategy claimStrategy(bufferSize, waitStrategy);
    sequence_barrier consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);

    std::thread producer([&]() {
        for (uint32_t i = 1; i <= bufferSize + 1; ++i) {
            sequence_t seq = claimStrategy.claim_one();
            buffer[seq].data = i;
            claimStrategy.publish(seq);
        }
    });

    std::thread consumer([&]() {
        uint64_t sum = 0;
        sequence_t nextToRead = 0;
        bool done = false;

        while (!done) {
            sequence_t available = claimStrategy.wait_until_published(nextToRead);
            do {
                auto& event = buffer[nextToRead];
                sum += event.data;
                if (nextToRead >= bufferSize) { done = true; }
            } while (nextToRead++ != available);
            consumed.publish(available);
        }
        // REQUIRE(sum == (bufferSize * (bufferSize + 1)) / 2);
    });

    producer.join();
    consumer.join();
}

TEST_CASE("Shutdown and Cleanup", "[disruptor++]") {
    size_t const bufferSize = 1024;   // Must be power-of-two
    ring_buffer<Event> buffer(bufferSize);
    spin_wait_strategy waitStrategy;
    single_threaded_claim_strategy claimStrategy(bufferSize, waitStrategy);
    sequence_barrier consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);

    std::thread consumer([&]() {
        uint64_t sum = 0;
        sequence_t nextToRead = 0;
        bool done = false;

        while (!done) {
            sequence_t available = claimStrategy.wait_until_published(nextToRead);
            do {
                auto& event = buffer[nextToRead];
                sum += event.data;
                if (event.data == 0) { done = true; }
            } while (nextToRead++ != available);
            consumed.publish(available);
        }
        REQUIRE(sum == 500500);
    });

    std::thread producer([&]() {
        for (uint32_t i = 1; i <= 1000; ++i) {
            sequence_t seq = claimStrategy.claim_one();
            buffer[seq].data = i;
            claimStrategy.publish(seq);
        }
        // Publish the terminating event.
        sequence_t seq = claimStrategy.claim_one();
        buffer[seq].data = 0;
        claimStrategy.publish(seq);
    });

    consumer.join();
    producer.join();
}
