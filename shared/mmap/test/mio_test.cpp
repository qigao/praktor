
#include "catch2/catch_all.hpp"
#include "file_chunk.hpp"   // Include the header for the class under test

#include <chrono>
#include <cstdint>   // For uint8_t
#include <filesystem>
#include <fstream>
#include <iostream>   // Include for std::cerr
#include <numeric>    // For std::iota (though we replaced its usage)
#include <random>
#include <set>   // For checking mapped indices
#include <string>
#include <system_error>
#include <thread>   // For sleep_for
#include <vector>

namespace fs = std::filesystem;
using namespace fm;   // Use the namespace for FileChunk
using namespace std::chrono_literals;
using Catch::Approx;   // Use Catch::Approx for floating point comparisons

// --- Constants for Tests ---
constexpr std::size_t KB = 1024;
constexpr std::size_t MB = 1024 * KB;

// --- Helper Functions (Reused/Adapted from chunk_file_test.cpp) ---

fs::path create_temp_dir(std::string const& base_name = "mio_file_test") {
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1, 1000000);
    fs::path temp_dir =
        fs::temp_directory_path() / (base_name + "_" + std::to_string(timestamp) + "_" + std::to_string(distrib(gen)));
    int retries = 5;
    while (fs::exists(temp_dir) && retries-- > 0) {
        timestamp++;
        temp_dir = fs::temp_directory_path() /
                   (base_name + "_" + std::to_string(timestamp) + "_" + std::to_string(distrib(gen)));
    }
    if (fs::exists(temp_dir)) {
        throw std::runtime_error("Failed to create unique temporary directory after multiple retries: " +
                                 temp_dir.string());
    }
    fs::create_directories(temp_dir);
    return temp_dir;
}

// Generates simple, predictable data (0, 1, 2, ...) for easier verification
// Fixed version without std::iota for std::byte
std::vector<std::byte> generate_sequential_data(size_t size) {
    std::vector<std::byte> data(size);
    for (size_t i = 0; i < size; ++i) {
        // Cast the integer counter (modulo 256) to std::byte
        data[i] = static_cast<std::byte>(static_cast<uint8_t>(i % 256));
    }
    return data;
}

void create_test_file(fs::path const& filepath, std::vector<std::byte> const& data) {
    fs::create_directories(filepath.parent_path());
    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file) { throw std::runtime_error("Failed to create test file: " + filepath.string()); }
    if (!data.empty()) {
        // Need to cast std::byte* to char* for ofstream::write
        file.write(reinterpret_cast<char const*>(data.data()), data.size());
        if (!file) { throw std::runtime_error("Failed to write to test file: " + filepath.string()); }
    }
    file.close();
    if (file.fail()) { throw std::runtime_error("Failed to close test file: " + filepath.string()); }
}

// --- Test Fixture for FileChunk ---
struct MioFileFixture {
    fs::path test_dir;

    MioFileFixture(std::string const& name = "mio_file_test") {
        test_dir = create_temp_dir(name);
        // Initialize logger (if not already done elsewhere)
        // This ensures logs from mio_file are captured during tests
    }

    ~MioFileFixture() {
        std::error_code ec;
        fs::remove_all(test_dir, ec);
        if (ec) {
            // Use std::cerr for errors in destructor during testing
            std::cerr << "Error during test cleanup (" << test_dir.string() << "): " << ec.message() << std::endl;
        }
    }

    // Helper to create a test file with sequential data
    fs::path createTestFile(std::string const& filename, size_t size) {
        fs::path file_path = test_dir / filename;
        auto data = generate_sequential_data(size);
        create_test_file(file_path, data);
        return file_path;
    }
};

// --- Test Cases ---

