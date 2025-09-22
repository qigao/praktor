/**
 * @file event.hpp
 * @brief Defines the EventDispatcher and related components for a thread-safe, topic-based event system.
 */

#pragma once

#include "BS_thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mimalloc.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

/**
 * @brief Custom allocator using mimalloc for improved memory allocation performance.
 * @tparam T The type of elements to allocate.
 */
template <typename T>
class MiAllocator {
public:
    using value_type = T;           /**< The type of elements. */
    using pointer = T*;             /**< Pointer to an element. */
    using const_pointer = T const*; /**< Const pointer to an element. */
    using size_type = std::size_t;  /**< Size type. */

    /**
     * @brief Rebind allocator to a different type.
     * @tparam U The new type.
     */
    template <typename U>
    struct rebind {
        using other = MiAllocator<U>; /**< The rebinded allocator type. */
    };

    /**
     * @brief Default constructor.
     */
    MiAllocator() = default;

    /**
     * @brief Copy constructor for rebinding.
     * @tparam U The type of the other allocator.
     * @param other The other allocator.
     */
    template <typename U>
    constexpr MiAllocator(MiAllocator<U> const&) noexcept {}

    /**
     * @brief Allocates memory for n objects of type T.
     * @param n The number of objects to allocate.
     * @return A pointer to the allocated memory.
     * @throw std::bad_alloc If memory allocation fails.
     */
    T* allocate(size_t n) {
        if (n > std::size_t(-1) / sizeof(T)) { throw std::bad_alloc(); }
        if (auto p = static_cast<T*>(mi_malloc(n * sizeof(T)))) { return p; }
        throw std::bad_alloc();
    }

    /**
     * @brief Deallocates memory previously allocated by this allocator.
     * @param p A pointer to the memory to deallocate.
     * @param n The number of objects previously allocated (ignored by mimalloc).
     */
    void deallocate(T* p, size_t /*n*/) noexcept { mi_free(p); }
};

/**
 * @brief Equality comparison for MiAllocator.
 * @tparam T Type of the first allocator.
 * @tparam U Type of the second allocator.
 * @return Always true, as all MiAllocator instances are interchangeable.
 */
template <typename T, typename U>
bool operator==(MiAllocator<T> const&, MiAllocator<U> const&) {
    return true;
}

/**
 * @brief Inequality comparison for MiAllocator.
 * @tparam T Type of the first allocator.
 * @tparam U Type of the second allocator.
 * @return Always false, as all MiAllocator instances are interchangeable.
 */
template <typename T, typename U>
bool operator!=(MiAllocator<T> const&, MiAllocator<U> const&) {
    return false;
}

// --- EventDispatcher and related structs ---
/**
 * @brief Forward declaration of EventDispatcher.
 * @tparam Event The type of event to dispatch.
 */
template <typename Event>
class EventDispatcher;
/**
 * @brief Forward declaration of SubscriberList.
 * @tparam Event The type of event.
 */
template <typename Event>
struct SubscriberList;
/**
 * @brief Forward declaration of TrieNode.
 * @tparam Event The type of event.
 */
template <typename Event>
struct TrieNode;

/**
 * @brief Represents a handle to an event subscription, allowing for unsubscription.
 * @tparam Event The type of event.
 */
template <typename Event>
struct SubscriptionHandle {
    std::string topic;                             /**< The topic to which the subscription belongs. */
    std::weak_ptr<SubscriberList<Event>> list_ptr; /**< A weak pointer to the subscriber list. */
    uint64_t id = 0;                               /**< Unique identifier for this subscription. */
};

/**
 * @brief Manages a list of callbacks for a specific event topic.
 * @tparam Event The type of event.
 */
template <typename Event>
struct SubscriberList {
    using Callback = std::function<void(Event const&)>; /**< Type alias for an event callback function. */
    using CallbackMap =
        std::map<uint64_t, Callback, std::less<uint64_t>,
                 MiAllocator<std::pair<uint64_t const, Callback>>>; /**< Map of subscription IDs to callbacks. */
    std::shared_ptr<CallbackMap const> const callbacks; /**< Shared pointer to the immutable map of callbacks. */

    /**
     * @brief Default constructor. Initializes with an empty callback map.
     */
    SubscriberList() : callbacks(std::allocate_shared<CallbackMap const>(MiAllocator<CallbackMap>{})) {}

