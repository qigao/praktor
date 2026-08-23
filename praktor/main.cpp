// Each repeated --input argument is one complete key=value record. Values may
// contain JSON commas, so cxxopts must not split vector items on commas.
#define CXXOPTS_VECTOR_DELIMITER '\0'
#include <cxxopts.hpp>
#include <tlog.h>

#include "util/logging.hpp"
#include "util/version.hpp"
#include "workflow_runner.hpp"
#include "yml/task_parser.hpp"
#include "util/dag_exporter.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "util/praktor_init.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
    constexpr int DEFAULT_MAX_CONCURRENCY = 4;
    constexpr int MIN_CONCURRENCY = 1;
    constexpr int MAX_CONCURRENCY = 32;

    // Trigger-chain depth safety bound: mirrors Praktor::Execution::kMaxTriggerChainDepth.
    constexpr int DEFAULT_MAX_TRIGGER_DEPTH = static_cast<int>(Praktor::Execution::kMaxTriggerChainDepth);
    constexpr int MIN_TRIGGER_DEPTH = 1;
    constexpr int MAX_TRIGGER_DEPTH = 256;

    enum class ColorMode {
        Auto,
        Always,
        Never
    };

    struct CliConfig {
        std::string command;
        std::string yamlPath;
        std::string taskName;
        std::string templateName = "basic";
        std::string outputPath;
        bool useConcurrent = false;
        bool verbose = false;
        bool showVersion = false;
        bool benchmark = false;
        int maxConcurrency = DEFAULT_MAX_CONCURRENCY;
        int maxTriggerDepth = DEFAULT_MAX_TRIGGER_DEPTH;
        std::string color = "auto";
        std::vector<std::string> inputParams;
    };

    bool isConsoleStream(FILE* stream) {
#ifdef _WIN32
        return _isatty(_fileno(stream)) != 0;
#else
        return isatty(fileno(stream)) != 0;
#endif
    }

    bool enableVirtualTerminalProcessing(int stream_id) {
#ifdef _WIN32
        HANDLE handle = GetStdHandle(static_cast<DWORD>(stream_id));
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            return false;
        }

        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode)) {
            return false;
        }

        if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
            return true;
        }

        return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        (void)stream_id;
        return true;