TEST_CASE_METHOD(MioFileFixture, "FileChunk Constructor", "[mio_file][constructor]") {

    SECTION("Construct with existing file (small)") {
        size_t file_size = 1 * KB;
        fs::path test_file = createTestFile("small.bin", file_size);
        REQUIRE_NOTHROW(FileChunk(test_file));
        FileChunk FileChunk(test_file);
        REQUIRE(FileChunk.get_file_info().size == file_size);
        REQUIRE(FileChunk.is_small_file());
        REQUIRE_FALSE(FileChunk.is_empty());
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);   // Small files mapped initially
    }

    SECTION("Construct with existing file (large)") {
        size_t file_size = DEFAULT_CHUNK_SIZE + 1 * KB;   // Ensure it's larger than default chunk
        fs::path test_file = createTestFile("large.bin", file_size);
        REQUIRE_NOTHROW(FileChunk(test_file));
        FileChunk FileChunk(test_file);
        REQUIRE(FileChunk.get_file_info().size == file_size);
        REQUIRE_FALSE(FileChunk.is_small_file());
        REQUIRE_FALSE(FileChunk.is_empty());
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);   // Large files not mapped initially
        REQUIRE(FileChunk.get_all_chunks().size() > 1);
    }

    SECTION("Construct with empty file") {
        fs::path test_file = createTestFile("empty.bin", 0);
        REQUIRE_NOTHROW(FileChunk(test_file));
        FileChunk FileChunk(test_file);
        REQUIRE(FileChunk.get_file_info().size == 0);
        REQUIRE_FALSE(FileChunk.is_small_file());   // Empty is not considered "small" in this context
        REQUIRE(FileChunk.is_empty());
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
        REQUIRE(FileChunk.get_all_chunks().empty());
    }

    SECTION("Construct with non-existent file") {
        fs::path test_file = test_dir / "non_existent.bin";
        REQUIRE_THROWS_AS(FileChunk(test_file), std::runtime_error);
    }

    SECTION("Construct with directory path") {
        fs::path dir_path = test_dir / "a_directory";
        fs::create_directory(dir_path);
        REQUIRE_THROWS_AS(FileChunk(dir_path), std::runtime_error);
    }

    SECTION("Constructor defaults chunk size/limit if 0") {
        size_t file_size = DEFAULT_CHUNK_SIZE * 2;
        fs::path test_file = createTestFile("defaults.bin", file_size);
        // Pass 0 for chunk size and limit
        FileChunk FileChunk(test_file, 0, 0);
        // Verify internal chunk size is the default and limit is 1
        REQUIRE(FileChunk.get_all_chunks().size() == 2);   // Implies chunk size was defaulted correctly
        // Test LRU limit of 1 later in get_span tests
    }
}

TEST_CASE_METHOD(MioFileFixture, "FileChunk get_span (Small File)", "[mio_file][get_span][small]") {
    size_t file_size = 1 * KB;
    fs::path test_file = createTestFile("small_span.bin", file_size);
    FileChunk FileChunk(test_file);

    REQUIRE(FileChunk.is_small_file());
    REQUIRE(FileChunk.get_mapped_chunk_count() == 1);

    SECTION("Get entire span") {
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(0, file_size));
        REQUIRE(data_span.size() == file_size);
        // Verify content (first byte should be 0, second 1, etc.)
        REQUIRE(data_span[0] == std::byte{0});
        REQUIRE(data_span[10] == std::byte{10});
        // Use modulo for verification as data wraps at 256
        REQUIRE(data_span[file_size - 1] == static_cast<std::byte>(static_cast<uint8_t>((file_size - 1) % 256)));
    }

    SECTION("Get partial span (start)") {
        size_t length = 100;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(0, length));
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] == std::byte{0});
        REQUIRE(data_span[length - 1] == static_cast<std::byte>(static_cast<uint8_t>((length - 1) % 256)));
    }

    SECTION("Get partial span (middle)") {
        size_t offset = 500;
        size_t length = 200;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(offset, length));
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] == static_cast<std::byte>(static_cast<uint8_t>(offset % 256)));
        REQUIRE(data_span[length - 1] == static_cast<std::byte>(static_cast<uint8_t>((offset + length - 1) % 256)));
    }

    SECTION("Get partial span (end)") {
        size_t length = 50;
        size_t offset = file_size - length;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(offset, length));
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] == static_cast<std::byte>(static_cast<uint8_t>(offset % 256)));
        REQUIRE(data_span[length - 1] == static_cast<std::byte>(static_cast<uint8_t>((file_size - 1) % 256)));
    }

    SECTION("Get zero-length span") {
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(100, 0));
        REQUIRE(data_span.empty());
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(0, 0));
        REQUIRE(data_span.empty());
    }

    SECTION("Out of bounds offset") {
        REQUIRE_THROWS_AS(FileChunk.get_span(file_size, 1), std::out_of_range);
        REQUIRE_THROWS_AS(FileChunk.get_span(file_size + 1, 1), std::out_of_range);
    }

    SECTION("Out of bounds length") {
        REQUIRE_THROWS_AS(FileChunk.get_span(0, file_size + 1), std::out_of_range);
        REQUIRE_THROWS_AS(FileChunk.get_span(100, file_size), std::out_of_range);
        REQUIRE_THROWS_AS(FileChunk.get_span(file_size - 1, 2), std::out_of_range);
    }
}

