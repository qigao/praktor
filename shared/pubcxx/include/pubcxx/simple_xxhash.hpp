#pragma once

#include <fstream>
#include <iomanip>     // For std::hex, std::setw, std::setfill
#include <sstream>     // For std::stringstream
#include <stdexcept>   // For std::runtime_error
#include <string>
#include <system_error>   // For std::system_error
#include <vector>

#define XXH_INLINE_ALL
#include "xxhash.h"

class XXHash {
public:
    // --- Public Static Interface ---

    /**
     * @brief Calculates the XXH3 64-bit hash of a file.
     * @param filename The path to the file.
     * @return The 64-bit hash as a 16-character hexadecimal string.
     * @throws std::runtime_error If the file cannot be opened or read.
     * @throws std::runtime_error If there's an error during hashing.
     */
    inline static std::string xxh3_64_sum_file_str(std::string const& filename) {
        XXH64_hash_t const hash_val = process_file_internal<XXH64_hash_t>(
            filename,
            [](XXH3_state_t* state) {   // Reset function
                return XXH3_64bits_reset(state);
            },
            [](XXH3_state_t* state, void const* data, size_t len) {   // Update function
                return XXH3_64bits_update(state, data, len);
            },
            [](XXH3_state_t* state) {   // Digest function
                return XXH3_64bits_digest(state);
            });

        // Format the 64-bit hash
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << hash_val;
        return ss.str();
    }

    /**
     * @brief Calculates the XXH3 64-bit hash of a file.
     * @param filename The path to the file.
     * @return The 64-bit hash as a 16-character hexadecimal string.
     * @throws std::runtime_error If the file cannot be opened or read.
     * @throws std::runtime_error If there's an error during hashing.
     */
    inline static uint64_t xxh3_64_sum_file(std::string const& filename) {
        XXH64_hash_t const hash_val = process_file_internal<XXH64_hash_t>(
            filename,
            [](XXH3_state_t* state) {   // Reset function
                return XXH3_64bits_reset(state);
            },
            [](XXH3_state_t* state, void const* data, size_t len) {   // Update function
                return XXH3_64bits_update(state, data, len);
            },
            [](XXH3_state_t* state) {   // Digest function
                return XXH3_64bits_digest(state);
            });

        return hash_val;
    }

    /**
     * @brief Calculates the XXH3 64-bit hash of memory.
     * @param data The pointer to the memory buffer.
     * @param len The length of the memory buffer in bytes.
     * @return The 64-bit hash.
     */
    inline static uint64_t xxh3_64_sum_buf(void const* data, size_t len) {
        XXH3_state_t* state = XXH3_createState();
        if (!state) { throw std::runtime_error("XXHasher: Failed to allocate XXH3 state."); }
        XXH64_hash_t hash_val = 0;
        try {
            if (XXH3_64bits_reset(state) == XXH_ERROR) {
                throw std::runtime_error("XXHasher: Could not reset XXH3 state.");
            }
            if (XXH3_64bits_update(state, data, len) == XXH_ERROR) {
                throw std::runtime_error("XXHasher: XXH3 update failed.");
            }
            hash_val = XXH3_64bits_digest(state);
        } catch (...) {
            XXH3_freeState(state);
            throw;
        }
        XXH3_freeState(state);
        return hash_val;
    }

    /**
     * @brief Calculates the XXH3 64-bit hash of memory.
     * @param data The pointer to the memory buffer.
     * @param len The length of the memory buffer in bytes.
     * @return The 64-bit hash as a 16-character hexadecimal string.
     */
    inline static std::string xxh3_64_sum_buf_str(void const* data, size_t len) {
        XXH64_hash_t const hash_val = xxh3_64_sum_buf(data, len);
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << hash_val;
        return ss.str();
    }

    inline static std::string xxh3_128_str(std::string const& filename) {
        XXH128_hash_t const hash_val = process_file_internal<XXH128_hash_t>(
            filename,
            [](XXH3_state_t* state) {   // Reset function
                return XXH3_128bits_reset(state);
            },
            [](XXH3_state_t* state, void const* data, size_t len) {   // Update function
                return XXH3_128bits_update(state, data, len);
            },
            [](XXH3_state_t* state) {   // Digest function
                return XXH3_128bits_digest(state);
            });

        // Format the 128-bit hash (high part first)
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << hash_val.high64 << std::hex << std::setw(16)
           << std::setfill('0') << hash_val.low64;
        return ss.str();
    }

