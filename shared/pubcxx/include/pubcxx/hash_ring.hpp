#pragma once

#include "node.h"
#include "phmap/btree.h"
#include "phmap/phmap.h"
#include "xxhash.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @file hash_ring.h
 * @brief A thread-safe, high-performance, and feature-rich consistent hash ring.
 *
 * This implementation uses high-performance data structures to minimize latency and
 * provides a rich node metadata model to enable intelligent, state-aware routing
 * in a distributed system.
 */

/**
 * @brief Custom hasher for std::string using the high-speed XXH32 algorithm.
 */
struct XXH32Hasher {
    uint32_t operator()(std::string const& key) const noexcept {
        constexpr uint32_t seed = 0;   // Fixed seed ensures consistent hashing
        return XXH32(key.data(), key.length(), seed);
    }
};

/**
 * @brief A thread-safe, state-aware consistent hash ring.
 *
 * Maps keys to nodes to minimize remapping on topology changes. Optimized for
 * read-heavy workloads using a cache-friendly B-Tree for the ring and a
 * parallel hash map for node lookups.
 */
class HashRing {
public:
    /**
     * @brief Constructs a hash ring.
     * @param base_replicas The number of virtual nodes per 100 units of weight.
     *        Higher values improve key distribution at the cost of memory.
     */
    explicit HashRing(uint32_t base_replicas) : replicas_(base_replicas) {
        if (base_replicas == 0) { throw std::invalid_argument("Base number of replicas must be positive."); }
    }

    /**
     * @brief Adds a node or updates it if it exists (upsert).
     * This is the recommended way to update any property, including weight, as it
     * correctly rebuilds the node's presence on the ring.
     * @param node The HostNode object to add or update. Its weight must be > 0.
     */
    void add_node(HostNode const& node) {
        if (node.id.empty()) { throw std::invalid_argument("HostNode ID must not be empty."); }
        if (node.weight == 0) { throw std::invalid_argument("HostNode weight must be positive."); }

        std::lock_guard<std::shared_mutex> lock(mutex_);

        // If a node with this ID already exists, remove its old state first.
        // This correctly handles changes in weight.
        if (physical_nodes_.contains(node.id)) { remove_node_internal(node.id); }

        physical_nodes_.emplace(node.id, node);
        add_replicas_for_node(node);
    }

    /**
     * @brief Removes a node from the ring by its unique ID. Idempotent.
     * @param node_id The stable ID of the node to remove.
     */
    void remove_node(std::string const& node_id) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        remove_node_internal(node_id);
    }

    /**
     * @brief Updates the state of a node by its unique ID.
     * @param node_id The stable ID of the node.
     * @return True if the node was found and updated, false otherwise.
     */
    bool set_node_state(std::string const& node_id, HostNode::State state) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        auto it = physical_nodes_.find(node_id);
        if (it != physical_nodes_.end()) {
            it->second.state = state;
            return true;
        }
        return false;
    }

    /**
     * @brief Updates non-structural metadata for an existing node by its unique ID.
     * Use this for efficient changes that don't affect the ring structure (e.g.,
     * updating version, region, but NOT weight).
     * @param node_id The stable ID of the node to update.
     * @param update_fn A function that receives a mutable reference to the HostNode.
     * @return True if the node was found and updated, false otherwise.
     */
    bool update_node_metadata(std::string const& node_id, std::function<void(HostNode&)> const& update_fn) {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        auto it = physical_nodes_.find(node_id);
        if (it != physical_nodes_.end()) {
            update_fn(it->second);
            return true;
        }
        return false;
    }

    /**
     * @brief Gets the single available node responsible for a key.
     * @param key The key to map.
     * @return The responsible HostNode.
     * @throws std::runtime_error if no available nodes exist in the ring.
     */
    HostNode get_node(std::string const& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto node_opt = find_next_available_node(hash(key));
        if (!node_opt.has_value()) { throw std::runtime_error("No available nodes in hash ring."); }
        return *node_opt;
    }

    /**
     * @brief Gets a list of available successor nodes for a key, for replication.
     * @param key The key to map.
     * @param count The desired number of unique successor nodes.
     * @return A vector of unique HostNode objects (size may be less than count).
     */
    std::vector<HostNode> get_next_nodes(std::string const& key, size_t count) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (ring_.empty() || count == 0) { return {}; }

        size_t num_to_find = std::min(count, physical_nodes_.size());
        std::vector<HostNode> result_nodes;
        result_nodes.reserve(num_to_find);
        std::set<std::string> found_node_ids;

        auto it = ring_.lower_bound(hash(key));
        size_t ring_iterations = 0;

        while (result_nodes.size() < num_to_find && ring_iterations < ring_.size()) {
            if (it == ring_.end()) {
                it = ring_.begin();   // Wrap around
            }

            std::string const& node_id = it->second;
            HostNode const& node = physical_nodes_.at(node_id);

            if (is_node_available(node)) {
                if (found_node_ids.insert(node_id).second) { result_nodes.push_back(node); }
            }
            ++it;
            ring_iterations++;
        }

        return result_nodes;
    }

    /**
     * @brief Removes all nodes from the ring.
     */
    void clear() noexcept {
        std::lock_guard<std::shared_mutex> lock(mutex_);
        ring_.clear();
        physical_nodes_.clear();
    }

    /**
     * @brief Gets the number of physical nodes currently in the configuration.
     */
    size_t get_physical_node_count() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return physical_nodes_.size();
    }

