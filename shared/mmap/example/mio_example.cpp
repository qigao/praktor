#include "file_chunk.hpp"

#include <fstream>
#include <iostream>
#include <vector>

int main() {
    std::filesystem::path const test_file_path = "large_test_file.bin";
    // Make file larger to ensure chunks 5 and 6 exist
    constexpr std::size_t file_size = 300 * 1024 * 1024;          // 300 MiB
    constexpr std::size_t custom_chunk_size = 32 * 1024 * 1024;   // 32 MiB

    // --- Create a dummy file ---
    {
        std::cout << "Creating dummy file: " << test_file_path << " (" << file_size / (1024 * 1024) << " MiB)\n";
        std::ofstream ofs(test_file_path, std::ios::binary | std::ios::out);
        if (!ofs) {
            std::cerr << "Error creating file!" << std::endl;
            return 1;
        }
        // Fill with some pattern - just fill first and last few bytes for simplicity
        ofs.seekp(0);
        char start_pattern[] = "START_";
        ofs.write(start_pattern, sizeof(start_pattern) - 1);
        ofs.seekp(file_size - 10);   // Go near the end
        char end_pattern[] = "_END";
        ofs.write(end_pattern, sizeof(end_pattern) - 1);
        ofs.seekp(file_size - 1);   // Ensure file size
        ofs.write("", 1);
        ofs.close();
        std::cout << "Dummy file created.\n";
    }
    std::cout << "----------------------------\n";

    try {
        constexpr std::size_t max_chunks = 3;
        // --- Create the mapped file object ---
        std::cout << "Mapping file with chunk size " << custom_chunk_size / (1024 * 1024)
                  << " MiB, max mapped chunks: " << max_chunks << "\n";
        fm::FileChunk mapped_file(test_file_path, custom_chunk_size, max_chunks);

        auto const& info = mapped_file.get_file_info();
        std::cout << "File Info:\n";
        std::cout << "  Path: " << info.path << "\n";
        std::cout << "  Size: " << info.size << " bytes (" << info.size / (1024.0 * 1024.0) << " MiB)\n";
        std::cout << "  Is Small File? " << std::boolalpha << mapped_file.is_small_file() << "\n";
        std::cout << "  Total Chunks Defined: " << mapped_file.get_all_chunks().size() << "\n";
        std::cout << "----------------------------\n";

        // Helper lambda to print mapped indices
        auto print_mapped_indices = [&]() {
            std::vector<std::size_t> indices = mapped_file.get_mapped_chunk_indices();
            if (indices.empty()) {
                std::cout << "(None)";
            } else {
                for (size_t i = 0; i < indices.size(); ++i) {
                    std::cout << indices[i] << (i == indices.size() - 1 ? "" : " ");
                }
            }
            std::cout << "\n";
        };

        // --- Access data - triggers mapping (as before) ---
        std::cout << "Accessing data near start (Chunk 0)...\n";
        std::span<std::byte const> span1 = mapped_file.get_span(0, 10);
        std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << " / "
                  << mapped_file.get_all_chunks().size() << "\n";
        std::cout << "  Mapped Indices: ";
        print_mapped_indices();

        std::cout << "Accessing data in Chunk 2 (offset = 2 * chunk_size)...\n";
        std::size_t offset2 = 2 * custom_chunk_size + 5;
        if (offset2 < info.size) {   // Check bounds
            std::span<std::byte const> span2 = mapped_file.get_span(offset2, 10);
        } else {
            std::cout << "  Skipping chunk 2 access (offset too large)\n";
        }
        std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << " / "
                  << mapped_file.get_all_chunks().size() << "\n";
        std::cout << "  Mapped Indices: ";
        print_mapped_indices();

        std::cout << "Accessing data in Chunk 4 (offset = 4 * chunk_size)...\n";
        std::size_t offset4 = 4 * custom_chunk_size + 1;
        if (offset4 < info.size) {   // Check bounds
            std::span<std::byte const> span4 = mapped_file.get_span(offset4, 10);
        } else {
            std::cout << "  Skipping chunk 4 access (offset too large)\n";
        }
        std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << " / "
                  << mapped_file.get_all_chunks().size() << " (Should be max " << max_chunks << ")\n";
        std::cout << "  Mapped Indices: ";
        print_mapped_indices();

        std::cout << "Accessing data near end (Last Chunk)...\n";
        std::size_t offset_end = info.size > 15 ? info.size - 15 : 0;
        std::span<std::byte const> span_end = mapped_file.get_span(offset_end, 10);
        std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << " / "
                  << mapped_file.get_all_chunks().size() << " (Should be max " << max_chunks << ")\n";
        std::cout << "  Mapped Indices: ";
        print_mapped_indices();   // Chunk 0 or 2 should be evicted

        std::cout << "----------------------------\n";
        std::cout << "State before explicit unmapping:\n";
        std::cout << "  Final mapped ratio: " << mapped_file.get_mapped_ratio() * 100.0 << " %\n";
        std::cout << "  Final mapped bytes: " << mapped_file.get_mapped_bytes() << "\n";
        std::cout << "  Mapped Indices: ";
        print_mapped_indices();
        std::cout << "----------------------------\n";

        // --- Test Unmapping ---

        // 1. Test unmapping a chunk that *might* be mapped (depends on LRU)
        std::cout << "Attempting to unmap chunk 4...\n";
        bool unmapped_4 = mapped_file.unmap_chunk(4);
        std::cout << "  Chunk 4 unmapped? " << std::boolalpha << unmapped_4 << "\n";
        std::cout << "  Mapped Chunks after attempt: " << mapped_file.get_mapped_chunk_count() << "\n";
        std::cout << "  Mapped Indices: ";
        print_mapped_indices();

        // 2. Test unmapping chunks that were *never* mapped (assuming they exist)
        std::size_t chunk_index_to_test = 5;   // Example: try chunk 5
        if (chunk_index_to_test < mapped_file.get_all_chunks().size()) {
            std::cout << "Attempting to unmap chunk " << chunk_index_to_test << " (never mapped)...\n";
            bool unmapped_never = mapped_file.unmap_chunk(chunk_index_to_test);
            std::cout << "  Chunk " << chunk_index_to_test << " unmapped? " << std::boolalpha << unmapped_never
                      << " (Expected false)\n";
            std::cout << "  Mapped Chunks after attempt: " << mapped_file.get_mapped_chunk_count() << "\n";
            std::cout << "  Mapped Indices: ";
            print_mapped_indices();
        } else {
            std::cout << "Skipping unmap test for chunk " << chunk_index_to_test << " (index out of bounds)\n";
        }

        // 3. Unmap ALL currently mapped chunks
        std::cout << "Unmapping all remaining chunks...\n";
        mapped_file.unmap_all_chunks();
        std::cout << "  Mapped Chunks after unmap_all: " << mapped_file.get_mapped_chunk_count() << " (Expected 0)\n";
        std::cout << "  Mapped Indices after unmap_all: ";
        print_mapped_indices();

        std::cout << "----------------------------\n";
        std::cout << "// NOTE: Adjusting chunk size dynamically is not supported by this class design.\n";
        std::cout << "//       Chunk layout is fixed at construction based on initial chunk size ("
                  << custom_chunk_size / (1024 * 1024) << " MiB).\n";
        std::cout << "//       Proceeding to access data within original chunks 5 and 6.\n";
        std::cout << "----------------------------\n";

        // 4. Map "original parts" (chunks 5 & 6) by accessing them
        std::size_t offset5 = 5 * custom_chunk_size + 10;   // Calculate offset within original chunk 5
        std::size_t chunk5_idx = 5;
        if (chunk5_idx < mapped_file.get_all_chunks().size() && offset5 < info.size) {
            std::cout << "Accessing data in original Chunk 5 (offset=" << offset5 << ")...\n";
            std::span<std::byte const> span5 = mapped_file.get_span(offset5, 5);
            std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << "\n";
            std::cout << "  Mapped Indices: ";
            print_mapped_indices();
        } else {
            std::cout << "Skipping access to Chunk 5 (offset or index invalid for file size/chunk "
                         "layout).\n";
        }

        std::size_t offset6 = 6 * custom_chunk_size + 20;   // Calculate offset within original chunk 6
        std::size_t chunk6_idx = 6;
        if (chunk6_idx < mapped_file.get_all_chunks().size() && offset6 < info.size) {
            std::cout << "Accessing data in original Chunk 6 (offset=" << offset6 << ")...\n";
            std::span<std::byte const> span6 = mapped_file.get_span(offset6, 5);
            std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << "\n";
            std::cout << "  Mapped Indices: ";
            print_mapped_indices();
        } else {
            std::cout << "Skipping access to Chunk 6 (offset or index invalid for file size/chunk "
                         "layout).\n";
        }

        // Access one more to potentially trigger LRU again if max_chunks >= 3
        std::size_t offset7 = 7 * custom_chunk_size + 1;   // Calculate offset within original chunk 7
        std::size_t chunk7_idx = 7;
        if (chunk7_idx < mapped_file.get_all_chunks().size() && offset7 < info.size) {
            std::cout << "Accessing data in original Chunk 7 (offset=" << offset7 << ")...\n";
            std::span<std::byte const> span7 = mapped_file.get_span(offset7, 5);
            std::cout << "  Mapped Chunks: " << mapped_file.get_mapped_chunk_count() << "\n";
            std::cout << "  Mapped Indices: ";
            print_mapped_indices();   // Should show 5, 6, 7 if max_chunks=3
        } else {
            std::cout << "Skipping access to Chunk 7 (offset or index invalid for file size/chunk "
                         "layout).\n";
        }

    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        // Clean up dummy file in case of error too
        std::filesystem::remove(test_file_path);
        return 1;
    }

    std::cout << "----------------------------\n";
    std::cout << "Cleaning up dummy file.\n";
    std::filesystem::remove(test_file_path);   // Clean up

    return 0;
}
