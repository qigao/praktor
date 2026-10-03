#include "workflow_effects.hpp"

#include "util/path_utils.hpp"
#include "yml/task_parser.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace Praktor::Effects {
namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Unable to read effect-analysis dependency: " +
                                 path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
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
    if (cursor >= source.size() || source[cursor++] != '(') return false;
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

void analyzeOrchNode(const OrchNode& node, EffectManifest& manifest) {
    if (node.type == "Shell") {
        manifest.add("process");
    } else if (node.type == "FileExists") {
        manifest.add("filesystem_read");
    }
    for (const auto& child : node.children) {
        analyzeOrchNode(child, manifest);
    }
}

class Analyzer {
public:
    EffectManifest analyze(const fs::path& workflow_path) {
        const fs::path absolute =
            fs::absolute(workflow_path).lexically_normal();
        root_directory_ = absolute.parent_path();
        analyzeWorkflowFile(absolute);
        return manifest_;
    }

private:
    bool outsideRoot(const std::string& raw_path,
                     const fs::path& source_path) const {
        if (raw_path.empty() ||
            raw_path.find("{{") != std::string::npos ||
            raw_path.find("}}") != std::string::npos) {
            return false;
        }
        fs::path path(raw_path);
        if (!path.is_absolute()) {
            path = source_path.parent_path() / path;
        }
        path = fs::absolute(path).lexically_normal();

        auto root_it = root_directory_.begin();
        auto path_it = path.begin();
        for (; root_it != root_directory_.end(); ++root_it, ++path_it) {
            if (path_it == path.end() || *root_it != *path_it) {
                return true;
            }
        }
        return false;
    }

    void classifyPath(const std::string& raw_path,
                      const fs::path& source_path,
                      const char* effect) {
        if (raw_path.empty()) {
            return;
        }
        manifest_.add(effect);
        if (outsideRoot(raw_path, source_path)) {
            manifest_.add("outside_workspace");
        }
    }
    void markUnknown(std::string reason) {
        manifest_.unknown_effects = true;
        if (std::find(manifest_.unknown_reasons.begin(),
                      manifest_.unknown_reasons.end(),
                      reason) == manifest_.unknown_reasons.end()) {
            manifest_.unknown_reasons.push_back(std::move(reason));
        }
    }

