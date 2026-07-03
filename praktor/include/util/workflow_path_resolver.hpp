#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jmespath/jmespath.hpp>

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
 * - JMESPath queries against scope values
 */
class WorkflowPathResolver {
public:
    static jsoncons::json getValueByPath(const VariableScope* scope,
                                         const TaskRegistry& task_registry,
                                         const std::string& path) {
        std::string_view trimmed = trimPath(path);
        if (trimmed.empty() || scope == nullptr) {
            return jsoncons::json::null();
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

    static jsoncons::json getJsonValue(const VariableScope& scope,
                                       const std::string& key,
                                       const std::string& jmespath_query) {
        try {
            return jsoncons::jmespath::search(scope.get(key), jmespath_query);
        } catch (const jsoncons::jmespath::jmespath_error& e) {
            throw std::runtime_error("JMESPath query failed for key '" + key + "': " +
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

    static jsoncons::json resolveTaskPath(const TaskRegistry& task_registry,
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
            return jsoncons::json(task_registry.getStatus(task_name));
        }
        if (rest == "outputs") {
            return task_registry.getAllOutputs(task_name);
        }
        if (rest.substr(0, 8) == "outputs.") {
            return resolveTaskOutput(task_registry, scope, task_name, rest.substr(8));
        }

        return jsoncons::json::null();
    }

    static jsoncons::json resolveTaskOutput(const TaskRegistry& task_registry,
                                            const VariableScope* scope,
                                            const std::string& task_name,
                                            std::string_view output_path) {
        const auto dot = output_path.find('.');
        const std::string base_key(output_path.substr(0, dot));

        jsoncons::json current = jsoncons::json::null();
        try {
            current = task_registry.getOutput(task_name, base_key);
        } catch (const std::exception&) {
            const std::string mirrored_path = "tasks." + task_name + ".outputs." + base_key;
            if (scope->has(mirrored_path)) {
                current = scope->get(mirrored_path);
            } else {
                return jsoncons::json::null();
            }
        }

        if (dot == std::string_view::npos) {
            return current;
        }

        return traverseJson(current, output_path.substr(dot + 1));
    }

    static jsoncons::json traverseNestedPath(const VariableScope& scope,
                                             std::string_view path) {
        const auto dot = path.find('.');
        if (dot == std::string_view::npos) {
            return jsoncons::json::null();
        }

        const std::string base_key(path.substr(0, dot));
        if (!scope.has(base_key)) {
            throw std::runtime_error("Context path not found: " + base_key);
        }

        return traverseJson(scope.get(base_key), path.substr(dot + 1));
    }

    static jsoncons::json traverseJson(jsoncons::json current,
                                       std::string_view remaining_path) {
        size_t start = 0;
        while (start < remaining_path.size()) {
            const auto dot = remaining_path.find('.', start);
            const size_t end =
                (dot == std::string_view::npos) ? remaining_path.size() : dot;
            const std::string segment(remaining_path.substr(start, end - start));

            if (!current.is_object() || !current.contains(segment)) {
                return jsoncons::json::null();
            }

            jsoncons::json next = current.at(segment);
            current = std::move(next);
            start = (dot == std::string_view::npos) ? remaining_path.size() : dot + 1;
        }

        return current;
    }
};

} // namespace Praktor::util
