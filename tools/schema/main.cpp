#include "projection.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
std::string read(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read schema source");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: praktor-schema workflow.schema workflow.editor.json grammar.schema.json\n";
        return 2;
    }
    try {
        const auto schema = Praktor::Schema::project(read(argv[1]), WorkflowValue::parse(read(argv[2])));
        const auto encoded = schema.pretty_string();
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
        output << encoded << '\n';
        output.close();
        if (!output) throw std::runtime_error("Cannot write generated schema");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