    /**
     * @brief Constructs a SubscriberList with a pre-existing callback map.
     * @param cbs A shared pointer to the callback map.
     */
    explicit SubscriberList(std::shared_ptr<CallbackMap const> cbs) : callbacks(std::move(cbs)) {}

    /**
     * @brief Dispatches an event to all registered subscribers in this list.
     * @param event The event to dispatch.
     */
    void dispatch(Event const& event) const {
        for (auto const& pair : *callbacks) { pair.second(event); }
    }
};

/**
 * @brief Represents a node in the topic trie structure.
 * Each node can have subscribers and children nodes for sub-topics.
 * @tparam Event The type of event.
 */
template <typename Event>
struct TrieNode {
    static constexpr size_t NUM_ENTRIES = 16;                 /**< Number of possible children entries (hash slots). */
    std::shared_ptr<SubscriberList<Event>> const subscribers; /**< Subscribers at this specific topic level. */
    std::shared_ptr<TrieNode const> const children[NUM_ENTRIES]; /**< Children nodes for sub-topics. */
    /**
     * @brief Default constructor.
     */
    TrieNode() = default;

    /**
     * @brief Creates a new TrieNode with an updated child at a specific index.
     * This is a functional update, returning a new node rather than modifying in place.
     * @param index The index of the child to update.
     * @param new_child The new child node.
     * @return A new TrieNode with the updated child.
     */
    std::shared_ptr<TrieNode> with_child(size_t index, std::shared_ptr<TrieNode const> new_child) const {
        auto new_node = std::allocate_shared<TrieNode>(MiAllocator<TrieNode>{}, *this);
        const_cast<std::shared_ptr<TrieNode const>&>(new_node->children[index]) = std::move(new_child);
        return new_node;
    }

    /**
     * @brief Creates a new TrieNode with updated subscribers.
     * This is a functional update, returning a new node rather than modifying in place.
     * @param new_subscribers The new subscriber list for this node.
     * @return A new TrieNode with the updated subscribers.
     */
    std::shared_ptr<TrieNode> with_subscribers(std::shared_ptr<SubscriberList<Event>> new_subscribers) const {
        auto new_node = std::allocate_shared<TrieNode>(MiAllocator<TrieNode>{}, *this);
        const_cast<std::shared_ptr<SubscriberList<Event>>&>(new_node->subscribers) =
            std::move(std::move(new_subscribers));
        return new_node;
    }
};

/**
 * @brief A thread-safe, topic-based event dispatcher.
 * It uses a trie structure to manage subscriptions and a thread pool for asynchronous event publishing.
 * @tparam Event The type of event to dispatch.
 */
template <typename Event>
class EventDispatcher {
public:
    using Callback = std::function<void(Event const&)>; /**< Type alias for an event callback. */
    using Handle = SubscriptionHandle<Event>;           /**< Type alias for a subscription handle. */

    /**
     * @brief Constructs an EventDispatcher.
     * @param num_threads The number of threads to use in the internal thread pool for dispatching events.
     */
    explicit EventDispatcher(size_t num_threads = std::thread::hardware_concurrency()) : thread_pool(num_threads) {
        root.store(std::allocate_shared<TrieNode<Event>>(MiAllocator<TrieNode<Event>>{}));
    }

    /**
     * @brief Destructor.
     */
    ~EventDispatcher() = default;

    /**
     * @brief Subscribes a callback function to a specific topic.
     * If the topic path does not exist, it will be created.
     * @param topic The topic string (e.g., "sensor.temperature", "user.login").
     * @param callback The function to be called when an event for the given topic is published.
     * @return A SubscriptionHandle that can be used to unsubscribe.
     */
    Handle subscribe(std::string const& topic, Callback callback) {
        auto parts = split_topic(topic);
        uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
        std::shared_ptr<SubscriberList<Event>> new_list;
        Handle handle;
        std::shared_ptr<TrieNode<Event>> current_root = root.load();
        while (true) {
            auto [new_root, list_for_topic] = find_or_create_path(current_root, parts);
            auto new_callbacks = std::allocate_shared<typename SubscriberList<Event>::CallbackMap>(
                MiAllocator<typename SubscriberList<Event>::CallbackMap>{}, *list_for_topic->callbacks);
            (*new_callbacks)[id] = std::move(callback);
            new_list = std::allocate_shared<SubscriberList<Event>>(MiAllocator<SubscriberList<Event>>{},
                                                                   std::move(new_callbacks));
            new_root = replace_subscribers_recursive(new_root, parts, 0, new_list);
            if (root.compare_exchange_weak(current_root, new_root)) {
                handle.topic = topic;
                handle.list_ptr = new_list;
                handle.id = id;
                break;
            }
        }
        return handle;
    }