#endif
    }

    ColorMode parseColorMode(const std::string& value) {
        if (value == "auto") return ColorMode::Auto;
        if (value == "always") return ColorMode::Always;
        if (value == "never") return ColorMode::Never;
        throw std::runtime_error("Invalid --color value: '" + value + "' (expected auto, always, or never)");
    }

    bool shouldUseColor(ColorMode mode) {
        switch (mode) {
            case ColorMode::Always:
                return true;
            case ColorMode::Never:
                return false;
            case ColorMode::Auto:
                return isConsoleStream(stdout) && isConsoleStream(stderr);
        }
        return false;
    }

    std::unordered_map<std::string, std::string> buildColorEnvironment(bool enable_color) {
        if (enable_color) {
            return {
                {"CLICOLOR", "1"},
                {"CLICOLOR_FORCE", "1"},
                {"FORCE_COLOR", "1"},
                {"NO_COLOR", ""},
                {"TERM", "xterm-256color"}
            };
        }

        return {
            {"CLICOLOR", "0"},
            {"CLICOLOR_FORCE", ""},
            {"FORCE_COLOR", "0"},
            {"NO_COLOR", "1"}
        };
    }

    void writeCompactLog(const turbo_log_entry_t* entry, void* /*user_data*/) {
        std::string_view message(entry->message, entry->message_len);

        if (message.rfind("__TASK__:", 0) == 0) {
            const std::string_view payload = message.substr(9);
            const size_t split = payload.rfind(':');
            if (split != std::string_view::npos) {
                const std::string_view task_name = payload.substr(0, split);
                const std::string_view status = payload.substr(split + 1);

                const char* status_color = Praktor::Logging::color("muted");
                if (status == "RUNNING") status_color = Praktor::Logging::color("running");
                else if (status == "SUCCESS") status_color = Praktor::Logging::color("success");
                else if (status == "SKIPPED") status_color = Praktor::Logging::color("warn");
                else if (status == "FAILED") status_color = Praktor::Logging::color("error");

                std::fprintf(stdout, "%s└%s %s[%.*s]%s %s%.*s%s\n",
                             Praktor::Logging::color("muted"), Praktor::Logging::reset(),
                             Praktor::Logging::color("task"), static_cast<int>(task_name.size()), task_name.data(),
                             Praktor::Logging::reset(),
                             status_color, static_cast<int>(status.size()), status.data(),
                             Praktor::Logging::reset());
                std::fflush(stdout);
            }
            return;
        }

        if (message.rfind("__WORKFLOW__:", 0) == 0) {
            const std::string_view status = message.substr(13);
            const char* status_color = status == "SUCCESS"
                ? Praktor::Logging::color("success")
                : Praktor::Logging::color("error");
            std::fprintf(stdout, "%s[workflow]%s %s%.*s%s\n",
                         Praktor::Logging::color("muted"), Praktor::Logging::reset(),
                         status_color, static_cast<int>(status.size()), status.data(),
                         Praktor::Logging::reset());
            std::fflush(stdout);
            return;
        }

        if (message.rfind("__SHELL__:", 0) == 0) {
            const std::string_view line = message.substr(10);
            std::fprintf(stdout, "    %.*s\n", static_cast<int>(line.size()), line.data());
            std::fflush(stdout);
            return;
        }

        if (message.rfind("__SCRIPT__:", 0) == 0) {
            const std::string_view line = message.substr(11);
            std::fprintf(stdout, "    %.*s\n", static_cast<int>(line.size()), line.data());
            std::fflush(stdout);
            return;
        }

        switch (entry->level) {
            case TURBO_LOG_LEVEL_INFO:
                std::fprintf(stdout, "    %.*s\n",
                             static_cast<int>(entry->message_len), entry->message);
                std::fflush(stdout);
                break;
            case TURBO_LOG_LEVEL_WARN:
                std::fprintf(stderr, "%s[warn]%s %.*s\n",
                             Praktor::Logging::color("warn"), Praktor::Logging::reset(),
                             static_cast<int>(entry->message_len), entry->message);
                std::fflush(stderr);
                break;
            case TURBO_LOG_LEVEL_ERROR:
            case TURBO_LOG_LEVEL_FATAL:
                std::fprintf(stderr, "%s[error]%s %.*s\n",
                             Praktor::Logging::color("error"), Praktor::Logging::reset(),
                             static_cast<int>(entry->message_len), entry->message);
                std::fflush(stderr);
                break;
            default:
                return;
        }
    }

    tlog_t* setupLogging(bool verbose, bool use_color) {
        Praktor::Logging::configure(verbose, use_color);

#ifdef _WIN32
        if (use_color) {
            enableVirtualTerminalProcessing(STD_OUTPUT_HANDLE);
            enableVirtualTerminalProcessing(STD_ERROR_HANDLE);
        }
#endif

        // Create default logger with console sink
        tlog_config_t config = {
            .min_level = verbose ? TURBO_LOG_LEVEL_DEBUG : TURBO_LOG_LEVEL_INFO,
            .buffer_size = 0,
            .pool_size = 0
        };
        tlog_t* logger = tlog_create(&config);
        if (!logger) {
            throw std::runtime_error("Failed to create logger");
        }

        turbo_log_sink_t* sink = nullptr;
        if (verbose) {
            turbo_console_sink_opts_t console_opts = {
                .output = stdout,
                .use_colors = use_color ? 1 : 0,
                .pattern = TURBO_LOG_DEFAULT_PATTERN
            };
            sink = turbo_sink_console_create(&console_opts);
        } else {
            sink = turbo_sink_callback_create(writeCompactLog, nullptr);
            if (sink && turbo_sink_set_min_level(sink, TURBO_LOG_LEVEL_INFO) != 0) {
                turbo_sink_destroy(sink);
                sink = nullptr;
            }
        }

        if (!sink || tlog_add_sink(logger, sink) != 0) {
            turbo_sink_destroy(sink);
            tlog_destroy(logger);
            throw std::runtime_error("Failed to configure logger sink");
        }
        tlog_set_default(logger);
        return logger;
    }

    std::unordered_map<std::string, std::string> parseInputParams(const std::vector<std::string>& inputParams) {
        std::unordered_map<std::string, std::string> result;
        for (const auto& param : inputParams) {
            size_t eqPos = param.find('=');
            if (eqPos == std::string::npos) {
                std::cerr << "Warning: Invalid input parameter format: " << param << " (expected key=value)" << std::endl;
                continue;
            }
            std::string key = param.substr(0, eqPos);
            std::string value = param.substr(eqPos + 1);
            result[key] = value;
        }
        return result;
    }

    bool isValidConcurrency(int value) {
        return value >= MIN_CONCURRENCY && value <= MAX_CONCURRENCY;
    }

    bool isValidTriggerDepth(int value) {
        return value >= MIN_TRIGGER_DEPTH && value <= MAX_TRIGGER_DEPTH;
    }
} // namespace

