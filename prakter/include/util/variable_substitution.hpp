#ifndef __VARIABLE_SUBSTITUTION_HPP__
#define __VARIABLE_SUBSTITUTION_HPP__

// Ensure we have the necessary standard library features
#if __cplusplus < 201703L
#error "This code requires C++17 or later"
#endif

// Project includes
#include "dag/workflow_context.hpp"

// Standard library includes
#include <algorithm>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

/**
 * Substitutes variables in a template string with their values from the context.
 * Variables are denoted by ${variable_name} syntax.
 *
 * @param templateStr The string containing variable placeholders
 * @param context The workflow context containing variable values
 * @return The string with variables substituted
 */
inline std::string substituteVariables(std::string const& templateStr, WorkflowContext const& context) {
    std::string result = templateStr;
    bool replacementMade;

    // Keep substituting until no more replacements can be made (handles nested variables)
    do {
        replacementMade = false;
        size_t pos = 0;

        while ((pos = result.find("{{", pos)) != std::string::npos) {
            size_t endPos = result.find("}}", pos);
            if (endPos == std::string::npos) break;

            // Extract variable name
            std::string varName = result.substr(pos + 2, endPos - pos - 2);
            
            // Trim whitespace from variable name
            varName.erase(varName.begin(), std::find_if(varName.begin(), varName.end(), [](unsigned char ch) {
                return !std::isspace(ch);
            }));
            varName.erase(std::find_if(varName.rbegin(), varName.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
            }).base(), varName.end());

            // Get variable value
            std::string varValue = context.getVariable(varName);

            // If not found in context, try environment variable
            if (varValue.empty()) {
                char const* envValue = std::getenv(varName.c_str());
                if (envValue) {
                    varValue = envValue;
                } else {
                    // Always replace with empty string for missing variables
                    varValue = "";
                }
            }

            // Replace the placeholder with the value
            result.replace(pos, endPos - pos + 2, varValue);
            replacementMade = true;

            // Continue searching from the current position
            // (don't advance pos to allow for nested variable resolution)
        }
    } while (replacementMade);

    return result;
}

#endif   // __VARIABLE_SUBSTITUTION_HPP__
