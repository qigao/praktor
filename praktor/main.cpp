#include <cxxopts.hpp>
#include "util/logging.hpp"
#include "util/version.hpp"
#include "workflow_runner.hpp"
#include "yml/task_parser.hpp"
#include "util/dag_exporter.hpp"
#include "util/gha_exporter.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "util/praktor_init.hpp"

namespace {
    constexpr int DEFAULT_MAX_CONCURRENCY = 4;
    constexpr int MIN_CONCURRENCY = 1;
    constexpr int MAX_CONCURRENCY = 32;

    struct CliConfig {
        std::string command;
        std::string yamlPath;
        std::string taskName;
        std::string templateName = "basic";
        std::string exportFormat = "github-actions";
        std::string outputPath;
        bool useConcurrent = false;
        bool verbose = false;
        bool showVersion = false;
        bool benchmark = false;
        int maxConcurrency = DEFAULT_MAX_CONCURRENCY;
        std::vector<std::string> inputParams;
    };

    void setupLogging(bool verbose) {
        // Create default logger with console sink
        tlog_config_t config = {
            .min_level = verbose ? TURBO_LOG_LEVEL_DEBUG : TURBO_LOG_LEVEL_INFO,
            .async_mode = 0, // Sync mode for CLI tool
            .buffer_size = 0,
            .pool_size = 0
        };
        tlog_t* logger = tlog_create(&config);

        turbo_console_sink_opts_t console_opts = {
            .output = stdout,
            .use_colors = 1,
            .pattern = TURBO_LOG_DEFAULT_PATTERN
        };
        tlog_add_sink(logger, turbo_sink_console_create(&console_opts));
        tlog_set_default(logger);
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
} // namespace

int main(int argc, char* argv[]) {
    CliConfig config;
    cxxopts::Options options("praktor", "Praktor - A task & workflow orchestrator by DAG");

    options.add_options()
        ("command", "Subcommand (init, validate, export, visualize)", cxxopts::value<std::string>(config.command))
        ("f,file", "Path to the YAML workflow file", cxxopts::value<std::string>(config.yamlPath))
        ("t,task", "Run a specific task", cxxopts::value<std::string>(config.taskName))
        ("c,concurrent", "Use concurrent execution mode", cxxopts::value<bool>(config.useConcurrent))
        ("v,verbose", "Enable verbose output", cxxopts::value<bool>(config.verbose))
        ("j,jobs", "Max concurrent tasks", cxxopts::value<int>(config.maxConcurrency)->default_value(std::to_string(DEFAULT_MAX_CONCURRENCY)))
        ("i,input", "Input parameter key=value", cxxopts::value<std::vector<std::string>>(config.inputParams))
        ("o,output", "Output file path (for export/visualize)", cxxopts::value<std::string>(config.outputPath))
        ("template", "Template for init (basic, cpp-library)", cxxopts::value<std::string>(config.templateName))
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

        if (config.command == "export") {
            if (config.outputPath.empty()) config.outputPath = "github-actions.yml";
            try {
                auto workflow = TaskParser::parseFile(config.yamlPath);
                return Praktor::Utils::GithubActionsExporter::exportToYaml(workflow, config.outputPath) ? 0 : 1;
            } catch (const std::exception& e) {
                std::cerr << "Export failed: " << e.what() << std::endl;
                return 1;
            }
        }

        // Default: Execute workflow (when no explicit command is given)
        setupLogging(config.verbose);
        auto start = std::chrono::high_resolution_clock::now();

        bool success = false;
        try {
            WorkflowRunner runner(config.yamlPath, parseInputParams(config.inputParams));
            if (!config.taskName.empty()) {
                success = runner.runTask(config.taskName, config.useConcurrent, config.maxConcurrency);
            } else {
                success = runner.run(config.useConcurrent, config.maxConcurrency);
            }
        } catch (const std::exception& e) {
            loge("Execution failed: {}", e.what());
            return 1;
        }

        auto end = std::chrono::high_resolution_clock::now();
        if (config.benchmark) {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "\nBenchmark Result:\n";
            std::cout << "  Execution Time: " << duration << "ms\n";
            std::cout << "  Status: " << (success ? "Success" : "Failure") << std::endl;
        }
        return success ? 0 : 1;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

