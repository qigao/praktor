#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include "data/structured_document_query.hpp"
#include "dag/task_registry.hpp"
#include "dag/variable_scope.hpp"

namespace Praktor::util {

/**
 * @brief Resolve WorkflowContext-style paths without bloating WorkflowContext itself.
 *
 * This helper owns the path traversal rules for:
 * - task registry lookups under `tasks.*`
 * - direct scope values
 * - nested JSON object traversal
 * - JSONPath queries against scope values
 */
class WorkflowPathResolver {
public:
    static WorkflowValue getValueByPath(const VariableScope* scope,
                                        const TaskRegistry& task_registry,
                                        const std::string& path) {
        std::string_view trimmed = trimPath(path);
        if (trimmed.empty() || scope == nullptr) {
            return WorkflowValue::null();
        }

        if (trimmed.substr(0, 5) == "tasks") {
            return resolveTaskPath(task_registry, scope, trimmed);
        }

        std::string key(trimmed);
        if (scope->has(key)) {
            try {
                return scope->get(key);
            } catch (const std::exception&) {
                // Fall through to nested lookup.
            }
        }

        return traverseNestedPath(*scope, trimmed);
    }

    static WorkflowValue getJsonValue(const VariableScope& scope,
                                      const std::string& key,
                                      const std::string& json_path) {
        try {
            return Praktor::Data::StructuredDocumentQuery::queryJson(scope.get(key), json_path);
        } catch (const std::exception& e) {
            throw std::runtime_error("JSONPath query failed for key '" + key + "': " +
                                     e.what());
        }
    }

private:
    static std::string_view trimPath(const std::string& path) {
        const size_t first = path.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }

        const size_t last = path.find_last_not_of(" \t\r\n");
        return std::string_view(path).substr(first, last - first + 1);
    }

    static WorkflowValue resolveTaskPath(const TaskRegistry& task_registry,
                                         const VariableScope* scope,
                                         std::string_view path) {
        if (path == "tasks" || path == "tasks.") {
            return task_registry.toJson();
        }

        const auto dot1 = path.find('.', 6);
        if (dot1 == std::string_view::npos) {
            return task_registry.toJson();
        }

        const std::string task_name(path.substr(6, dot1 - 6));
        const std::string_view rest = path.substr(dot1 + 1);

        if (rest == "status") {
            return WorkflowValue(task_registry.getStatus(task_name));
        }
        if (rest == "outputs") {
            return task_registry.getAllOutputs(task_name);
        }
        if (rest.substr(0, 8) == "outputs.") {
            return resolveTaskOutput(task_registry, scope, task_name, rest.substr(8));
        }

        return WorkflowValue::null();
    }

    static WorkflowValue resolveTaskOutput(const TaskRegistry& task_registry,
                                           const VariableScope* scope,
                                           const std::string& task_name,
                                           std::string_view output_path) {
        const auto dot = output_path.find('.');
        const std::string base_key(output_path.substr(0, dot));

        WorkflowValue current = WorkflowValue::null();
        try {
            current = task_registry.getOutput(task_name, base_key);
        } catch (const std::exception&) {
            const std::string mirrored_path = "tasks." + task_name + ".outputs." + base_key;
            if (scope->has(mirrored_path)) {
                current = scope->get(mirrored_path);
            } else {
                return WorkflowValue::null();
            }
        }

        if (dot == std::string_view::npos) {
            return current;
        }

        return traverseJson(current, output_path.substr(dot + 1));
    }

    static WorkflowValue traverseNestedPath(const VariableScope& scope,
                                            std::string_view path) {
        const auto dot = path.find('.');
        if (dot == std::string_view::npos) {
            return WorkflowValue::null();
        }

        const std::string base_key(path.substr(0, dot));
        if (!scope.has(base_key)) {
            throw std::runtime_error("Context path not found: " + base_key);
        }

        return traverseJson(scope.get(base_key), path.substr(dot + 1));
    }

    static WorkflowValue traverseJson(WorkflowValue current,
                                      std::string_view remaining_path) {
        size_t start = 0;
        while (start < remaining_path.size()) {
            const auto dot = remaining_path.find('.', start);
            const size_t end =
                (dot == std::string_view::npos) ? remaining_path.size() : dot;
            const std::string segment(remaining_path.substr(start, end - start));

            if (!current.is_object() || !current.contains(segment)) {
                return WorkflowValue::null();
            }

            WorkflowValue next = current.at(segment);
            current = std::move(next);
            start = (dot == std::string_view::npos) ? remaining_path.size() : dot + 1;
        }

        return current;
    }
};

} // namespace Praktor::util
