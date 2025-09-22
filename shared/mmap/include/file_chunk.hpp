#pragma once

#include "mio.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fm {   // File Mapping namespace

    //-------------------------------------------------------------------------
    // Constants
    //-------------------------------------------------------------------------

    // Default size for chunks if the file is large. 64 KiB example.
    static constexpr std::size_t DEFAULT_CHUNK_SIZE = 64 * 1024;
    // Default limit on the number of chunks actively mapped for large files.
    static constexpr std::size_t DEFAULT_MAX_MAPPED_CHUNKS = 4;

    //-------------------------------------------------------------------------
    // File Metadata Structure
    //-------------------------------------------------------------------------
    struct file_info_t {
        std::filesystem::path path;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type last_write_time;

        // Check if the file seems valid based on gathered info
        [[nodiscard]] bool is_valid() const {
            return !path.empty() && size > 0;   // Basic check
        }
    };

    //-------------------------------------------------------------------------
    // Chunk Information Structure
    //-------------------------------------------------------------------------
    struct chunk_info_t {
        std::size_t index = 0;    // Sequential index of the chunk (0, 1, ...)
        std::size_t offset = 0;   // Starting byte offset in the file
        std::size_t size = 0;     // Size of this chunk in bytes
    };

    //-------------------------------------------------------------------------
    // Main Mapped File Class
    //-------------------------------------------------------------------------

    class FileChunk {
    public:
        //-----------------------------------------------------
        // Construction & Initialization
        //-----------------------------------------------------

        /**
         * @brief Constructs and maps a file.
         *
         * @param file_path Path to the file to map.
         * @param chunk_size_bytes For large files, the size of each mapping chunk.
         *                         If the file size is <= this value, it's mapped entirely.
         * @param max_mapped_chunks_limit For large files, the maximum number of chunks
         *                                allowed to be mapped simultaneously.
         * @throws std::runtime_error If the file doesn't exist, isn't a regular file,
         *                            or if initial mapping fails.
         */
        explicit FileChunk(std::filesystem::path const& file_path, std::size_t chunk_size_bytes = DEFAULT_CHUNK_SIZE,
                           std::size_t max_mapped_chunks_limit = DEFAULT_MAX_MAPPED_CHUNKS) :
            file_path_(file_path),
            chunk_size_(chunk_size_bytes == 0 ? DEFAULT_CHUNK_SIZE : chunk_size_bytes),   // Ensure chunk size > 0
            max_mapped_chunks_(
                max_mapped_chunks_limit == 0 ? 1 : max_mapped_chunks_limit)   // Ensure at least 1 chunk allowed
        {
            std::error_code ec;

            // 1. Validate Path and Get Metadata
            if (!std::filesystem::exists(file_path_, ec) || ec) {
                throw std::runtime_error("File does not exist or error checking existence: " + file_path_.string() +
                                         " (ec: " + ec.message() + ")");
            }
            if (!std::filesystem::is_regular_file(file_path_, ec) || ec) {
                throw std::runtime_error("Path is not a regular file: " + file_path_.string() +
                                         " (ec: " + ec.message() + ")");
            }

            file_info_.path = file_path_;
            file_info_.size = std::filesystem::file_size(file_path_, ec);
            if (ec) {
                throw std::runtime_error("Failed to get file size: " + file_path_.string() + " (ec: " + ec.message() +
                                         ")");
            }
            file_info_.last_write_time = std::filesystem::last_write_time(file_path_, ec);
            if (ec) {
                // Non-fatal, maybe log a warning? For now, we just ignore the error.
                // std::cerr << "Warning: Failed to get last write time: " << file_path_.string() <<
                // " (ec: " << ec.message() << ")" << std::endl;
            }

            if (file_info_.size == 0) {
                // Handle empty file case - no chunks, no mapping needed.
                // Allow construction but most operations will do nothing or return empty.
                is_empty_file_ = true;
                return;
            }

            // 2. Calculate Chunk Information
            calculate_chunks();

            // 3. Initial Mapping (if small file)
            if (is_small_file()) {
                // Map the entire file as the first (and only) chunk
                try {
                    (void)map_chunk_internal(0);   // Lock is acquired inside
                } catch (std::exception const& e) {
                    throw std::runtime_error("Failed to map small file '" + file_path_.string() + "': " + e.what());
                }
            }
            // For large files, chunks are mapped on demand via get_span().
        }

        // Ensure proper cleanup (RAII via mio::mmap_source destructor)
        ~FileChunk() = default;   // Mappings in mapped_chunks_ unmap automatically

        // Non-copyable, non-movable (managing OS resources)

        //-----------------------------------------------------
        // Public Interface
        //-----------------------------------------------------

        /**
         * @brief Gets the file metadata.
         */
        [[nodiscard]] file_info_t const& get_file_info() const noexcept { return file_info_; }

        /**
         * @brief Gets the definition of all chunks (whether mapped or not).
         */
        [[nodiscard]] std::vector<chunk_info_t> const& get_all_chunks() const noexcept { return chunks_; }

        /**
         * @brief Checks if the file was determined to be "small" (<= chunk_size).
         */
        [[nodiscard]] bool is_small_file() const noexcept {
            // An empty file is technically smaller than any chunk size,
            // but handle it explicitly as having 0 chunks defined.
            return !is_empty_file_ && chunks_.size() == 1 && chunks_[0].size <= chunk_size_;
        }

        /**
         * @brief Checks if the file was empty upon opening.
         */
        [[nodiscard]] bool is_empty() const noexcept { return is_empty_file_; }

        /**
         * @brief Gets a view (span) of the data within the mapped file.
         *        For large files, this triggers mapping of the required chunk if
         *        it's not already mapped (and potentially unmapping the LRU chunk).
         *
         * @param offset The starting byte offset within the *entire file*.
         * @param length The number of bytes requested.
         * @return std::span<const std::byte> A view of the requested data.
         * @throws std::out_of_range If the requested offset/length is invalid or
         *                           spans across chunk boundaries (currently unsupported).
         * @throws std::runtime_error If mapping the required chunk fails.
         */
        [[nodiscard]] std::span<std::byte const> get_span(std::size_t offset, std::size_t length) const {
            if (is_empty_file_) {
                if (offset == 0 && length == 0) {
                    // Explicitly return a default-constructed (empty) span
                    return std::span<std::byte const>();
                }
                throw std::out_of_range("Cannot get span from empty file (offset/length must be 0)");
            }
            if (length == 0) {
                // Explicitly return a default-constructed (empty) span
                return std::span<std::byte const>();
            }
            if (offset + length > file_info_.size) {
                throw std::out_of_range("Requested span exceeds file size (offset=" + std::to_string(offset) +
                                        ", length=" + std::to_string(length) +
                                        ", file_size=" + std::to_string(file_info_.size) + ")");
            }

            // Find the chunk containing the start of the requested span
            std::size_t target_chunk_index = get_chunk_index_from_offset(offset);

            if (target_chunk_index >= chunks_.size()) {
                throw std::logic_error("Internal error: Invalid chunk index calculated.");   // Should not happen
            }

            auto const& target_chunk = chunks_[target_chunk_index];

            // Check if the requested span crosses a chunk boundary
            if (offset + length > target_chunk.offset + target_chunk.size) {
                throw std::out_of_range("Requested span crosses chunk boundary (offset=" + std::to_string(offset) +
                                        ", length=" + std::to_string(length) +
                                        ", chunk_end=" + std::to_string(target_chunk.offset + target_chunk.size) + ")");
            }

            // Ensure the required chunk is mapped (acquires lock)
            mio::mmap_source const* mapping = ensure_chunk_mapped(target_chunk_index);

            // Calculate offset within the mapped chunk's data
            std::size_t offset_in_chunk = offset - target_chunk.offset;

            // Get the base pointer (likely const char*)
            char const* base_ptr = mapping->data();   // Assuming mio returns const char*

            // Calculate the start pointer for the span
            char const* span_start_ptr = base_ptr + offset_in_chunk;

            // Return the span as bytes
            return std::span(std::as_bytes(std::span(span_start_ptr, length)));
        }

        //-----------------------------------------------------
        // Progress & Status
        //-----------------------------------------------------

        /**
         * @brief Gets the number of chunks currently held in memory.
         */
        [[nodiscard]] std::size_t get_mapped_chunk_count() const {
            std::lock_guard lock(map_mutex_);
            return mapped_chunks_.size();
        }

        /**
         * @brief Gets the total number of bytes currently mapped into memory.
         */
        [[nodiscard]] std::size_t get_mapped_bytes() const {
            std::lock_guard lock(map_mutex_);
            std::size_t total_bytes = 0;
            for (auto const& pair : mapped_chunks_) {
                // pair.first is chunk_index
                // pair.second is mio::mmap_source
                if (pair.first < chunks_.size()) {   // Sanity check
                    total_bytes += chunks_[pair.first].size;
                }
            }
            return total_bytes;
        }

        /**
         * @brief Gets the ratio of chunks currently mapped (0.0 to 1.0).
         */
        [[nodiscard]] double get_mapped_ratio() const {
            if (is_empty_file_ || chunks_.empty()) {
                return 0.0;   // Or 1.0 if you consider an empty file fully 'handled'? 0.0 seems
                              // more intuitive.
            }
            std::lock_guard lock(map_mutex_);
            return static_cast<double>(mapped_chunks_.size()) / static_cast<double>(chunks_.size());
        }

        /**
         * @brief Gets the indices of the chunks currently mapped.
         */
        [[nodiscard]] std::vector<std::size_t> get_mapped_chunk_indices() const {
            std::lock_guard lock(map_mutex_);
            std::vector<std::size_t> indices;
            indices.reserve(mapped_chunks_.size());
            for (auto const& pair : mapped_chunks_) { indices.push_back(pair.first); }
            std::ranges::sort(indices);   // Optional: return sorted indices
            return indices;
        }

        /**
         * @brief Explicitly ensures a specific chunk is mapped.
         * Primarily for testing or pre-fetching, use get_span for data access.
         *
         * @param chunk_index The index of the chunk to map.
         * @throws std::out_of_range If chunk_index is invalid.
         * @throws std::runtime_error If mapping fails.
         */
        void prefetch_chunk(std::size_t chunk_index) const {
            if (is_empty_file_) return;   // No chunks to prefetch
            if (chunk_index >= chunks_.size()) {
                throw std::out_of_range("Invalid chunk index for prefetching: " + std::to_string(chunk_index));
            }
            (void)ensure_chunk_mapped(chunk_index);   // Handles locking and mapping
        }

        /**
         * @brief Explicitly unmaps a specific chunk if it's currently mapped.
         *
         * @param chunk_index The index of the chunk to unmap.
         * @return true if the chunk was mapped and is now unmapped, false otherwise.
         */
        bool unmap_chunk(std::size_t chunk_index) const {
            if (is_empty_file_) return false;
            std::lock_guard lock(map_mutex_);
            return unmap_chunk_internal(chunk_index);
        }

        /**
         * @brief Unmaps all currently mapped chunks.
         */
        void unmap_all_chunks() const {
            if (is_empty_file_) return;
            std::lock_guard lock(map_mutex_);
            // Make a copy of keys because unmap_chunk_internal modifies the map
            std::vector<std::size_t> indices_to_unmap;
            indices_to_unmap.reserve(mapped_chunks_.size());
            for (auto const& pair : mapped_chunks_) { indices_to_unmap.push_back(pair.first); }
            // Now unmap using the copied indices
            for (std::size_t index : indices_to_unmap) {
                (void)unmap_chunk_internal(index);   // Already locked
            }
            // Or simpler, if RAII unmapping is sufficient:
            // mapped_chunks_.clear();
            // mapped_chunk_lru_.clear();
        }

    private:
        //-----------------------------------------------------
        // Internal Implementation Details
        //-----------------------------------------------------
        std::filesystem::path file_path_;
        file_info_t file_info_;
        std::size_t chunk_size_;
        std::size_t max_mapped_chunks_;
        std::vector<chunk_info_t> chunks_;
        bool is_empty_file_ = false;

        // Mutable members needed for on-demand mapping and LRU
        mutable std::mutex map_mutex_;
        // Map: chunk_index -> mio mapping object
        mutable std::map<std::size_t, mio::mmap_source> mapped_chunks_;
        // Stores chunk indices in order of use (most recent at the back)
        mutable std::vector<std::size_t> mapped_chunk_lru_;

        /**
         * @brief Calculates chunk boundaries based on file size and chunk size.
         * Populates the `chunks_` vector.
         */
        void calculate_chunks() {

            chunks_.clear();

            if (file_info_.size == 0) {
                return;   // No chunks for an empty file
            }

            if (file_info_.size <= chunk_size_) {
                // Small file: one chunk covering the whole file
                chunks_.push_back({0, 0, static_cast<std::size_t>(file_info_.size)});
            } else {
                // Large file: multiple chunks
                std::size_t num_chunks =
                    static_cast<std::size_t>(std::ceil(static_cast<double>(file_info_.size) / chunk_size_));
                chunks_.reserve(num_chunks);
                std::size_t current_offset = 0;
                for (std::size_t i = 0; i < num_chunks; ++i) {
                    std::size_t size_for_this_chunk = chunk_size_;
                    // Check if remaining size is less than a full chunk
                    std::uintmax_t remaining_size = file_info_.size - current_offset;
                    if (remaining_size < chunk_size_) {
                        size_for_this_chunk = static_cast<std::size_t>(remaining_size);
                    }

                    // Handle potential overflow if file_info_.size is huge (close to uintmax_t
                    // limit) and chunk_size is also large. In practice, std::size_t limits are more
                    // Handle potential overflow if file_info_.size is huge
                    if (current_offset + size_for_this_chunk < current_offset) {
                        throw std::overflow_error("Chunk offset calculation resulted in overflow.");
                    }

                    chunks_.push_back({i, current_offset, size_for_this_chunk});
                    current_offset += size_for_this_chunk;
                }

                // Sanity check
                if (current_offset != file_info_.size) {
                    throw std::logic_error("Internal error: Chunk calculation did not cover the entire file size.");
                }
            }
        }

        /**
         * @brief Finds the index of the chunk that contains the given file offset.
         */
        [[nodiscard]] std::size_t get_chunk_index_from_offset(std::size_t offset) const {
            if (is_empty_file_ || chunks_.empty()) {
                throw std::out_of_range("Cannot get chunk index for empty or unchunked file.");
            }
            // For small files, it's always chunk 0
            if (is_small_file()) { return 0; }
            // For large files, calculate based on chunk size (integer division)
            // Note: offset should be < file_info_.size, already checked by caller (get_span)
            std::size_t index = offset / chunk_size_;
            // Handle edge case where offset is exactly the start of the last chunk
            // or if calculation somehow yields an index >= size() due to rounding (unlikely with
            // ceil earlier)
            if (index >= chunks_.size()) {
                index = chunks_.size() - 1;   // Clamp to the last valid index
            }
            // Verify the offset is indeed within the calculated chunk's range
            // Disabled for performance, rely on calculation correctness
            // if (offset < chunks_[index].offset || offset >= chunks_[index].offset +
            // chunks_[index].size) {
            //    throw std::logic_error("Internal error: Offset does not fall within calculated
            //    chunk.");
            //}
            return index;
        }

        /**
         * @brief Core logic to map a chunk. Assumes lock is already held.
         *
         * @param chunk_index Index of the chunk to map.
         * @return Pointer to the newly created mio::mmap_source object in the map.
         * @throws std::runtime_error on mio mapping error.
         */
        mio::mmap_source* map_chunk_internal(std::size_t chunk_index) const {
            // Assumes: Lock is held, chunk_index is valid.

            // 1. Check if mapping limit reached, evict LRU if necessary
            if (mapped_chunks_.size() >= max_mapped_chunks_) {
                evict_lru_chunk_internal();   // Assumes lock is held
            }

            // 2. Perform the mapping using mio
            auto const& chunk = chunks_[chunk_index];
            std::error_code ec;
            mio::mmap_source mmap;
            // Use mio::map_options for potential platform specific flags if needed later
            mmap.map(file_path_.string(), chunk.offset, chunk.size, ec);

            if (ec) {
                throw std::runtime_error("mio::mmap_source::map failed for chunk " + std::to_string(chunk_index) +
                                         " (offset=" + std::to_string(chunk.offset) +
                                         ", size=" + std::to_string(chunk.size) + ")" +
                                         " - ec: " + std::to_string(ec.value()) + ": " + ec.message());
            }

            // 3. Store the mapping and update LRU
            auto [it, success] = mapped_chunks_.emplace(chunk_index, std::move(mmap));
            if (!success) {
                // Should not happen if eviction worked correctly, but handle defensively
                throw std::logic_error("Internal error: Failed to insert new mapping after potential eviction.");
            }
            mapped_chunk_lru_.push_back(chunk_index);   // Add to the end (most recent)

            return &it->second;   // Return pointer to the mapping object in the map
        }

        /**
         * @brief Unmaps a specific chunk. Assumes lock is already held.
         *
         * @param chunk_index The index of the chunk to unmap.
         * @return true if the chunk was found and removed, false otherwise.
         */
        bool unmap_chunk_internal(std::size_t chunk_index) const {
            // Assumes: Lock is held.
            auto map_it = mapped_chunks_.find(chunk_index);
            if (map_it != mapped_chunks_.end()) {
                // Erase from map (mio::mmap_source destructor handles OS unmap)
                mapped_chunks_.erase(map_it);

                // Erase from LRU list
                auto lru_it = std::ranges::find(mapped_chunk_lru_, chunk_index);
                if (lru_it != mapped_chunk_lru_.end()) { mapped_chunk_lru_.erase(lru_it); }
                return true;
            }
            return false;
        }

        /**
         * @brief Evicts the least recently used chunk. Assumes lock is already held.
         */
        void evict_lru_chunk_internal() const {
            // Assumes: Lock is held.
            if (!mapped_chunk_lru_.empty() && !mapped_chunks_.empty()) {
                std::size_t lru_index = mapped_chunk_lru_.front();   // Oldest is at the front
                bool removed = unmap_chunk_internal(lru_index);      // Use internal unmap function (already locked)
                // This also removes from LRU list internally
                if (!removed) {
                    // This indicates an inconsistency between LRU list and map
                    throw std::logic_error("Internal error: LRU chunk index not found in mapped "
                                           "chunks during eviction.");
                }
            }
            // else: Nothing to evict
        }

        /**
         * @brief Ensures the chunk is mapped, handling locking and LRU.
         *
         * @param chunk_index Index of the chunk.
         * @return Pointer to the mio::mmap_source object (guaranteed non-null).
         * @throws std::runtime_error If mapping fails.
         */
        mio::mmap_source const* ensure_chunk_mapped(std::size_t chunk_index) const {
            std::lock_guard lock(map_mutex_);

            auto it = mapped_chunks_.find(chunk_index);
            if (it != mapped_chunks_.end()) {
                // Chunk already mapped, update LRU status
                update_lru(chunk_index);
                return &it->second;
            } else {
                // Chunk not mapped, map it (handles eviction if needed)
                auto* mapping = map_chunk_internal(chunk_index);   // Returns pointer to new mapping
                return mapping;
            }
        }

        /**
         * @brief Updates the LRU list when a chunk is accessed. Assumes lock is held.
         * Moves the accessed chunk index to the back (most recently used).
         *
         * @param accessed_chunk_index The index of the chunk that was just accessed.
         */
        void update_lru(std::size_t accessed_chunk_index) const {
            // Assumes: Lock is held.
            auto const it = std::ranges::find(mapped_chunk_lru_, accessed_chunk_index);
            if (it != mapped_chunk_lru_.end()) {
                // Found it, move it to the end (most recent)
                // Avoid moving if it's already the last element
                if (it != std::prev(mapped_chunk_lru_.end())) {
                    // Erase from current position and push to back
                    mapped_chunk_lru_.erase(it);
                    mapped_chunk_lru_.push_back(accessed_chunk_index);
                }
            }
            // else: Should not happen if called after finding the chunk in mapped_chunks_
            // If it does, it indicates an inconsistency.
            else {
                // Maybe log an error or throw? For now, let's assume consistency.
                // Could potentially happen if unmap_chunk_internal fails to remove from LRU?
                // Re-adding it might be a defensive measure:
                // mapped_chunk_lru_.push_back(accessed_chunk_index);
                throw std::logic_error("Internal error: Accessed chunk index not found in LRU list.");
            }
        }
    };

}   // namespace fm
