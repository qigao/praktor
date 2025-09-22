#pragma once

#include "simple_xxhash.hpp"
#include "types.hpp"

#include <functional>   // For std::less (used in MiStringMap)
#include <map>
#include <memory>   // For std::unique_ptr, std::shared_ptr, std::allocator_traits
// #include <mimalloc.h> // Temporarily disabled
#include <new>   // For placement new, std::bad_alloc
#include <cstdlib>  // For std::malloc, std::free
#include <string>
#include <string_view>   // For std::string_view
#include <utility>       // For std::forward, std::move
#include <vector>

// --- Use mimalloc's STL allocator ---

/**
 * @brief Custom STL allocator that uses mimalloc for memory management.
 * Use this allocator with STL containers to leverage mimalloc's performance.
 * @tparam T The type of element the allocator is for.
 */
template <typename T>
class MiAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = MiAllocator<U>;
    };

    MiAllocator() noexcept = default;

    template <typename U>
    MiAllocator(const MiAllocator<U>&) noexcept {}

    pointer allocate(size_type n) {
        if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }
        void* p = std::malloc(n * sizeof(T)); // Use standard malloc temporarily
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(p);
    }

    void deallocate(pointer p, size_type) noexcept {
        std::free(p); // Use standard free temporarily
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        p->~U();
    }

    bool operator==(const MiAllocator&) const noexcept { return true; }
    bool operator!=(const MiAllocator&) const noexcept { return false; }
};

/**
 * @brief Custom deleter for smart pointers (unique_ptr, shared_ptr)
 *        that uses mimalloc for deallocation.
 * Ensures the object's destructor is called before freeing memory with mi_free.
 * @tparam T The type of object being managed.
 */
template <typename T>
struct MiMallocDeleter {
    void operator()(T* p) const noexcept {
        if (p) {
            p->~T();      // Explicitly call destructor
            std::free(p); // Then free memory using standard free
        }
    }
};

/**
 * @brief Alias for std::unique_ptr using MiMallocDeleter.
 * Manages a single object allocated via mimalloc.
 * @tparam T The type of object being managed.
 */
template <typename T>
using MiUniquePtr = std::unique_ptr<T, MiMallocDeleter<T>>;

/**
 * @brief Alias for std::shared_ptr configured to potentially work with MiMallocDeleter.
 * Note: When using make_mi_shared, the custom deleter is automatically included.
 * @tparam T The type of object being managed.
 */
template <typename T>
using MiSharedPtr = std::shared_ptr<T>;

/**
 * @brief Factory function to create a MiSharedPtr managing an object allocated via mimalloc.
 * Constructs the object in-place using placement new on mimalloc-allocated memory.
 * Provides exception safety during construction.
 *
 * @tparam T The type of object to create.
 * @tparam Args Types of arguments for T's constructor.
 * @param args Arguments to forward to T's constructor.
 * @return MiSharedPtr<T> managing the newly created object.
 * @throws std::bad_alloc If mimalloc fails to allocate memory.
 * @throws Any exception thrown by T's constructor.
 */
template <typename T, typename... Args>
MiSharedPtr<T> make_mi_shared(Args&&... args) {
    void* raw_mem = std::malloc(sizeof(T)); // Use standard malloc temporarily
    if (!raw_mem) {
        throw std::bad_alloc();   // Allocation failed
    }
    T* obj_ptr = nullptr;
    try {
        // Construct using perfect forwarding in the allocated memory
        obj_ptr = new (raw_mem) T(std::forward<Args>(args)...);
        // Create shared_ptr with the custom deleter instance
        return MiSharedPtr<T>(obj_ptr, MiMallocDeleter<T>{});
    } catch (...) {
        if (raw_mem) { std::free(raw_mem); }
        throw;   // Re-throw the exception
    }
}

/**
 * @brief Factory function to create a MiUniquePtr managing an object allocated via mimalloc.
 * Constructs the object in-place using placement new on mimalloc-allocated memory.
 * Provides exception safety during construction.
 *
 * @tparam T The type of object to create.
 * @tparam Args Types of arguments for T's constructor.
 * @param args Arguments to forward to T's constructor.
 * @return MiUniquePtr<T> managing the newly created object.
 * @throws std::bad_alloc If mimalloc fails to allocate memory.
 * @throws Any exception thrown by T's constructor.
 */
template <typename T, typename... Args>
MiUniquePtr<T> make_mi_unique(Args&&... args) {
    void* raw_mem = std::malloc(sizeof(T)); // Use standard malloc temporarily
    if (!raw_mem) {
        throw std::bad_alloc();   // Allocation failed
    }
    T* obj_ptr = nullptr;
    try {
        obj_ptr = new (raw_mem) T(std::forward<Args>(args)...);
        return MiUniquePtr<T>(obj_ptr);
    } catch (...) {
        if (raw_mem) { std::free(raw_mem); }
        throw;   // Re-throw the exception
    }
}

