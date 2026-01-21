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

#include "util/mustache_substitutor.hpp"

/**
 * Substitutes variables in a template string with their values from the context.
 * Now uses the Mustache templating engine for full spec support.
 *
 * @param templateStr The string containing variable placeholders
 * @param context The workflow context containing variable values
 * @return The string with variables substituted
 */
inline std::string substituteVariables(std::string const& templateStr, WorkflowContext const& context) {
    return Praktor::Util::substituteMustache(templateStr, context);
}

#endif   // __VARIABLE_SUBSTITUTION_HPP__