TEST_CASE_METHOD(MioFileFixture, "FileChunk get_span (Large File)", "[mio_file][get_span][large]") {
    // Use a smaller chunk size for easier testing of LRU
    size_t const test_chunk_size = 1 * KB;
    size_t const num_chunks = 10;
    size_t const file_size = test_chunk_size * num_chunks;
    // Set LRU limit low to force eviction
    size_t const lru_limit = 3;

    fs::path test_file = createTestFile("large_span.bin", file_size);
    FileChunk FileChunk(test_file, test_chunk_size, lru_limit);

    REQUIRE_FALSE(FileChunk.is_small_file());
    REQUIRE(FileChunk.get_all_chunks().size() == num_chunks);
    REQUIRE(FileChunk.get_mapped_chunk_count() == 0);

    SECTION("Get span within first chunk") {
        size_t offset = 100;
        size_t length = 50;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(offset, length));
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] == static_cast<std::byte>(static_cast<uint8_t>(offset % 256)));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);
        REQUIRE(FileChunk.get_mapped_chunk_indices()[0] == 0);
    }

    SECTION("Get span within later chunk (trigger map)") {
        size_t chunk_idx = 5;
        size_t offset = chunk_idx * test_chunk_size + 200;
        size_t length = 100;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(offset, length));
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] ==
                static_cast<std::byte>(static_cast<uint8_t>(offset % 256)));   // Check content based on sequential data
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);
        REQUIRE(FileChunk.get_mapped_chunk_indices()[0] == chunk_idx);
    }

    SECTION("Get multiple spans within limit (LRU order)") {
        // Access 0, 2, 1
        REQUIRE_NOTHROW(FileChunk.get_span(10, 1));   // Maps 0
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0});

        REQUIRE_NOTHROW(FileChunk.get_span(2 * test_chunk_size + 10, 1));   // Maps 2
        REQUIRE(FileChunk.get_mapped_chunk_count() == 2);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0, 2});

        REQUIRE_NOTHROW(FileChunk.get_span(1 * test_chunk_size + 10, 1));   // Maps 1
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        REQUIRE(FileChunk.get_mapped_chunk_indices() ==
                std::vector<size_t>{0, 1, 2});   // Order depends on map iteration, check count

        // Access 0 again, should move 0 to end of LRU, indices remain 0, 1, 2
        REQUIRE_NOTHROW(FileChunk.get_span(20, 1));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0, 1, 2});
    }

    SECTION("Get span forcing LRU eviction") {
        // Map 0, 1, 2 (limit is 3)
        REQUIRE_NOTHROW(FileChunk.get_span(0 * test_chunk_size + 1, 1));
        REQUIRE_NOTHROW(FileChunk.get_span(1 * test_chunk_size + 1, 1));
        REQUIRE_NOTHROW(FileChunk.get_span(2 * test_chunk_size + 1, 1));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0, 1, 2});

        // Access chunk 3, should evict chunk 0 (LRU)
        REQUIRE_NOTHROW(FileChunk.get_span(3 * test_chunk_size + 1, 1));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        // Check that 0 is gone and 3 is present
        auto indices = FileChunk.get_mapped_chunk_indices();   // Already sorted
        REQUIRE(indices == std::vector<size_t>{1, 2, 3});

        // Access chunk 4, should evict chunk 1
        REQUIRE_NOTHROW(FileChunk.get_span(4 * test_chunk_size + 1, 1));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        indices = FileChunk.get_mapped_chunk_indices();
        REQUIRE(indices == std::vector<size_t>{2, 3, 4});

        // Access chunk 2 again, should evict nothing but reorder LRU
        REQUIRE_NOTHROW(FileChunk.get_span(2 * test_chunk_size + 5, 1));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        indices = FileChunk.get_mapped_chunk_indices();
        REQUIRE(indices == std::vector<size_t>{2, 3, 4});
    }

    SECTION("Get span exactly at chunk boundary (end of one chunk)") {
        size_t offset = test_chunk_size - 1;
        size_t length = 1;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(offset, length));   // Should be in chunk 0
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] == static_cast<std::byte>(static_cast<uint8_t>(offset % 256)));
        REQUIRE(FileChunk.get_mapped_chunk_indices()[0] == 0);
    }

    SECTION("Get span exactly at chunk boundary (start of next chunk)") {
        size_t offset = test_chunk_size;   // Start of chunk 1
        size_t length = 1;
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(offset, length));   // Should be in chunk 1
        REQUIRE(data_span.size() == length);
        REQUIRE(data_span[0] == static_cast<std::byte>(static_cast<uint8_t>(offset % 256)));
        REQUIRE(FileChunk.get_mapped_chunk_indices()[0] == 1);
    }

    SECTION("Get span crossing chunk boundary (throws)") {
        size_t offset = test_chunk_size - 10;   // Near end of chunk 0
        size_t length = 20;                     // Crosses into chunk 1
        REQUIRE_THROWS_AS(FileChunk.get_span(offset, length), std::out_of_range);
    }

    SECTION("Get zero-length span") {
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(test_chunk_size + 100, 0));
        REQUIRE(data_span.empty());
        // Ensure it doesn't unnecessarily map a chunk for a zero-length request
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
    }

    SECTION("Out of bounds offset/length") {
        REQUIRE_THROWS_AS(FileChunk.get_span(file_size, 1), std::out_of_range);
        REQUIRE_THROWS_AS(FileChunk.get_span(0, file_size + 1), std::out_of_range);
        REQUIRE_THROWS_AS(FileChunk.get_span(file_size - 1, 2), std::out_of_range);
    }
}

