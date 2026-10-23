#include "actions/output_parser.hpp"
#include <tinytest.hpp>
#include <stdexcept>

spec("Output parser compatibility bridge") {
    it("preserves binary spans, CR and invalid regex fallback") {
        const std::string binary("a\0b\n\nlast\r\n", 11u);
        const std::vector<std::string> expected{std::string("a\0b", 3u), "", "last\r"};
        check(actions::OutputParser::parseLines(binary) == expected);
        check(actions::OutputParser::parseLines(binary, "[") == expected);
        check(actions::OutputParser::parseLines(binary, "^last") == std::vector<std::string>{"last\r"});
        check(actions::OutputParser::contains(binary, std::string("\0b", 2u)));
    }
    it("preserves binary key/value containers and rejects an empty separator") {
        const std::string binary(" k\0x = v\0y \n", 12u);
        const auto parsed = actions::OutputParser::parseKeyValue(binary);
        check(parsed.size() == 1u);
        check(parsed.at(std::string("k\0x", 3u)) == std::string("v\0y", 3u));
        bool rejected = false;
        try { actions::OutputParser::parseKeyValue("k=v", "=", ""); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        rejected = false;
        try { actions::OutputParser::parseKeyValue("", "=", ""); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
    }
}
