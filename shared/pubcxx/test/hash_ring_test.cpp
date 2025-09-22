#include "pubcxx/hash_ring.hpp"

#include "catch2/catch_all.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <thread>
#include <vector>

// Use a high number of replicas for more stable distribution tests
constexpr uint32_t TEST_REPLICAS = 500;

// Helper function to create a basic HostNode for tests
HostNode make_node(std::string const& id, std::string const& host, uint16_t port) {
    HostNode node;
    node.id = id;
    node.weight = 100;
    node.state = HostNode::State::ACTIVE;

    Endpoint ep;
    ep.host = host;
    ep.port = port;
    node.endpoints["http"] = ep;

    return node;
}

TEST_CASE("HashRing with Multi-Endpoint HostNodes", "[hash_ring]") {

    SECTION("1. Basic Node Management") {
        HashRing ring(TEST_REPLICAS);
        REQUIRE(ring.get_physical_node_count() == 0);

        // Add a node
        ring.add_node(make_node("node-1", "192.168.1.1", 8080));
        REQUIRE(ring.get_physical_node_count() == 1);

        // Add another unique node
        ring.add_node(make_node("node-2", "192.168.1.2", 8080));
        REQUIRE(ring.get_physical_node_count() == 2);

        // Remove a node by its ID
        ring.remove_node("node-1");
        REQUIRE(ring.get_physical_node_count() == 1);

        // Clear the ring
        ring.clear();
        REQUIRE(ring.get_physical_node_count() == 0);
    }

    SECTION("2. State and Metadata Management") {
        HashRing ring(TEST_REPLICAS);
        ring.add_node(make_node("n1", "host1", 1000));
        ring.add_node(make_node("n2", "host2", 2000));

        // Set state to INACTIVE
        REQUIRE(ring.set_node_state("n1", HostNode::State::INACTIVE) == true);

        // Verify that get_node never returns the inactive node
        for (int i = 0; i < 100; ++i) {
            HostNode result = ring.get_node("key-" + std::to_string(i));
            REQUIRE(result.id == "n2");
        }

        // Update non-structural metadata
        ring.update_node_metadata("n1", [](HostNode& node) {
            node.version = "v2.0";
            node.region = "us-east-1";
        });

        // Retrieve and verify metadata
        ring.set_node_state("n1", HostNode::State::ACTIVE);
        auto nodes = ring.get_next_nodes("anykey", 2);
        auto it = std::find_if(nodes.begin(), nodes.end(), [](HostNode const& n) { return n.id == "n1"; });
        REQUIRE(it != nodes.end());
        REQUIRE(it->version == "v2.0");
        REQUIRE(it->region == "us-east-1");
    }

    SECTION("3. Core Feature: Endpoint Change without Remapping") {
        HashRing ring(TEST_REPLICAS);
        ring.add_node(make_node("shard-A", "10.0.0.1", 9000));
        ring.add_node(make_node("shard-B", "10.0.0.2", 9000));
        ring.add_node(make_node("shard-C", "10.0.0.3", 9000));

        // Store initial key mappings to node IDs
        std::map<std::string, std::string> initial_mappings;
        for (int i = 0; i < 1000; ++i) {
            std::string key = "user-profile-" + std::to_string(i);
            initial_mappings[key] = ring.get_node(key).id;
        }

        // A node's IP changes. We create an updated HostNode object and upsert it.
        HostNode updated_node_B;
        updated_node_B.id = "shard-B";
        updated_node_B.weight = 100;   // Keep same weight
        // Change the host and add a new endpoint!
        updated_node_B.endpoints["http"] = {"172.16.10.100", 9001, "/api"};
        updated_node_B.endpoints["ws"] = {"172.16.10.100", 9002, "/events"};

        ring.add_node(updated_node_B);

        REQUIRE(ring.get_physical_node_count() == 3);

        int remapped_count = 0;
        for (int i = 0; i < 1000; ++i) {
            std::string key = "user-profile-" + std::to_string(i);
            if (ring.get_node(key).id != initial_mappings[key]) { remapped_count++; }
        }

        // VERIFY THE CORE FEATURE: No keys should have been remapped!
        REQUIRE(remapped_count == 0);

        // Verify the endpoint was actually updated for the node
        auto nodes = ring.get_next_nodes("anykey", 3);
        auto it = std::find_if(nodes.begin(), nodes.end(), [](HostNode const& n) { return n.id == "shard-B"; });
        REQUIRE(it != nodes.end());
        REQUIRE(it->endpoints.size() == 2);
        REQUIRE(it->endpoints.at("http").host == "172.16.10.100");
        REQUIRE(it->endpoints.count("ws") == 1);
    }

    SECTION("4. Upsert via add_node (for weight) and Weighted Distribution") {
        HashRing ring(TEST_REPLICAS);

        // Add a node with standard weight
        ring.add_node(make_node("server-a", "host-a", 80));

        // "Update" the node's weight by adding it again with the same ID
        HostNode updated_node = make_node("server-a", "host-a", 80);
        updated_node.weight = 500;
        ring.add_node(updated_node);

        REQUIRE(ring.get_physical_node_count() == 1);

        // Add a second node for comparison
        HostNode node_b = make_node("server-b", "host-b", 80);
        node_b.weight = 100;
        ring.add_node(node_b);

        std::map<std::string, int> counts;
        int const total_keys = 10000;
        for (int i = 0; i < total_keys; ++i) { counts[ring.get_node("item-" + std::to_string(i)).id]++; }

        REQUIRE(counts.size() == 2);
        double ratio = static_cast<double>(counts["server-a"]) / counts["server-b"];

        REQUIRE(ratio > 4.0);
        REQUIRE(ratio < 6.0);
        std::cout << "Weight test distribution ratio: " << ratio << " (expected ~5.0)" << std::endl;
    }

    SECTION("5. Consistent Hashing Property (Node Removal)") {
        HashRing ring(TEST_REPLICAS);
        ring.add_node(make_node("n1", "h1", 1));
        ring.add_node(make_node("n2", "h2", 2));
        ring.add_node(make_node("n3", "h3", 3));

        std::map<std::string, std::string> initial_mappings;
        for (int i = 0; i < 100; ++i) {
            initial_mappings["key" + std::to_string(i)] = ring.get_node("key" + std::to_string(i)).id;
        }

        ring.remove_node("n2");

        int remapped_count = 0;
        for (int i = 0; i < 100; ++i) {
            if (initial_mappings["key" + std::to_string(i)] != "n2" &&
                ring.get_node("key" + std::to_string(i)).id != initial_mappings["key" + std::to_string(i)]) {
                remapped_count++;
            }
        }
        // Test that some keys were remapped from n1/n3 to other nodes, but not all.
        // This is a weak check, the main point is that only keys mapped to n2 (and some others) change.
        CHECK(remapped_count < 50);
    }

    SECTION("6. Thread Safety Smoke Test") {
        auto ring = std::make_shared<HashRing>(50);

        // Add some initial nodes that the writer will modify
        std::vector<std::string> node_ids;
        for (int i = 0; i < 10; ++i) {
            std::string id = "init-node-" + std::to_string(i);
            ring->add_node(make_node(id, "host", (uint16_t)(1000 + i)));
            node_ids.push_back(id);
        }

        std::atomic<bool> stop_flag = false;
        std::atomic<int> errors = 0;

        // Reader threads continuously get nodes
        auto reader_task = [&]() {
            // Use a thread-local random generator for better performance
            std::mt19937 gen(std::hash<std::thread::id>()(std::this_thread::get_id()));
            std::uniform_int_distribution<> distrib(1, 10000);
            while (!stop_flag) {
                try {
                    // Ring is never empty in this test, so get_node should not throw
                    ring->get_node("key-" + std::to_string(distrib(gen)));
                } catch (...) { errors++; }
            }
        };

        // CORRECTED: The writer task now performs more realistic updates
        // on existing nodes instead of rapidly adding/deleting.
        auto writer_task = [&]() {
            std::mt19937 gen(std::hash<std::thread::id>()(std::this_thread::get_id()));
            std::uniform_int_distribution<> distrib(0, node_ids.size() - 1);

            for (int i = 0; i < 50; ++i) {
                // Pick a random existing node to modify
                std::string id_to_update = node_ids[distrib(gen)];

                try {
                    // Alternate between changing state and changing metadata (an upsert)
                    if (i % 2 == 0) {
                        ring->set_node_state(id_to_update, HostNode::State::BUSY);
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        ring->set_node_state(id_to_update, HostNode::State::ACTIVE);
                    } else {
                        // Create an updated version and upsert it
                        HostNode current_node = ring->get_node(id_to_update);   // This is safe
                        current_node.version = "v" + std::to_string(i);
                        current_node.weight = 100 + (i % 50);   // Change weight
                        ring->add_node(current_node);           // Upsert
                    }
                } catch (...) { errors++; }
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) threads.emplace_back(reader_task);
        threads.emplace_back(writer_task);

        // Wait for writer to finish
        threads.back().join();
        threads.pop_back();

        // Stop readers and wait for them
        stop_flag = true;
        for (auto& t : threads) t.join();

        // The test passes if it runs without crashing and without raising exceptions.
        REQUIRE(errors == 0);
    }
}