// --- Define aliases for common types using the mimalloc allocator ---
struct StringKeyXxHash {
    using is_transparent = void;   // Enable heterogeneous lookups

    std::size_t operator()(std::string_view sv) const { return XXHash::xxh3_64_sum_buf(sv.data(), sv.size()); }
};

/**
 * @brief Alias for std::vector<uint8_t> using MiAllocator.
 */
using MiVectorU8 = std::vector<u8, MiAllocator<u8>>;

using MiVectorU64 = std::vector<u64, MiAllocator<u64>>;

using MiString = std::basic_string<char, std::char_traits<char>, MiAllocator<char>>;

/**
 * @brief Alias for std::basic_string<char> using MiAllocator.
 */
struct MiStringHash {
    using is_transparent = void;   // Enables heterogeneous lookup

    size_t operator()(std::string_view sv) const noexcept {
        return StringKeyXxHash{}(sv);
    }

    size_t operator()(MiString const& s) const noexcept {
        return StringKeyXxHash{}(std::string_view(s.data(), s.size()));
    }

    size_t operator()(std::string const& s) const noexcept {
        return StringKeyXxHash{}(std::string_view(s.data(), s.size()));
    }

    size_t operator()(char const* s) const noexcept {
        return StringKeyXxHash{}(std::string_view(s));
    }
};

struct MiStringEqual {
    using is_transparent = void;   // Enables heterogeneous lookup

    bool operator()(MiString const& lhs, MiString const& rhs) const { return lhs == rhs; }

    bool operator()(MiString const& lhs, std::string_view rhs) const {
        return std::string_view(lhs.data(), lhs.size()) == rhs;
    }

    bool operator()(std::string_view lhs, MiString const& rhs) const {
        return lhs == std::string_view(rhs.data(), rhs.size());
    }

    bool operator()(std::string_view lhs, std::string_view rhs) const { return lhs == rhs; }

    bool operator()(MiString const& lhs, std::string const& rhs) const {
        return std::string(lhs.begin(), lhs.end()) == rhs;
    }

    bool operator()(std::string const& lhs, MiString const& rhs) const {
        return lhs == std::string(rhs.begin(), rhs.end());
    }

    bool operator()(MiString const& lhs, char const* rhs) const { return std::string(lhs.begin(), lhs.end()) == rhs; }

    bool operator()(char const* lhs, MiString const& rhs) const { return lhs == std::string(rhs.begin(), rhs.end()); }

    bool operator()(std::string const& lhs, char const* rhs) const { return lhs == rhs; }

    bool operator()(char const* lhs, std::string const& rhs) const { return lhs == rhs; }
};

/**
 * @brief Alias for std::vector<MiString> using MiAllocator.
 */
using MiStringVector = std::vector<MiString, MiAllocator<MiString>>;

/**
 * @brief Alias for std::map<MiString, MiString> using MiAllocator for its internal nodes.
 * Both keys and values are MiString, which also use MiAllocator internally.
 */
using MiStringMap = std::map<MiString,      // Key type (uses MiAllocator internally)
                             MiString,      // Value type (uses MiAllocator internally)
                             std::less<>,   // Comparator (default std::less is fine)
                             MiAllocator<std::pair<MiString const, MiString>>   // Allocator for map nodes
                             >;
using MiMapType = MiStringMap;

/**
 * @brief Helper function to convert a MiString to a MiVectorUint8.
 * Creates a byte vector containing the character data of the string.
 * @param s The input MiString.
 * @return A MiVectorUint8 containing the bytes of the string.
 */
inline MiVectorU8 string_to_vec(MiString const& s) {
    MiVectorU8 vec(s.begin(), s.end());
    return vec;
}

/**
 * @brief General alias for a byte vector, using the mimalloc-based vector.
 */
using ByteVector = MiVectorU8;

/**
 * @brief High-performance allocator for frequent small allocations
 * Uses a memory pool for better cache locality and reduced fragmentation
 */
template <typename T>
class PoolAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template <typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };

    PoolAllocator() noexcept = default;

    template <typename U>
    PoolAllocator(const PoolAllocator<U>&) noexcept {}

    pointer allocate(size_type n) {
        if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        size_t bytes = n * sizeof(T);
        // For small allocations, use the pool; for large ones, use standard malloc temporarily
        void* p = std::malloc(bytes);

        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(p);
    }

    void deallocate(pointer p, size_type n) noexcept {
        std::free(p); // Use standard free temporarily
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        p->~U();
    }

    bool operator==(const PoolAllocator&) const noexcept { return true; }
    bool operator!=(const PoolAllocator&) const noexcept { return false; }
};

// --- End mimalloc setup ---