    /**
     * @brief Unsubscribes a previously registered callback using its handle.
     * @param handle The SubscriptionHandle obtained from a prior subscribe call.
     * @return True if the subscription was successfully removed, false otherwise (e.g., handle invalid or already
     * unsubscribed).
     */
    bool unsubscribe(Handle const& handle) {
        if (handle.id == 0) return false;
        auto parts = split_topic(handle.topic);
        std::shared_ptr<TrieNode<Event>> current_root = root.load();
        while (true) {
            std::shared_ptr<SubscriberList<Event>> list_for_topic = find_list_for_topic(current_root, parts);
            if (!list_for_topic || list_for_topic->callbacks->find(handle.id) == list_for_topic->callbacks->end()) {
                return false;
            }
            auto new_callbacks = std::allocate_shared<typename SubscriberList<Event>::CallbackMap>(
                MiAllocator<typename SubscriberList<Event>::CallbackMap>{}, *list_for_topic->callbacks);
            if (new_callbacks->erase(handle.id) == 0) { return false; }
            auto new_list = std::allocate_shared<SubscriberList<Event>>(MiAllocator<SubscriberList<Event>>{},
                                                                        std::move(new_callbacks));
            auto new_root = replace_subscribers_recursive(current_root, parts, 0, new_list);
            if (root.compare_exchange_weak(current_root, new_root)) { return true; }
        }
    }

    /**
     * @brief Publishes an event to all subscribers of the given topic and its wildcard matches.
     * The event dispatching is performed asynchronously in the internal thread pool.
     * @param topic The topic to publish the event to.
     * @param event The event object to publish.
     */
    void publish(std::string const& topic, Event const& event) {
        auto parts = split_topic(topic);
        std::vector<std::shared_ptr<SubscriberList<Event>>> matched;
        std::shared_ptr<TrieNode<Event> const> current_root = root.load();
        match_topics_recursive(current_root, parts, 0, matched);
        for (auto const& subscribers : matched) {
            thread_pool.submit_task([subscribers, event] {
                if (subscribers) { subscribers->dispatch(event); }
            });
        }
    }

private:
    /**
     * @brief Splits a topic string into its constituent parts based on the '.' delimiter.
     * @param topic The topic string (e.g., "a.b.c").
     * @return A vector of strings, where each string is a part of the topic.
     */
    static std::vector<std::string> split_topic(std::string const& topic) {
        std::vector<std::string> parts;
        if (topic.empty()) return parts;
        size_t start = 0;
        for (size_t i = 0; i < topic.length(); ++i) {
            if (topic[i] == '.') {
                if (start < i) { parts.push_back(topic.substr(start, i - start)); }
                start = i + 1;
            }
        }
        parts.push_back(topic.substr(start));
        return parts;
    }

    /**
     * @brief Hashes a topic part to determine its slot in the TrieNode's children array.
     * @param part The topic part string.
     * @return A hash value (uint32_t).
     */
    static uint32_t hash_part(std::string const& part) {
        uint32_t hash = 5381;
        for (char c : part) { hash = ((hash << 5) + hash) + c; }
        return hash;
    }

    /**
     * @brief Recursively finds or creates the path in the trie for a given topic.
     * This function ensures that all intermediate nodes for the topic path exist.
     * @param node The current TrieNode to start from.
     * @param parts The vector of topic parts.
     * @return A pair containing the new root of the trie (if modified) and the SubscriberList for the target topic.
     */
    static std::pair<std::shared_ptr<TrieNode<Event>>, std::shared_ptr<SubscriberList<Event>>>
    find_or_create_path(std::shared_ptr<TrieNode<Event> const> node, std::vector<std::string> const& parts) {
        auto internal_find_or_create =
            [&parts](
                auto& self, std::shared_ptr<TrieNode<Event> const> current_node,
                size_t index) -> std::pair<std::shared_ptr<TrieNode<Event>>, std::shared_ptr<SubscriberList<Event>>> {
            if (!current_node) { current_node = std::allocate_shared<TrieNode<Event>>(MiAllocator<TrieNode<Event>>{}); }
            if (index == parts.size()) {
                auto list = current_node->subscribers
                                ? current_node->subscribers
                                : std::allocate_shared<SubscriberList<Event>>(MiAllocator<SubscriberList<Event>>{});
                return {std::const_pointer_cast<TrieNode<Event>>(current_node), list};
            }
            size_t slot = hash_part(parts[index]) % TrieNode<Event>::NUM_ENTRIES;
            auto [new_child, final_list] = self(self, current_node->children[slot], index + 1);
            if (new_child.get() == current_node->children[slot].get()) {
                return {std::const_pointer_cast<TrieNode<Event>>(current_node), final_list};
            }
            return {current_node->with_child(slot, new_child), final_list};
        };
        return internal_find_or_create(internal_find_or_create, node, 0);
    }