    /**
     * @brief Calculates the XXH3 128-bit hash of memory.
     * @param data The pointer to the memory buffer.
     * @param len The length of the memory buffer in bytes.
     * @return The 128-bit hash.
     */
    inline static XXH128_hash_t xxh3_128_sum_buf(void const* data, size_t len) {
        XXH3_state_t* state = XXH3_createState();
        if (!state) { throw std::runtime_error("XXHasher: Failed to allocate XXH3 state."); }
        XXH128_hash_t hash_val = {0, 0};
        try {
            if (XXH3_128bits_reset(state) == XXH_ERROR) {
                throw std::runtime_error("XXHasher: Could not reset XXH3 state.");
            }
            if (XXH3_128bits_update(state, data, len) == XXH_ERROR) {
                throw std::runtime_error("XXHasher: XXH3 update failed.");
            }
            hash_val = XXH3_128bits_digest(state);
        } catch (...) {
            XXH3_freeState(state);
            throw;
        }
        XXH3_freeState(state);
        return hash_val;
    }

    /**
     * @brief Calculates the XXH3 128-bit hash of memory.
     * @param data The pointer to the memory buffer.
     * @param len The length of the memory buffer in bytes.
     * @return The 128-bit hash as a 32-character hexadecimal string.
     */
    inline static std::string xxh3_128_sum_str(void const* data, size_t len) {
        XXH128_hash_t const hash_val = xxh3_128_sum_buf(data, len);
        std::stringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << hash_val.high64 << std::hex << std::setw(16)
           << std::setfill('0') << hash_val.low64;
        return ss.str();
    }

private:
    // --- Private Implementation Details ---

    // Define a reasonable buffer size for reading files
    static constexpr size_t BUFFER_SIZE = 8192;   // 8 KB

    // Templated internal processing function to avoid code duplication
    template <typename HashType, typename ResetFunc, typename UpdateFunc, typename DigestFunc>
    inline static HashType process_file_internal(std::string const& filename, ResetFunc reset_fn, UpdateFunc update_fn,
                                                 DigestFunc digest_fn) {
        // RAII for file stream
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            // Include system error message for better diagnostics
            throw std::system_error(errno, std::generic_category(), "Error opening file '" + filename + "'");
        }

        // Improve performance by potentially disabling sync with stdio
        // file.sync_with_stdio(false); // Optional: Can sometimes help

        // --- RAII for XXH3 State ---
        struct XXHStateGuard {
            XXH3_state_t* state = nullptr;

            XXHStateGuard() {
                state = XXH3_createState();
                if (!state) { throw std::runtime_error("XXHasher: Failed to allocate XXH3 state."); }
            }

            ~XXHStateGuard() {
                if (state) { XXH3_freeState(state); }
            }

            // Disable copying
            XXHStateGuard(XXHStateGuard const&) = delete;
            XXHStateGuard& operator=(XXHStateGuard const&) = delete;
        } state_guard;

        XXH3_state_t* state = state_guard.state;

        // Reset the state using the provided reset function
        if (reset_fn(state) == XXH_ERROR) {
            throw std::runtime_error("XXHasher: Could not reset XXH3 state for file '" + filename + "'.");
        }

        // Use a dynamically sized buffer on the heap in case BUFFER_SIZE is large
        std::vector<char> buffer(BUFFER_SIZE);
        while (true) {
            // Read a chunk from the file
            file.read(buffer.data(), buffer.size());
            size_t bytes_read = static_cast<size_t>(file.gcount());

            if (bytes_read > 0) {
                // Update the hash state with the chunk
                if (update_fn(state, buffer.data(), bytes_read) == XXH_ERROR) {
                    throw std::runtime_error("XXHasher: XXH3 update failed for file '" + filename + "'.");
                }
            }

            // Check for end-of-file *after* processing potential last partial read
            if (file.eof()) { break; }

            // Check for other read errors
            if (file.fail()) {
                throw std::system_error(errno, std::generic_category(), "Error reading file '" + filename + "'");
            }
        }

        // Get the final hash digest using the provided digest function
        HashType const hash_result = digest_fn(state);

        // state_guard's destructor will automatically call XXH3_freeState

        return hash_result;
    }

};   // class XXHasher
