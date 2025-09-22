#include "pubcxx/event.hpp"

#include <catch2/catch_all.hpp>

// =================================================================================
//  BEGIN: Unit Tests (Corrected Version)
// =================================================================================

// A simple event struct for testing
struct TestEvent {
    int value;
    std::string payload;
};

TEST_CASE("Core Functionality", "[dispatcher.core]") {
    EventDispatcher<TestEvent> dispatcher(2);
    // A simple sleep is a pragmatic way to wait for async tasks in these tests.
    auto wait = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };

    std::atomic<int> callback_count = 0;
    std::atomic<int> received_value = 0;

    SECTION("Subscribe and Publish") {
        auto handle = dispatcher.subscribe("topic.a", [&](TestEvent const& event) {
            callback_count++;
            received_value = event.value;
        });

        dispatcher.publish("topic.a", {101, "data"});
        wait();   // Wait for the task to be processed

        REQUIRE(callback_count == 1);
        REQUIRE(received_value == 101);

        // Publishing to a different topic should not trigger the callback
        dispatcher.publish("topic.b", {202, "other"});
        wait();
        REQUIRE(callback_count == 1);   // Should not have changed
    }

    // ... The rest of this TEST_CASE is the same ...
    SECTION("Multiple Subscribers on the same topic") {
        dispatcher.subscribe("topic.a", [&](TestEvent const&) { callback_count++; });
        dispatcher.subscribe("topic.a", [&](TestEvent const&) { callback_count++; });

        dispatcher.publish("topic.a", {10, ""});
        wait();
        REQUIRE(callback_count == 2);
    }

    SECTION("No subscribers") {
        dispatcher.publish("non.existent.topic", {0, ""});
        wait();
        REQUIRE(callback_count == 0);
    }
}

TEST_CASE("Unsubscription", "[dispatcher.unsubscribe]") {
    EventDispatcher<TestEvent> dispatcher(2);
    auto wait = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };
    std::atomic<int> counter = 0;

    auto handle1 = dispatcher.subscribe("topic.a", [&](TestEvent const&) { counter++; });
    auto handle2 = dispatcher.subscribe("topic.a", [&](TestEvent const&) { counter++; });

    dispatcher.publish("topic.a", {0, ""});
    wait();
    REQUIRE(counter == 2);

    SECTION("Unsubscribe one handler") {
        REQUIRE(dispatcher.unsubscribe(handle1) == true);
        counter = 0;
        dispatcher.publish("topic.a", {0, ""});
        wait();
        REQUIRE(counter == 1);
    }

    // ... The rest of this TEST_CASE and the Wildcard test case are the same, just use the local 'wait' lambda ...
    SECTION("Unsubscribe an invalid handle") {
        REQUIRE(dispatcher.unsubscribe(handle1) == true);
        REQUIRE(dispatcher.unsubscribe(handle1) == false);
        counter = 0;
        dispatcher.publish("topic.a", {0, ""});
        wait();
        REQUIRE(counter == 1);
    }

    SECTION("Unsubscribe all handlers") {
        REQUIRE(dispatcher.unsubscribe(handle1) == true);
        REQUIRE(dispatcher.unsubscribe(handle2) == true);
        counter = 0;
        dispatcher.publish("topic.a", {0, ""});
        wait();
        REQUIRE(counter == 0);
    }
}

TEST_CASE("Wildcard Subscription", "[dispatcher.wildcard]") {
    EventDispatcher<TestEvent> dispatcher(2);
    auto wait = [] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); };
    std::atomic<int> specific_counter = 0;
    std::atomic<int> wildcard_counter = 0;

    dispatcher.subscribe("stock.us.aapl", [&](TestEvent const&) { specific_counter++; });
    dispatcher.subscribe("stock.us.*", [&](TestEvent const&) { wildcard_counter++; });

    SECTION("Publish to specific topic matches both") {
        dispatcher.publish("stock.us.aapl", {0, ""});
        wait();
        REQUIRE(specific_counter == 1);
        REQUIRE(wildcard_counter == 1);
    }

    SECTION("Publish to other topic matches only wildcard") {
        dispatcher.publish("stock.us.goog", {0, ""});
        wait();
        REQUIRE(specific_counter == 0);
        REQUIRE(wildcard_counter == 1);
    }

    SECTION("Publish to non-matching topic matches neither") {
        dispatcher.publish("stock.eu.vw", {0, ""});
        wait();
        REQUIRE(specific_counter == 0);
        REQUIRE(wildcard_counter == 0);
    }
}

// The concurrency test does not need any changes, as it relies on the
// EventDispatcher's destructor to correctly shut down the thread pool,
// which is the most important test of the pool's shutdown behavior.
TEST_CASE("Concurrency Stress Test", "[dispatcher.concurrency]") {
    // ... This entire test case remains unchanged and is correct ...
    EventDispatcher<TestEvent> dispatcher(8);
    std::atomic<bool> test_running = true;

    std::vector<std::thread> threads;
    int const num_threads = 16;
    int const duration_ms = 500;

    std::deque<EventDispatcher<TestEvent>::Handle> handle_queue;
    std::mutex queue_mutex;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            while (test_running) {
                int action = i % 4;

                if (action == 0) {
                    auto topic = "topic." + std::to_string(rand() % 10);
                    auto handle = dispatcher.subscribe(topic, [](TestEvent const&) {});
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (handle_queue.size() < 1000) { handle_queue.push_back(handle); }
                } else if (action == 1) {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    if (!handle_queue.empty()) {
                        auto handle = handle_queue.front();
                        handle_queue.pop_front();
                        lock.unlock();
                        dispatcher.unsubscribe(handle);
                    }
                } else {
                    auto topic = "topic." + std::to_string(rand() % 10);
                    dispatcher.publish(topic, {rand(), ""});
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    test_running = false;

    for (auto& t : threads) { t.join(); }

    SUCCEED("Concurrency test completed without crashing.");
}
