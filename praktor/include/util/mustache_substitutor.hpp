#ifndef __MUSTACHE_SUBSTITUTOR_HPP__
#define __MUSTACHE_SUBSTITUTOR_HPP__

#include <string>
#include "dag/workflow_context.hpp"

namespace Praktor::Util {

/**
 * @brief Substitutes variables in a template string using the Mustache engine.
 * 
 * Uses the TurboNet Mustache library to provide full Mustache spec support,
 * including sections, inverted sections, and loops, powered by the WorkflowContext.
 * 
 * @param templateStr The Mustache template string.
 * @param context The workflow context providing data for the template.
 * @return The rendered string.
 */
std::string substituteMustache(const std::string& templateStr, const WorkflowContext& context);

} // namespace Praktor::Util

#endif // __MUSTACHE_SUBSTITUTOR_HPP__
