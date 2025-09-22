#include "pubcxx/simple_xxhash.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Helper to create a dummy file for testing
void create_dummy_file(std::string const& filename, size_t size) {
    std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::cerr << "Error: Could not create dummy file: " << filename << std::endl;
        return;
    }
    std::vector<char> data(1024, 'A');   // Fill with 'A'
    for (size_t i = 0; i < size / data.size(); ++i) { ofs.write(data.data(), data.size()); }
    ofs.write(data.data(), size % data.size());   // Write remaining part
    std::cout << "Created dummy file: " << filename << " (" << size << " bytes)" << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> filenames;

    if (argc > 1) {
        for (int i = 1; i < argc; ++i) { filenames.push_back(argv[i]); }
    } else {
        // Create some dummy files if no args provided
        std::cout << "No filenames provided. Creating dummy files for testing." << std::endl;
        create_dummy_file("dummy1.bin", 1024 * 1024);              // 1 MB
        create_dummy_file("dummy2.bin", 10 * 1024 * 1024 + 123);   // ~10 MB + odd bytes
        create_dummy_file("dummy_empty.bin", 0);                   // Empty file
        filenames = {"dummy1.bin", "dummy2.bin", "dummy_empty.bin", "nonexistent_file.xyz"};
    }

    for (auto const& filename : filenames) {
        std::cout << "--- Processing: " << filename << " ---" << std::endl;
        try {
            std::string hash64 = XXHash::xxh3_64_sum_file_str(filename);
            std::cout << "XXH3-64:  " << hash64 << std::endl;

            std::string hash128 = XXHash::xxh3_128_str(filename);
            std::cout << "XXH3-128: " << hash128 << std::endl;

        } catch (std::system_error const& e) {
            // Catch specific system errors (like file not found, permission denied)
            std::cerr << "System Error: " << e.what() << " (code: " << e.code() << ")" << std::endl;
        } catch (std::runtime_error const& e) {
            // Catch other runtime errors from the hasher or xxHash library
            std::cerr << "Runtime Error: " << e.what() << std::endl;
        } catch (std::exception const& e) {
            // Catch any other standard exceptions
            std::cerr << "Standard Exception: " << e.what() << std::endl;
        }
        std::cout << std::endl;
        std::filesystem::remove(filename);
    }

    return 0;
}