private:
    /**
     * @brief Defines the policy for whether a node can receive traffic.
     */
    bool is_node_available(HostNode const& node) const noexcept { return node.state == HostNode::State::ACTIVE; }

    /**
     * @brief Finds the next available node on the ring starting from a hash value.
     */
    std::optional<HostNode> find_next_available_node(uint32_t key_hash) const {
        if (ring_.empty()) return std::nullopt;

        auto it = ring_.lower_bound(key_hash);
        auto start_it = it;

        do {
            if (it == ring_.end()) it = ring_.begin();

            auto const& node_id = it->second;
            auto const& node = physical_nodes_.at(node_id);

            if (is_node_available(node)) return node;

            ++it;
        } while (it != start_it);

        return std::nullopt;
    }

    void remove_node_internal(std::string const& node_id) {
        auto node_it = physical_nodes_.find(node_id);
        if (node_it == physical_nodes_.end()) return;

        remove_replicas_for_node(node_it->second);
        physical_nodes_.erase(node_it);
    }

    void add_replicas_for_node(HostNode const& node) {
        std::string const& node_id = node.id;
        uint32_t num_replicas = calculate_replicas(node.weight);

        for (uint32_t i = 0; i < num_replicas; ++i) {
            std::string vnode_key = node_id + "#" + std::to_string(i);
            uint32_t hash_value = hash(vnode_key);

            while (ring_.count(hash_value)) hash_value++;   // Linear probing

            ring_[hash_value] = node_id;
        }
    }

    void remove_replicas_for_node(HostNode const& node) {
        std::string const& node_id = node.id;
        uint32_t num_replicas = calculate_replicas(node.weight);

        for (uint32_t i = 0; i < num_replicas; ++i) {
            std::string vnode_key = node_id + "#" + std::to_string(i);
            uint32_t hash_value = hash(vnode_key);

            while (ring_.count(hash_value) && ring_.at(hash_value) != node_id) hash_value++;

            if (ring_.count(hash_value)) ring_.erase(hash_value);
        }
    }

    uint32_t calculate_replicas(uint32_t weight) const {
        double ratio = static_cast<double>(weight) / 100.0;
        auto replicas = static_cast<uint32_t>(std::round(ratio * replicas_));
        return std::max(1u, replicas);
    }

    uint32_t hash(std::string const& key) const noexcept {
        constexpr uint32_t seed = 0;
        return XXH32(key.data(), key.length(), seed);
    }

    using RingMap = phmap::btree_map<uint32_t, std::string>;
    using NodeMap =
        phmap::parallel_flat_hash_map<std::string, HostNode, XXH32Hasher, std::equal_to<std::string>,
                                      std::allocator<std::pair<std::string const, HostNode>>, 4, std::shared_mutex>;

    uint32_t replicas_;
    RingMap ring_;
    NodeMap physical_nodes_;
    mutable std::shared_mutex mutex_;
};
