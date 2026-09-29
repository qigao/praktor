#include "workflow_plan.hpp"
#include "workflow_contract.hpp"
#include "workflow_effects.hpp"

#include "util/path_utils.hpp"
#include "yml/task_parser.hpp"
#include "yml/task_yaml_internal.hpp"

#include <s3/s3_signer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Praktor::Plan {
namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to open WorkflowPlan dependency: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::string sha256(std::string_view bytes) {
    char digest[S3_SIGNER_SHA256_HEX_SIZE + 1]{};
    if (s3_signer_sha256_hex(bytes.data(), bytes.size(), digest) != SALTS_OK) {
        throw std::runtime_error("Unable to compute WorkflowPlan SHA-256");
    }
    return std::string(digest);
}

bool pathWithin(const fs::path& root, const fs::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

std::string joinRoles(const std::vector<std::string>& roles) {
    std::string result;
    for (std::size_t index = 0; index < roles.size(); ++index) {
        if (index != 0) {
            result.push_back(',');
        }
        result += roles[index];
    }
    return result;
}

bool identifierChar(char ch) {
    const auto value = static_cast<unsigned char>(ch);
    return std::isalnum(value) != 0 || ch == '_';
}

bool parseImportLiteral(const std::string& source, std::size_t offset,
                        std::size_t& path_begin, std::size_t& path_end) {
    std::string_view keyword;
    if (source.compare(offset, 13, "import_module") == 0) {
        keyword = "import_module";
    } else if (source.compare(offset, 6, "import") == 0) {
        keyword = "import";
    } else {
        return false;
    }

    if ((offset > 0 && identifierChar(source[offset - 1])) ||
        (offset + keyword.size() < source.size() &&
         identifierChar(source[offset + keyword.size()]))) {
        return false;
    }

    std::size_t cursor = offset + keyword.size();
    while (cursor < source.size() &&
           std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= source.size() || source[cursor++] != '(') {
        return false;
    }
    while (cursor < source.size() &&
           std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
        ++cursor;
    }
    if (cursor >= source.size() ||
        (source[cursor] != '"' && source[cursor] != '\'')) {
        return false;
    }

    const char quote = source[cursor++];
    path_begin = cursor;
    while (cursor < source.size()) {
        if (source[cursor] == '\\' && cursor + 1 < source.size()) {
            cursor += 2;
            continue;
        }
        if (source[cursor] == quote) {
            path_end = cursor;
            return true;
        }
        ++cursor;
    }
    return false;
}

class Compiler {
public:
    explicit Compiler(fs::path root_path)
        : root_path_(canonicalFile(root_path, false)),
          root_directory_(root_path_.parent_path()) {}

    WorkflowPlan build() {
        processWorkflow(root_path_, "root");

        std::vector<WorkflowDependency> dependencies;
        dependencies.reserve(entries_.size());
        for (auto& [_, entry] : entries_) {
            std::sort(entry.roles.begin(), entry.roles.end());
            entry.roles.erase(std::unique(entry.roles.begin(), entry.roles.end()),
                              entry.roles.end());
            dependencies.push_back(std::move(entry));
        }
        std::sort(dependencies.begin(), dependencies.end(),
                  [](const WorkflowDependency& lhs, const WorkflowDependency& rhs) {
                      return lhs.relative_path < rhs.relative_path;
                  });

        std::string manifest;
        for (const auto& dependency : dependencies) {
            manifest += dependency.relative_path;
            manifest.push_back('\0');
            manifest += dependency.sha256;
            manifest.push_back('\0');
            manifest += joinRoles(dependency.roles);
            manifest.push_back('\n');
        }

        const Workflow root_workflow = TaskParser::parseFileWithIncludes(
            root_path_.string(), root_directory_.string());
        return WorkflowPlan(
            root_path_.generic_string(),
            root_directory_.generic_string(),
            sha256(manifest),
            std::move(dependencies),
            Praktor::Contract::inputSchema(root_workflow),
            Praktor::Contract::outputSchema(root_workflow),
            Praktor::Effects::analyzeWorkflow(root_path_).toValue());
    }

private:
    fs::path canonicalFile(const fs::path& requested, bool enforce_root = true) const {
        std::error_code error;
        const auto status = fs::symlink_status(requested, error);
        if (error || !fs::exists(status) || fs::is_symlink(status) ||
            !fs::is_regular_file(status)) {
            throw std::runtime_error(
                "WorkflowPlan dependency must be an existing regular non-symlink file: " +
                requested.string());
        }

        const fs::path canonical = fs::canonical(requested, error);
        if (error) {
            throw std::runtime_error(
                "Unable to canonicalize WorkflowPlan dependency: " + requested.string());
        }
        if (enforce_root && !pathWithin(root_directory_, canonical)) {
            throw std::runtime_error(
                "WorkflowPlan dependency escapes workflow root: " + canonical.string());
        }
        return canonical;
    }

    WorkflowDependency& addDependency(const fs::path& requested,
                                      const std::string& role) {
        const fs::path canonical = canonicalFile(requested);
        const std::string key = canonical.generic_string();
        auto found = entries_.find(key);
        if (found == entries_.end()) {
            const std::string bytes = readFile(canonical);
            WorkflowDependency entry;
            entry.path = key;
            entry.relative_path =
                canonical.lexically_relative(root_directory_).generic_string();
            entry.sha256 = sha256(bytes);
            entry.size = static_cast<std::uint64_t>(bytes.size());
            entry.roles.push_back(role);
            found = entries_.emplace(key, std::move(entry)).first;
        } else {
            found->second.roles.push_back(role);
        }
        return found->second;
    }

    void collectIncludes(const fs::path& workflow_path) {
        const std::string key = workflow_path.generic_string();
        if (!include_scanned_.insert(key).second) {
            return;
        }

        const std::string content = readFile(workflow_path);
        TaskYamlDetail::YamlDocument document(content);
        const auto root = document.root();
        if (!root.has_child("includes")) {
            return;
        }

        const auto& includes = root["includes"];
        if (!includes.is_map()) {
            TaskYamlDetail::throw_parse_error(includes, "'includes' must be a map");
        }

        for (const auto& child : includes) {
            const std::string include_name =
                TaskYamlDetail::read_scalar_or_throw(child,
                    "include path must be a scalar");
            if (include_name.empty()) {
                TaskYamlDetail::throw_parse_error(child, "include path cannot be empty");
            }

            const fs::path include_path =
                canonicalFile(workflow_path.parent_path() / include_name);
            addDependency(include_path, "include");

            // parseFileWithIncludes merges included tasks/variables/env into the
            // parent Workflow but intentionally does not merge the included
            // workflow's top-level dotEnv list. Capture those files directly
            // from the declaring include so the reviewed closure is complete.
            {
                const std::string include_content = readFile(include_path);
                TaskYamlDetail::YamlDocument include_document(include_content);
                Workflow included_workflow =
                    parse_workflow(include_document.root(), include_path.string());
                collectDotEnv(included_workflow.dot_env, include_path);
            }

            collectIncludes(include_path);
        }
    }

    void scanScript(const std::string& source, const fs::path& source_path) {
        std::size_t cursor = 0;
        while (cursor < source.size()) {
            if (source.compare(cursor, 2, "//") == 0) {
                const auto newline = source.find('\n', cursor + 2);
                cursor = newline == std::string::npos ? source.size() : newline + 1;
                continue;
            }
            if (source.compare(cursor, 2, "/*") == 0) {
                const auto end = source.find("*/", cursor + 2);
                cursor = end == std::string::npos ? source.size() : end + 2;
                continue;
            }
            if (source[cursor] == '"' || source[cursor] == '\'') {
                const char quote = source[cursor++];
                while (cursor < source.size()) {
                    if (source[cursor] == '\\' && cursor + 1 < source.size()) {
                        cursor += 2;
                    } else if (source[cursor++] == quote) {
                        break;
                    }
                }
                continue;
            }

            std::size_t path_begin = 0;
            std::size_t path_end = 0;
            if (!parseImportLiteral(source, cursor, path_begin, path_end)) {
                ++cursor;
                continue;
            }

            const std::string import_name =
                source.substr(path_begin, path_end - path_begin);
            const fs::path import_path(import_name);
            if (import_path.extension() == ".tbs") {
                const fs::path resolved = import_path.is_absolute()
                    ? canonicalFile(import_path)
                    : canonicalFile(source_path.parent_path() / import_path);
                processScriptFile(resolved);
            }
            cursor = path_end + 1;
        }
    }

    void processScriptFile(const fs::path& script_path) {
        const fs::path canonical = canonicalFile(script_path);
        addDependency(canonical, "script");
        const std::string key = canonical.generic_string();
        if (!script_scanned_.insert(key).second) {
            return;
        }
        scanScript(readFile(canonical), canonical);
    }

    void collectDotEnv(const std::vector<std::string>& files,
                       const fs::path& source_path) {
        for (const auto& file : files) {
            addDependency(
                Praktor::util::resolveRelativePath(source_path.string(), file),
                "dotenv");
        }
    }

    void processWorkflow(const fs::path& workflow_path, const std::string& role) {
        const fs::path canonical = canonicalFile(workflow_path, role != "root");
        addDependency(canonical, role);

        const std::string key = canonical.generic_string();
        if (active_workflows_.find(key) != active_workflows_.end()) {
            throw std::runtime_error("Circular WorkflowPlan uses dependency: " + key);
        }
        if (!workflow_scanned_.insert(key).second) {
            return;
        }

        active_workflows_.insert(key);
        struct ScopeExit {
            std::set<std::string>& set;
            std::string key;
            ~ScopeExit() { set.erase(key); }
        } scope{active_workflows_, key};

        collectIncludes(canonical);

        Workflow workflow = TaskParser::parseFileWithIncludes(
            canonical.string(), canonical.parent_path().string());
        (void)TaskParser::buildGraph(workflow);

        collectDotEnv(workflow.dot_env, canonical);

        for (const auto& task : workflow.tasks) {
            const fs::path task_source =
                task.source_path.empty() ? canonical : fs::path(task.source_path);
            collectDotEnv(task.dot_env, task_source);

            if (task.script.has_value()) {
                scanScript(*task.script, task_source);
            }

            if (task.action == TaskAction::Uses) {
                const auto& params = std::get<UsesParams>(task.specifics);
                const fs::path nested =
                    Praktor::util::resolveRelativePath(task_source.string(), params.path);
                processWorkflow(nested, "uses");
            }
        }
    }

    fs::path root_path_;
    fs::path root_directory_;
    std::map<std::string, WorkflowDependency> entries_;
    std::unordered_set<std::string> workflow_scanned_;
    std::unordered_set<std::string> include_scanned_;
    std::unordered_set<std::string> script_scanned_;
    std::set<std::string> active_workflows_;
};

std::string manifestDigest(const std::vector<WorkflowDependency>& dependencies) {
    std::string manifest;
    for (const auto& dependency : dependencies) {
        manifest += dependency.relative_path;
        manifest.push_back('\0');
        manifest += dependency.sha256;
        manifest.push_back('\0');
        manifest += joinRoles(dependency.roles);
        manifest.push_back('\n');
    }
    return sha256(manifest);
}

} // namespace