int main(int argc, char* argv[]) {
    CliConfig config;
    cxxopts::Options options("praktor", "Praktor - A task & workflow orchestrator by DAG");

    options.add_options()
        ("command", "Subcommand (init, validate, visualize)", cxxopts::value<std::string>(config.command))
        ("f,file", "Path to the YAML workflow file", cxxopts::value<std::string>(config.yamlPath))
        ("t,task", "Run a specific task", cxxopts::value<std::string>(config.taskName))
        ("c,concurrent", "Use concurrent execution mode", cxxopts::value<bool>(config.useConcurrent))
        ("v,verbose", "Enable verbose output", cxxopts::value<bool>(config.verbose))
        ("j,jobs", "Max concurrent tasks", cxxopts::value<int>(config.maxConcurrency)->default_value(std::to_string(DEFAULT_MAX_CONCURRENCY)))
        ("max-trigger-depth", "Max nested trigger hops before a workflow fails as a circular trigger chain", cxxopts::value<int>(config.maxTriggerDepth)->default_value(std::to_string(DEFAULT_MAX_TRIGGER_DEPTH)))
        ("i,input", "Input parameter key=value", cxxopts::value<std::vector<std::string>>(config.inputParams))
        ("o,output", "Output file path (for visualize)", cxxopts::value<std::string>(config.outputPath))
        ("template", "Template for init", cxxopts::value<std::string>(config.templateName))
        ("color", "Color output (auto, always, never)", cxxopts::value<std::string>(config.color)->default_value("auto"))
        ("benchmark", "Show execution benchmark results", cxxopts::value<bool>(config.benchmark))
        ("version", "Show version information", cxxopts::value<bool>(config.showVersion))
        ("h,help", "Print usage");

    options.parse_positional({"command"});

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (config.showVersion) {
            std::cout << PRAKTOR_PROJECT_NAME << " v" << PRAKTOR_VERSION << std::endl;
            return 0;
        }

        const ColorMode color_mode = parseColorMode(config.color);
        const bool use_color = shouldUseColor(color_mode);

        if (!isValidConcurrency(config.maxConcurrency)) {
            std::cerr << "Error: --jobs must be between " << MIN_CONCURRENCY
                      << " and " << MAX_CONCURRENCY << std::endl;
            return 1;
        }

        if (!isValidTriggerDepth(config.maxTriggerDepth)) {
            std::cerr << "Error: --max-trigger-depth must be between " << MIN_TRIGGER_DEPTH
                      << " and " << MAX_TRIGGER_DEPTH << std::endl;
            return 1;
        }

        // Subcommand dispatch
        if (config.command == "init") {
            return Praktor::Utils::PraktorInit::initialize(config.templateName) ? 0 : 1;
        }

        // All other operations require a YAML file
        if (config.yamlPath.empty()) {
            std::cerr << "Error: YAML workflow file is required" << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(config.yamlPath)) {
            std::cerr << "Error: File '" << config.yamlPath << "' does not exist" << std::endl;
            return 1;
        }

        if (config.command == "validate") {
            try {
                auto workflow = TaskParser::parseFile(config.yamlPath);
                TaskParser::buildGraph(workflow);
                std::cout << "Validation successful: " << config.yamlPath << " is a valid Praktor workflow." << std::endl;
                return 0;
            } catch (const std::exception& e) {
                std::cerr << "Validation failed: " << e.what() << std::endl;
                return 1;
            }
        }

        if (config.command == "visualize") {
            if (config.outputPath.empty()) config.outputPath = "workflow.dot";
            try {
                auto workflow = TaskParser::parseFile(config.yamlPath);
                return Praktor::Utils::DagExporter::exportToDot(workflow, config.outputPath) ? 0 : 1;
            } catch (const std::exception& e) {
                std::cerr << "Visualization failed: " << e.what() << std::endl;
                return 1;
            }
        }

        // Default: Execute workflow (when no explicit command is given)
        tlog_t* logger = setupLogging(config.verbose, use_color);
        auto start = std::chrono::high_resolution_clock::now();

        bool success = false;
        try {
            WorkflowRunner runner(
                config.yamlPath,
                parseInputParams(config.inputParams),
                buildColorEnvironment(use_color),
                static_cast<size_t>(config.maxTriggerDepth)
            );
            if (!config.taskName.empty()) {
                success = runner.runTask(config.taskName, config.useConcurrent, config.maxConcurrency);
            } else {
                success = runner.run(config.useConcurrent, config.maxConcurrency);
            }
        } catch (const std::exception& e) {
            logef("Execution failed: {}", e.what());
            tlog_flush(logger);
            tlog_destroy(logger);
            return 1;
        }

        auto end = std::chrono::high_resolution_clock::now();
        if (config.benchmark) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "\nBenchmark Result:\n";
            std::cout << "  Execution Time: " << duration << "ms\n";
            std::cout << "  Status: " << (success ? "Success" : "Failure") << std::endl;
        }
        tlog_flush(logger);
        tlog_destroy(logger);
        return success ? 0 : 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