TEST_CASE_METHOD(MioFileFixture, "FileChunk get_span (Empty File)", "[mio_file][get_span][empty]") {
    fs::path test_file = createTestFile("empty_span.bin", 0);
    FileChunk FileChunk(test_file);

    REQUIRE(FileChunk.is_empty());

    SECTION("Get zero-length span at offset 0") {
        std::span<std::byte const> data_span;
        REQUIRE_NOTHROW(data_span = FileChunk.get_span(0, 0));
        REQUIRE(data_span.empty());
    }

    SECTION("Get non-zero length span") { REQUIRE_THROWS_AS(FileChunk.get_span(0, 1), std::out_of_range); }

    SECTION("Get span at non-zero offset") {
        REQUIRE_THROWS_AS(FileChunk.get_span(1, 0), std::out_of_range);
        REQUIRE_THROWS_AS(FileChunk.get_span(1, 1), std::out_of_range);
    }
}

TEST_CASE_METHOD(MioFileFixture, "FileChunk Control Methods", "[mio_file][control]") {
    size_t const test_chunk_size = 1 * KB;
    size_t const num_chunks = 5;
    size_t const file_size = test_chunk_size * num_chunks;
    size_t const lru_limit = 3;

    fs::path test_file = createTestFile("control.bin", file_size);
    FileChunk FileChunk(test_file, test_chunk_size, lru_limit);

    REQUIRE(FileChunk.get_mapped_chunk_count() == 0);

    SECTION("Prefetch chunk") {
        REQUIRE_NOTHROW(FileChunk.prefetch_chunk(2));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);
        REQUIRE(FileChunk.get_mapped_chunk_indices()[0] == 2);

        REQUIRE_NOTHROW(FileChunk.prefetch_chunk(0));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 2);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0, 2});

        // Prefetching already mapped chunk should be okay
        REQUIRE_NOTHROW(FileChunk.prefetch_chunk(2));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 2);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0, 2});

        // Prefetch out of bounds
        REQUIRE_THROWS_AS(FileChunk.prefetch_chunk(num_chunks), std::out_of_range);
    }

    SECTION("Unmap chunk") {
        // Map a few chunks first
        FileChunk.prefetch_chunk(1);
        FileChunk.prefetch_chunk(3);
        FileChunk.prefetch_chunk(4);
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{1, 3, 4});

        // Unmap an existing chunk
        REQUIRE(FileChunk.unmap_chunk(3));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 2);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{1, 4});

        // Unmap another existing chunk
        REQUIRE(FileChunk.unmap_chunk(1));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);
        REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{4});

        // Unmap non-existent chunk
        REQUIRE_FALSE(FileChunk.unmap_chunk(0));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 1);

        // Unmap last chunk
        REQUIRE(FileChunk.unmap_chunk(4));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
        REQUIRE(FileChunk.get_mapped_chunk_indices().empty());

        // Unmap when already empty
        REQUIRE_FALSE(FileChunk.unmap_chunk(0));
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
    }

    SECTION("Unmap all chunks") {
        // Map a few chunks first
        FileChunk.prefetch_chunk(0);
        FileChunk.prefetch_chunk(2);
        FileChunk.prefetch_chunk(4);
        REQUIRE(FileChunk.get_mapped_chunk_count() == 3);

        REQUIRE_NOTHROW(FileChunk.unmap_all_chunks());
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
        REQUIRE(FileChunk.get_mapped_chunk_indices().empty());

        // Call again when already empty
        REQUIRE_NOTHROW(FileChunk.unmap_all_chunks());
        REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
    }
}