WorkflowPlan WorkflowPlan::compile(const fs::path& workflow_path) {
    if (workflow_path.empty()) {
        throw std::runtime_error("WorkflowPlan requires a workflow path");
    }
    return Compiler(workflow_path).build();
}

bool WorkflowPlan::validate(std::string* error_message) const {
    try {
        std::vector<WorkflowDependency> current = dependencies_;
        for (auto& dependency : current) {
            std::error_code error;
            const fs::path path(dependency.path);
            const auto status = fs::symlink_status(path, error);
            if (error || !fs::exists(status) || fs::is_symlink(status) ||
                !fs::is_regular_file(status)) {
                if (error_message) {
                    *error_message =
                        "WorkflowPlan dependency missing or no longer a regular file: " +
                        dependency.relative_path;
                }
                return false;
            }

            const std::string bytes = readFile(path);
            const std::string current_hash = sha256(bytes);
            if (current_hash != dependency.sha256) {
                if (error_message) {
                    *error_message =
                        "WorkflowPlan dependency changed: " + dependency.relative_path;
                }
                return false;
            }
            dependency.sha256 = std::move(current_hash);
            dependency.size = static_cast<std::uint64_t>(bytes.size());
        }

        if (manifestDigest(current) != digest_) {
            if (error_message) {
                *error_message = "WorkflowPlan manifest digest changed";
            }
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error_message) {
            *error_message = exception.what();
        }
        return false;
    }
}

WorkflowValue WorkflowPlan::toValue() const {
    WorkflowValue result = WorkflowValue::object();
    result["root_path"] = root_path_;
    result["root_directory"] = root_directory_;
    result["digest"] = digest_;
    result["input_schema"] = input_schema_;
    result["output_schema"] = output_schema_;
    result["effect_manifest"] = effect_manifest_;

    WorkflowValue dependencies = WorkflowValue::array();
    for (const auto& dependency : dependencies_) {
        WorkflowValue item = WorkflowValue::object();
        item["path"] = dependency.path;
        item["relative_path"] = dependency.relative_path;
        item["sha256"] = dependency.sha256;
        item["size"] = static_cast<std::int64_t>(dependency.size);

        WorkflowValue roles = WorkflowValue::array();
        for (const auto& role : dependency.roles) {
            roles.push_back(role);
        }
        item["roles"] = std::move(roles);
        dependencies.push_back(std::move(item));
    }
    result["dependencies"] = std::move(dependencies);
    return result;
}

} // namespace Praktor::Plan
