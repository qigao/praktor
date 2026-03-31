#pragma once

#include <string>
#include <vector>
#include <map>
#include <regex>

namespace btdsl {

// Output parser utilities
class OutputParser {
public:
  // Parse using regex and extract capture groups
  static bool parseRegex(
    const std::string& input,
    const std::string& pattern,
    std::vector<std::string>& captures
  );

  // Parse JSON and extract value by path (simple implementation)
  static bool parseJson(
    const std::string& input,
    const std::string& path,
    std::string& output
  );

  // Split into lines and filter by pattern
  static std::vector<std::string> parseLines(
    const std::string& input,
    const std::string& filter_pattern = ""
  );

  // Extract key-value pairs (e.g., "key=value" format)
  static std::map<std::string, std::string> parseKeyValue(
    const std::string& input,
    const std::string& delimiter = "=",
    const std::string& line_separator = "\n"
  );

  // Check if output contains a string
  static bool contains(
    const std::string& input,
    const std::string& search
  );

  // Count occurrences of a pattern
  static int countMatches(
    const std::string& input,
    const std::string& pattern
  );
};

} // namespace btdsl
