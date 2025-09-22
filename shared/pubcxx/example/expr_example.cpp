#include "pubcxx/tiny_expr.hpp"

#include <iostream>
#include <string>
#include <tao/pegtl.hpp>   // For tao::pegtl::parse_error

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cout << "Usage: " << argv[0] << " \"<expression>\" ..." << std::endl;
        std::cout << "Example: " << argv[0] << " \"(2 + 3) * 4\" \"100 / (12 - 2)\"" << std::endl;
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        try {
            std::string const expr_str(argv[i]);
            long const result = calculator::calculate(expr_str);
            std::cout << "\"" << expr_str << "\" = " << result << std::endl;
        } catch (tao::pegtl::parse_error const& e) {
            std::cerr << "Error parsing \"" << argv[i] << "\": " << e.what() << std::endl;
        } catch (std::exception const& e) {
            std::cerr << "An unexpected error occurred for \"" << argv[i] << "\": " << e.what() << std::endl;
        }
    }
    return 0;
}