TEST_CASE_METHOD(MioFileFixture, "FileChunk Status Methods", "[mio_file][status]") {
    size_t const test_chunk_size = 1 * KB;
    size_t const num_chunks = 8;
    size_t const file_size = test_chunk_size * num_chunks;
    size_t const lru_limit = 4;

    fs::path test_file = createTestFile("status.bin", file_size);
    FileChunk FileChunk(test_file, test_chunk_size, lru_limit);

    REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
    REQUIRE(FileChunk.get_mapped_bytes() == 0);
    REQUIRE(FileChunk.get_mapped_ratio() == Approx(0.0));
    REQUIRE(FileChunk.get_mapped_chunk_indices().empty());

    // Map chunk 1
    FileChunk.get_span(1 * test_chunk_size, 1);
    REQUIRE(FileChunk.get_mapped_chunk_count() == 1);
    REQUIRE(FileChunk.get_mapped_bytes() == test_chunk_size);
    REQUIRE(FileChunk.get_mapped_ratio() == Approx(1.0 / num_chunks));
    REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{1});

    // Map chunk 3
    FileChunk.get_span(3 * test_chunk_size, 1);
    REQUIRE(FileChunk.get_mapped_chunk_count() == 2);
    REQUIRE(FileChunk.get_mapped_bytes() == 2 * test_chunk_size);
    REQUIRE(FileChunk.get_mapped_ratio() == Approx(2.0 / num_chunks));
    REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{1, 3});

    // Map chunk 5 and 7 (reaching limit)
    FileChunk.get_span(5 * test_chunk_size, 1);
    FileChunk.get_span(7 * test_chunk_size, 1);
    REQUIRE(FileChunk.get_mapped_chunk_count() == 4);
    REQUIRE(FileChunk.get_mapped_bytes() == 4 * test_chunk_size);
    REQUIRE(FileChunk.get_mapped_ratio() == Approx(4.0 / num_chunks));
    REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{1, 3, 5, 7});

    // Map chunk 0 (evicts chunk 1)
    FileChunk.get_span(0 * test_chunk_size, 1);
    REQUIRE(FileChunk.get_mapped_chunk_count() == 4);
    REQUIRE(FileChunk.get_mapped_bytes() == 4 * test_chunk_size);
    REQUIRE(FileChunk.get_mapped_ratio() == Approx(4.0 / num_chunks));
    REQUIRE(FileChunk.get_mapped_chunk_indices() == std::vector<size_t>{0, 3, 5, 7});

    // Unmap all
    FileChunk.unmap_all_chunks();
    REQUIRE(FileChunk.get_mapped_chunk_count() == 0);
    REQUIRE(FileChunk.get_mapped_bytes() == 0);
    REQUIRE(FileChunk.get_mapped_ratio() == Approx(0.0));
    REQUIRE(FileChunk.get_mapped_chunk_indices().empty());
}
