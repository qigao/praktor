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

/**
 * Evaluates a simple condition string with variable substitution.
 * Supports basic comparison operators: ==, !=, >, <, >=, <=
 * And logical operators: && (AND), || (OR)
 *
 * @param conditionStr The condition string to evaluate
 * @param context The workflow context containing variable values
 * @return true if the condition evaluates to true, false otherwise
 */
inline bool evaluateCondition(std::string const& conditionStr, WorkflowContext const& context) {
    // First substitute variables
    std::string substituted = substituteVariables(conditionStr, context);

    // Debug output to stderr
    fprintf(stderr, "DEBUG: Original condition: '%s'\n", conditionStr.c_str());
    fprintf(stderr, "DEBUG: After substitution: '%s'\n", substituted.c_str());

    // Split by logical operators
    std::vector<std::string> andConditions;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < substituted.length(); i++) {
        if (substituted[i] == '"' || substituted[i] == '\'') {
            inQuotes = !inQuotes;
            current += substituted[i];
        } else if (!inQuotes && i < substituted.length() - 1 && substituted[i] == '&' && substituted[i + 1] == '&') {
            andConditions.push_back(current);
            current.clear();
            i++;   // Skip the second &
        } else {
            current += substituted[i];
        }
    }

    if (!current.empty()) { andConditions.push_back(current); }

    fprintf(stderr, "DEBUG: Number of AND conditions: %zu\n", andConditions.size());
    for (size_t i = 0; i < andConditions.size(); i++) {
        fprintf(stderr, "DEBUG: AND condition %zu: '%s'\n", i, andConditions[i].c_str());
    }

    // Process AND conditions
    for (auto const& andCond : andConditions) {
        std::vector<std::string> orConditions;
        std::string orCurrent;
        inQuotes = false;

        for (size_t i = 0; i < andCond.length(); i++) {
            if (andCond[i] == '"' || andCond[i] == '\'') {
                inQuotes = !inQuotes;
                orCurrent += andCond[i];
            } else if (!inQuotes && i < andCond.length() - 1 && andCond[i] == '|' && andCond[i + 1] == '|') {
                orConditions.push_back(orCurrent);
                orCurrent.clear();
                i++;   // Skip the second |
            } else {
                orCurrent += andCond[i];
            }
        }

        if (!orCurrent.empty()) { orConditions.push_back(orCurrent); }

        fprintf(stderr, "DEBUG: Number of OR conditions: %zu\n", orConditions.size());
        for (size_t i = 0; i < orConditions.size(); i++) {
            fprintf(stderr, "DEBUG: OR condition %zu: '%s'\n", i, orConditions[i].c_str());
        }

        // Evaluate OR conditions
        bool orResult = false;
        for (auto const& orCond : orConditions) {
            std::string trimmed = orCond;
            // Trim whitespace
            if (!trimmed.empty()) {
                size_t start = trimmed.find_first_not_of(" \t\n\r");
                if (start != std::string::npos) {
                    trimmed = trimmed.substr(start);
                } else {
                    trimmed = "";
                }
            }
            if (!trimmed.empty()) {
                size_t end = trimmed.find_last_not_of(" \t\n\r");
                if (end != std::string::npos) { trimmed = trimmed.substr(0, end + 1); }
            }

            fprintf(stderr, "DEBUG: Trimmed condition: '%s'\n", trimmed.c_str());

            // Find comparison operator
            bool condResult = false;
            if (trimmed.find("==") != std::string::npos) {
                size_t pos = trimmed.find("==");
                std::string left = trimmed.substr(0, pos);
                std::string right = trimmed.substr(pos + 2);

                // Trim both sides
                if (!left.empty()) {
                    size_t start = left.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        left = left.substr(start);
                    } else {
                        left = "";
                    }
                }
                if (!left.empty()) {
                    size_t end = left.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { left = left.substr(0, end + 1); }
                }

                if (!right.empty()) {
                    size_t start = right.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        right = right.substr(start);
                    } else {
                        right = "";
                    }
                }
                if (!right.empty()) {
                    size_t end = right.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { right = right.substr(0, end + 1); }
                }

                fprintf(stderr, "DEBUG: == comparison: left='%s', right='%s'\n", left.c_str(), right.c_str());
                condResult = (left == right);
                fprintf(stderr, "DEBUG: == result: %s\n", condResult ? "true" : "false");
            } else if (trimmed.find("!=") != std::string::npos) {
                size_t pos = trimmed.find("!=");
                std::string left = trimmed.substr(0, pos);
                std::string right = trimmed.substr(pos + 2);

                // Trim both sides
                if (!left.empty()) {
                    size_t start = left.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        left = left.substr(start);
                    } else {
                        left = "";
                    }
                }
                if (!left.empty()) {
                    size_t end = left.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { left = left.substr(0, end + 1); }
                }

                if (!right.empty()) {
                    size_t start = right.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        right = right.substr(start);
                    } else {
                        right = "";
                    }
                }
                if (!right.empty()) {
                    size_t end = right.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { right = right.substr(0, end + 1); }
                }

                fprintf(stderr, "DEBUG: != comparison: left='%s', right='%s'\n", left.c_str(), right.c_str());
                condResult = (left != right);
                fprintf(stderr, "DEBUG: != result: %s\n", condResult ? "true" : "false");
            } else if (trimmed.find(">=") != std::string::npos) {
                size_t pos = trimmed.find(">=");
                std::string left = trimmed.substr(0, pos);
                std::string right = trimmed.substr(pos + 2);

                // Trim both sides
                if (!left.empty()) {
                    size_t start = left.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        left = left.substr(start);
                    } else {
                        left = "";
                    }
                }
                if (!left.empty()) {
                    size_t end = left.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { left = left.substr(0, end + 1); }
                }

                if (!right.empty()) {
                    size_t start = right.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        right = right.substr(start);
                    } else {
                        right = "";
                    }
                }
                if (!right.empty()) {
                    size_t end = right.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { right = right.substr(0, end + 1); }
                }

                fprintf(stderr, "DEBUG: >= comparison: left='%s', right='%s'\n", left.c_str(), right.c_str());
                try {
                    double leftVal = std::stod(left);
                    double rightVal = std::stod(right);
                    condResult = (leftVal >= rightVal);
                    fprintf(stderr, "DEBUG: >= numeric result: %s\n", condResult ? "true" : "false");
                } catch (std::exception const&) {
                    condResult = false;
                    fprintf(stderr, "DEBUG: >= exception, result: false\n");
                }
            } else if (trimmed.find("<=") != std::string::npos) {
                size_t pos = trimmed.find("<=");
                std::string left = trimmed.substr(0, pos);
                std::string right = trimmed.substr(pos + 2);

                // Trim both sides
                if (!left.empty()) {
                    size_t start = left.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        left = left.substr(start);
                    } else {
                        left = "";
                    }
                }
                if (!left.empty()) {
                    size_t end = left.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { left = left.substr(0, end + 1); }
                }

                if (!right.empty()) {
                    size_t start = right.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        right = right.substr(start);
                    } else {
                        right = "";
                    }
                }
                if (!right.empty()) {
                    size_t end = right.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { right = right.substr(0, end + 1); }
                }

                fprintf(stderr, "DEBUG: <= comparison: left='%s', right='%s'\n", left.c_str(), right.c_str());
                try {
                    double leftVal = std::stod(left);
                    double rightVal = std::stod(right);
                    condResult = (leftVal <= rightVal);
                    fprintf(stderr, "DEBUG: <= numeric result: %s\n", condResult ? "true" : "false");
                } catch (std::exception const&) {
                    condResult = false;
                    fprintf(stderr, "DEBUG: <= exception, result: false\n");
                }
            } else if (trimmed.find(">") != std::string::npos) {
                size_t pos = trimmed.find(">");
                std::string left = trimmed.substr(0, pos);
                std::string right = trimmed.substr(pos + 1);

                // Trim both sides
                if (!left.empty()) {
                    size_t start = left.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        left = left.substr(start);
                    } else {
                        left = "";
                    }
                }
                if (!left.empty()) {
                    size_t end = left.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { left = left.substr(0, end + 1); }
                }

                if (!right.empty()) {
                    size_t start = right.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        right = right.substr(start);
                    } else {
                        right = "";
                    }
                }
                if (!right.empty()) {
                    size_t end = right.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { right = right.substr(0, end + 1); }
                }

                fprintf(stderr, "DEBUG: > comparison: left='%s', right='%s'\n", left.c_str(), right.c_str());
                try {
                    double leftVal = std::stod(left);
                    double rightVal = std::stod(right);
                    condResult = (leftVal > rightVal);
                    fprintf(stderr, "DEBUG: > numeric result: %s\n", condResult ? "true" : "false");
                } catch (std::exception const&) {
                    condResult = false;
                    fprintf(stderr, "DEBUG: > exception, result: false\n");
                }
            } else if (trimmed.find("<") != std::string::npos) {
                size_t pos = trimmed.find("<");
                std::string left = trimmed.substr(0, pos);
                std::string right = trimmed.substr(pos + 1);

                // Trim both sides
                if (!left.empty()) {
                    size_t start = left.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        left = left.substr(start);
                    } else {
                        left = "";
                    }
                }
                if (!left.empty()) {
                    size_t end = left.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { left = left.substr(0, end + 1); }
                }

                if (!right.empty()) {
                    size_t start = right.find_first_not_of(" \t\n\r");
                    if (start != std::string::npos) {
                        right = right.substr(start);
                    } else {
                        right = "";
                    }
                }
                if (!right.empty()) {
                    size_t end = right.find_last_not_of(" \t\n\r");
                    if (end != std::string::npos) { right = right.substr(0, end + 1); }
                }

                fprintf(stderr, "DEBUG: < comparison: left='%s', right='%s'\n", left.c_str(), right.c_str());
                try {
                    double leftVal = std::stod(left);
                    double rightVal = std::stod(right);
                    condResult = (leftVal < rightVal);
                    fprintf(stderr, "DEBUG: < numeric result: %s\n", condResult ? "true" : "false");
                } catch (std::exception const&) {
                    condResult = false;
                    fprintf(stderr, "DEBUG: < exception, result: false\n");
                }
            } else if (trimmed.empty()) {
                // Empty condition is true
                fprintf(stderr, "DEBUG: Empty condition, result: true\n");
                condResult = true;
            } else {
                // No operator, check if the value is "true"
                fprintf(stderr, "DEBUG: No operator, checking if '%s' == 'true'\n", trimmed.c_str());
                condResult = (trimmed == "true");
                fprintf(stderr, "DEBUG: No operator result: %s\n", condResult ? "true" : "false");
            }

            orResult = orResult || condResult;
            fprintf(stderr, "DEBUG: OR result so far: %s\n", orResult ? "true" : "false");
            if (orResult) break;   // Short-circuit OR
        }

        fprintf(stderr, "DEBUG: Final OR result: %s\n", orResult ? "true" : "false");
        if (!orResult) {
            fprintf(stderr, "DEBUG: Final condition result: false (short-circuit AND)\n");
            return false;   // Short-circuit AND
        }
    }

    fprintf(stderr, "DEBUG: Final condition result: true\n");
    return true;
}

#endif   // __VARIABLE_SUBSTITUTION_HPP__