    /**
     * @brief Finds the SubscriberList associated with a specific topic path in the trie.
     * @param node The current TrieNode to start the search from.
     * @param parts The vector of topic parts.
     * @return A shared pointer to the SubscriberList if found, nullptr otherwise.
     */
    static std::shared_ptr<SubscriberList<Event>> find_list_for_topic(std::shared_ptr<TrieNode<Event> const> node,
                                                                      std::vector<std::string> const& parts) {
        std::shared_ptr<TrieNode<Event> const> current = node;
        for (auto const& part : parts) {
            if (!current) return nullptr;
            size_t slot = hash_part(part) % TrieNode<Event>::NUM_ENTRIES;
            current = current->children[slot];
        }
        return current ? current->subscribers : nullptr;
    }

    /**
     * @brief Recursively replaces the SubscriberList at a specific topic path in the trie.
     * This function is used to update the trie with new subscriber lists (e.g., after subscribe/unsubscribe).
     * @param node The current TrieNode to start the replacement from.
     * @param parts The vector of topic parts.
     * @param index The current index in the topic parts vector.
     * @param new_list The new SubscriberList to set at the target topic path.
     * @return A new TrieNode representing the updated path in the trie.
     */
    static std::shared_ptr<TrieNode<Event>>
    replace_subscribers_recursive(std::shared_ptr<TrieNode<Event>> node, std::vector<std::string> const& parts,
                                  size_t index, std::shared_ptr<SubscriberList<Event>> new_list) {
        if (!node) return nullptr;
        if (index == parts.size()) { return node->with_subscribers(new_list); }
        size_t slot = hash_part(parts[index]) % TrieNode<Event>::NUM_ENTRIES;
        auto new_child = replace_subscribers_recursive(std::const_pointer_cast<TrieNode<Event>>(node->children[slot]),
                                                       parts, index + 1, std::move(new_list));
        if (!new_child) return node;
        return node->with_child(slot, new_child);
    }

    /**
     * @brief Recursively matches topics in the trie for publishing.
     * This function traverses the trie to find all subscriber lists that match the given topic,
     * including those matching wildcard topics.
     * @param node The current TrieNode to start matching from.
     * @param parts The vector of topic parts for the event being published.
     * @param index The current index in the topic parts vector.
     * @param matched A vector to store all matched SubscriberList pointers.
     */
    void match_topics_recursive(std::shared_ptr<TrieNode<Event> const> node, std::vector<std::string> const& parts,
                                size_t index, std::vector<std::shared_ptr<SubscriberList<Event>>>& matched) const {
        if (!node) return;
        if (index == parts.size()) {
            if (node->subscribers && !node->subscribers->callbacks->empty()) { matched.push_back(node->subscribers); }
            return;
        }
        size_t slot = hash_part(parts[index]) % TrieNode<Event>::NUM_ENTRIES;
        if (node->children[slot]) { match_topics_recursive(node->children[slot], parts, index + 1, matched); }
        size_t wildcard_slot = hash_part("*") % TrieNode<Event>::NUM_ENTRIES;
        if (wildcard_slot != slot && node->children[wildcard_slot]) {
            match_topics_recursive(node->children[wildcard_slot], parts, index + 1, matched);
        }
    }

    std::atomic<std::shared_ptr<TrieNode<Event>>> root; /**< The root node of the topic trie. */
    std::atomic<uint64_t> next_id{1};                   /**< Atomic counter for generating unique subscription IDs. */
    BS::thread_pool<> thread_pool;                        /**< Thread pool for asynchronous event dispatching. */
};