    void analyzeScript(const std::string& source, const fs::path& source_path) {
        if (source.find("shell.exec(") != std::string::npos) {
            manifest_.add("process");
        }
        if (source.find("http.") != std::string::npos) {
            manifest_.add("network");
        }
        if (source.find("api.openai.com") != std::string::npos ||
            source.find("api.anthropic.com") != std::string::npos ||
            source.find("generativelanguage.googleapis.com") != std::string::npos) {
            manifest_.add("model_api");
        }
        if (source.find("fs.") != std::string::npos) {
            manifest_.add("filesystem_read");
            manifest_.add("filesystem_write");
        }
        if (source.find("dll.") != std::string::npos) {
            manifest_.add("native_extension");
            markUnknown("script uses native extension surface");
        }

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

            std::size_t begin = 0;
            std::size_t end = 0;
            if (!parseImportLiteral(source, cursor, begin, end)) {
                ++cursor;
                continue;
            }

            const std::string name = source.substr(begin, end - begin);
            const fs::path import_path(name);
            if (import_path.extension() == ".tbs") {
                const fs::path resolved = import_path.is_absolute()
                    ? import_path.lexically_normal()
                    : (source_path.parent_path() / import_path).lexically_normal();
                const std::string key = fs::absolute(resolved).lexically_normal().generic_string();
                if (script_files_.insert(key).second) {
                    analyzeScript(readFile(key), key);
                }
            } else if (name == "net") {
                manifest_.add("network");
            } else if (name == "os") {
                manifest_.add("process");
                manifest_.add("system_control");
                manifest_.add("filesystem_read");
                manifest_.add("filesystem_write");
            } else if (name == "mapper" || name == "parser") {
                // Known data-only plugins.
            } else {
                manifest_.add("plugin");
                markUnknown("unknown TurboScript plugin: " + name);
            }
            cursor = end + 1;
        }
    }

    void analyzeWorkflowFile(const fs::path& workflow_path) {
        const std::string key =
            fs::absolute(workflow_path).lexically_normal().generic_string();
        if (!workflow_files_.insert(key).second) {
            return;
        }

        Workflow workflow = TaskParser::parseFileWithIncludes(
            key, fs::path(key).parent_path().string());

        for (const auto& env_file : workflow.dot_env) {
            classifyPath(env_file, fs::path(key), "filesystem_read");
        }

        for (const auto& task : workflow.tasks) {
            const fs::path task_source =
                task.source_path.empty() ? fs::path(key)
                                         : fs::path(task.source_path);

            for (const auto& env_file : task.dot_env) {
                classifyPath(env_file, task_source, "filesystem_read");
            }
            for (const auto& source : task.sources) {
                classifyPath(source, task_source, "filesystem_read");
            }
            for (const auto& generated : task.generates) {
                classifyPath(generated, task_source, "filesystem_write");
            }
            if (task.working_dir.has_value() &&
                outsideRoot(*task.working_dir, task_source)) {
                manifest_.add("outside_workspace");
            }

            switch (task.action) {
            case TaskAction::Program: {
                manifest_.add("process");
                const auto& params = std::get<ProgramParams>(task.specifics);
                if (outsideRoot(params.program, task_source)) {
                    manifest_.add("outside_workspace");
                }
                break;
            }
            case TaskAction::Download: {
                manifest_.add("network");
                const auto& params = std::get<DownloadParams>(task.specifics);
                classifyPath(params.path, task_source, "filesystem_write");
                break;
            }
            case TaskAction::Service:
                manifest_.add("process");
                manifest_.add("system_control");
                break;
            case TaskAction::ManagedProcess: {
                manifest_.add("process");
                manifest_.add("system_control");
                const auto& params =
                    std::get<ManagedProcessParams>(task.specifics);
                if (outsideRoot(params.executable, task_source) ||
                    outsideRoot(params.working_directory, task_source)) {
                    manifest_.add("outside_workspace");
                }
                break;
            }
            case TaskAction::HostTool: {
                // Backend authority/effects remain host-owned. Praktor can prove
                // only that reviewed host execution is required; the embedding
                // host performs capability/effect admission before execution.
                manifest_.add("host_tool");
                const auto& params = std::get<HostToolParams>(task.specifics);
                markUnknown("host tool effects resolved by embedding host: " +
                            params.tool);
                break;
            }
            case TaskAction::DynamicTasks:
                // Dynamic task templates currently generate command/orch tasks.
                manifest_.add("process");
                break;
            case TaskAction::Uses: {
                const auto& params = std::get<UsesParams>(task.specifics);
                const fs::path nested = Praktor::util::resolveRelativePath(
                    task.source_path, params.path);
                analyzeWorkflowFile(nested);
                break;
            }
            case TaskAction::Orch: {
                const auto& params = std::get<OrchParams>(task.specifics);
                analyzeOrchNode(params.root, manifest_);
                break;
            }
            case TaskAction::Command:
                // Legacy enum value; command currently desugars to Orch.
                manifest_.add("process");
                break;
            case TaskAction::None:
                break;
            }

            if (task.script.has_value()) {
                analyzeScript(*task.script,
                              task.source_path.empty()
                                  ? fs::path(key)
                                  : fs::path(task.source_path));
            }
        }
    }

    EffectManifest manifest_;
    fs::path root_directory_;
    std::unordered_set<std::string> workflow_files_;
    std::unordered_set<std::string> script_files_;
};

} // namespace

void EffectManifest::add(std::string effect) {
    effects.insert(std::move(effect));
}

void EffectManifest::merge(const EffectManifest& other) {
    effects.insert(other.effects.begin(), other.effects.end());
    unknown_effects = unknown_effects || other.unknown_effects;
    for (const auto& reason : other.unknown_reasons) {
        if (std::find(unknown_reasons.begin(), unknown_reasons.end(), reason) ==
            unknown_reasons.end()) {
            unknown_reasons.push_back(reason);
        }
    }
}

WorkflowValue EffectManifest::toValue() const {
    WorkflowValue result = WorkflowValue::object();
    WorkflowValue effect_values = WorkflowValue::array();
    for (const auto& effect : effects) {
        effect_values.push_back(effect);
    }
    result["effects"] = std::move(effect_values);
    result["unknown_effects"] = unknown_effects;

    WorkflowValue reasons = WorkflowValue::array();
    for (const auto& reason : unknown_reasons) {
        reasons.push_back(reason);
    }
    result["unknown_reasons"] = std::move(reasons);
    return result;
}

EffectManifest analyzeWorkflow(const fs::path& workflow_path) {
    return Analyzer().analyze(workflow_path);
}

} // namespace Praktor::Effects
