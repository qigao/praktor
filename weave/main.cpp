#include "util/cxxopts.hpp"
#include "util/logger.hpp"
#include "workflow_runner.hpp"
#include "yml/task_parser.hpp" // Replaced validator with parser

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace {
    constexpr char const* VERSION = "1.0.0";
    constexpr int DEFAULT_MAX_CONCURRENCY = 4;
    constexpr int MIN_CONCURRENCY = 1;
    constexpr int MAX_CONCURRENCY = 32;

    struct CliConfig {
        std::string yamlPath;
        std::string taskName;
        bool useConcurrent = false;
        bool verbose = false;
        bool showVersion = false;
        bool validateOnly = false;
        int maxConcurrency = DEFAULT_MAX_CONCURRENCY;
        std::vector<std::string> inputParams;  // New: input parameters
    };
}   // namespace

class WeaveApplication {
public:
    struct Config {
        std::string yamlPath;
        std::string taskName;
        bool useConcurrent = false;
        bool verbose = false;
        int maxConcurrency = DEFAULT_MAX_CONCURRENCY;
        std::unordered_map<std::string, std::string> inputValues;  // New: parsed input values
    };

    explicit WeaveApplication(Config config) : config_(std::move(config)) {}

    int run() {
        try {
            setupLogging();
            logConfiguration();

            auto runner = createWorkflowRunner();
            if (!runner) { return 1; }

            bool success = executeWorkflow(*runner);
            logResult(success);

            return success ? 0 : 1;
        } catch (std::exception const& e) {
            LOG_ERROR("Unexpected error: " + std::string(e.what()));
            return 1;
        }
    }

private:
    Config config_;

    void setupLogging() const {
        auto& logger = Logger::getInstance();
        logger.setLevel(config_.verbose ? Logger::Level::DEBUG : Logger::Level::INFO);
        logger.showTimestamps(false);
    }

    void logConfiguration() const {
        if (!config_.verbose) return;

        LOG_INFO("Verbose mode enabled");
        LOG_INFO("Using YAML file: " + config_.yamlPath);
        if (!config_.taskName.empty()) { LOG_INFO("Running specific task: " + config_.taskName); }
        LOG_INFO("Execution mode: " + std::string(config_.useConcurrent ? "concurrent" : "sequential"));
        if (config_.useConcurrent) { LOG_INFO("Maximum concurrent tasks: " + std::to_string(config_.maxConcurrency)); }
    }

    std::unique_ptr<WorkflowRunner> createWorkflowRunner() const {
        try {
            return std::make_unique<WorkflowRunner>(config_.yamlPath, config_.inputValues);
        } catch (std::exception const& e) {
            LOG_ERROR("Failed to create workflow runner: " + std::string(e.what()));
            return nullptr;
        }
    }

    bool executeWorkflow(WorkflowRunner& runner) const {
        try {
            if (!config_.taskName.empty()) {
                LOG_INFO("Starting specific task execution: " + config_.taskName);
                return runner.runTask(config_.taskName, config_.useConcurrent, config_.maxConcurrency);
            } else {
                LOG_INFO("Starting workflow execution");
                return runner.run(config_.useConcurrent, config_.maxConcurrency);
            }
        } catch (std::exception const& e) {
            LOG_ERROR("Workflow execution failed: " + std::string(e.what()));
            return false;
        }
    }

    void logResult(bool success) const {
        if (config_.verbose) { LOG_INFO("Workflow execution " + std::string(success ? "succeeded" : "failed")); }
    }
};

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

void setupCliOptions(cxxopts::Options& options, CliConfig& config) {
    options.add_options()
        ("f,file", "Path to the YAML workflow file", cxxopts::value<std::string>(config.yamlPath))
        ("t,task", "Run a specific task (and its dependencies)", cxxopts::value<std::string>(config.taskName))
        ("c,concurrent", "Use concurrent execution mode", cxxopts::value<bool>(config.useConcurrent))
        ("v,verbose", "Enable verbose output", cxxopts::value<bool>(config.verbose))
        ("validate", "Validate YAML file grammar and structure only", cxxopts::value<bool>(config.validateOnly))
        ("j,jobs", "Maximum number of concurrent tasks", cxxopts::value<int>(config.maxConcurrency)->default_value(std::to_string(DEFAULT_MAX_CONCURRENCY)))
        ("i,input", "Input parameter in key=value format (can be repeated)", cxxopts::value<std::vector<std::string>>(config.inputParams))
        ("version", "Show version information", cxxopts::value<bool>(config.showVersion))
        ("h,help", "Print usage");

    options.parse_positional({"file"});
}

int main(int argc, char* argv[]) {
    CliConfig config;
    cxxopts::Options options("weave", "Weave Workflow Runner - A DAG-based task execution system");
    setupCliOptions(options, config);

    try {
        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (config.showVersion) {
            std::cout << "Weave Workflow Runner v" << VERSION << std::endl;
            return 0;
        }

        if (config.yamlPath.empty()) {
            std::cerr << "Error: YAML workflow file is required" << std::endl;
            std::cout << options.help() << std::endl;
            return 1;
        }

        if (!std::filesystem::exists(config.yamlPath)) {
            std::cerr << "Error: File '" << config.yamlPath << "' does not exist" << std::endl;
            return 1;
        }

        if (config.maxConcurrency < MIN_CONCURRENCY || config.maxConcurrency > MAX_CONCURRENCY) {
            std::cerr << "Error: jobs must be between " << MIN_CONCURRENCY << " and " << MAX_CONCURRENCY << std::endl;
            return 1;
        }

        if (config.validateOnly) {
            try {
                TaskParser::parseFile(config.yamlPath);
                std::cout << "Validation successful: " << config.yamlPath << " is a valid Weave workflow file." << std::endl;
                return 0;
            } catch (const std::exception& e) {
                std::cerr << "Validation failed: " << e.what() << std::endl;
                return 1;
            }
        }

        WeaveApplication::Config appConfig{
            config.yamlPath,
            config.taskName,
            config.useConcurrent,
            config.verbose,
            config.maxConcurrency,
            parseInputParams(config.inputParams)  // New: parse input parameters
        };
        WeaveApplication application(appConfig);
        return application.run();

    } catch (cxxopts::exceptions::exception const& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        std::cout << options.help() << std::endl;
        return 1;
    }
}
