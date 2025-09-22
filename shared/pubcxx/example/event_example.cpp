#include "pubcxx/event.hpp"

#include <iostream>
#include <thread>

// A simple struct for our events
struct PriceUpdate {
    std::string symbol;
    double price;
};

// A free function to be used as a callback
void on_apple_price(PriceUpdate const& event) {
    std::cout << "[Free Function Handler]: " << event.symbol << " is now " << event.price << std::endl;
}

int main() {
    std::cout << "--- Initial mimalloc stats ---\n";
    mi_stats_print(nullptr);

    // Create a dispatcher with 4 worker threads
    EventDispatcher<PriceUpdate> dispatcher(4);

    std::cout << "\n--- Subscribing handlers ---" << std::endl;
    // Subscribe a free function
    auto handle_apple_fn = dispatcher.subscribe("stock.us.aapl", on_apple_price);

    // Subscribe a lambda to the same topic
    auto handle_apple_lambda = dispatcher.subscribe("stock.us.aapl", [](PriceUpdate const& event) {
        std::cout << "[Lambda Handler]: Received update for " << event.symbol << ", price: " << event.price
                  << std::endl;
    });

    // Subscribe to a different stock
    dispatcher.subscribe("stock.us.msft", [](PriceUpdate const& event) {
        std::cout << "[Microsoft Handler]: " << event.symbol << " at " << event.price << std::endl;
    });

    // Subscribe to a wildcard topic
    dispatcher.subscribe("stock.us.*", [](PriceUpdate const& event) {
        std::cout << "[US Wildcard Handler]: Caught an update for " << event.symbol << std::endl;
    });

    std::cout << "\n--- Publishing events ---" << std::endl;
    std::cout << "\nPublishing to 'stock.us.aapl' (should trigger 3 handlers)..." << std::endl;
    dispatcher.publish("stock.us.aapl", {"AAPL", 175.50});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // Allow time for processing

    std::cout << "\nPublishing to 'stock.us.goog' (should trigger 1 handler)..." << std::endl;
    dispatcher.publish("stock.us.goog", {"GOOG", 2800.00});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "\nPublishing to 'stock.eu.bmw' (should trigger 0 handlers)..." << std::endl;
    dispatcher.publish("stock.eu.bmw", {"BMW", 85.30});

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "\n--- Unsubscribing the free function handler for Apple ---" << std::endl;
    bool unsubscribed = dispatcher.unsubscribe(handle_apple_fn);
    std::cout << "Unsubscribe successful: " << std::boolalpha << unsubscribed << std::endl;

    std::cout << "\n--- Publishing to 'stock.us.aapl' again (should trigger 2 handlers) ---" << std::endl;
    dispatcher.publish("stock.us.aapl", {"AAPL", 176.00});

    std::cout << "\nWaiting for all tasks to complete before exiting..." << std::endl;
    // The EventDispatcher destructor will automatically wait for the thread pool to finish.
    // We add a final sleep just to ensure the last console output appears before main exits.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\n--- Final mimalloc stats ---\n";
    mi_stats_print(nullptr);

    return 0;
}
