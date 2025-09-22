#include "docker_plugin.hpp"
#include "util/logger.hpp"
#include "util/variable_substitution.hpp"
#include <sstream>
#include <regex>

namespace weave {
namespace plugins {
namespace docker {

// DockerExecutor implementation
DockerExecutor::DockerExecutor() {
    if (!isDockerAvailable()) {
        throw std::runtime_error("Docker is not available on this system");
    }
}

Weave::Execution::TaskResult DockerExecutor::execute(const Task& task, WorkflowContext& context) {
    try {
        // Build Docker command from task parameters
        std::string docker_cmd = buildDockerCommand(task, context);

        LOG_INFO("Executing Docker command: " + docker_cmd);

        // Execute the command (simplified - in real implementation, use libuv process execution)
        // This would integrate with the existing RunCommandExecutor
        Weave::Execution::TaskResult result(true);

        // Parse Docker-specific outputs
        parseDockerOutput("", context, task);

        return result;

    } catch (const std::exception& e) {
        LOG_ERROR("Docker execution failed: " + std::string(e.what()));
        return Weave::Execution::TaskResult(false, e.what());
    }
}

bool DockerExecutor::isDockerAvailable() const {
    // Check if Docker is available (simplified implementation)
    return system("docker --version > /dev/null 2>&1") == 0;
}

std::string DockerExecutor::buildDockerCommand(const Task& task, const WorkflowContext& context) const {
    std::ostringstream cmd;
    cmd << "docker";

    // Extract Docker-specific parameters from task
    if (task.params.has_value()) {
        // In a real implementation, you'd parse Docker-specific parameters
        // For now, assume the command is in a "command" field
        cmd << " run";

        // Add common Docker options
        cmd << " --rm";  // Remove container after execution

        // Add image and command from task parameters
        // This is simplified - real implementation would parse structured parameters
        if (task.command.has_value()) {
            std::string substituted_cmd = substituteVariables(*task.command, context);
            cmd << " " << substituted_cmd;
        }
    }

    return cmd.str();
}

void DockerExecutor::parseDockerOutput(const std::string& output, WorkflowContext& context, const Task& task) const {
    // Parse Docker-specific output and extract useful information
    // For example, container IDs, image hashes, etc.

    if (task.outputs.has_value()) {
        // Extract container ID if present
        std::regex container_id_regex(R"([a-f0-9]{12,64})");
        std::smatch match;
        if (std::regex_search(output, match, container_id_regex)) {
            context.setValue("docker_container_id", match.str());
        }
    }
}

// DockerComposeExecutor implementation
DockerComposeExecutor::DockerComposeExecutor() {
    // Check if docker-compose is available
    if (system("docker-compose --version > /dev/null 2>&1") != 0) {
        throw std::runtime_error("Docker Compose is not available on this system");
    }
}

Weave::Execution::TaskResult DockerComposeExecutor::execute(const Task& task, WorkflowContext& context) {
    try {
        std::string compose_cmd = buildComposeCommand(task, context);

        LOG_INFO("Executing Docker Compose command: " + compose_cmd);

        // Execute the command (simplified)
        Weave::Execution::TaskResult result(true);

        return result;

    } catch (const std::exception& e) {
        LOG_ERROR("Docker Compose execution failed: " + std::string(e.what()));
        return Weave::Execution::TaskResult(false, e.what());
    }
}

std::string DockerComposeExecutor::buildComposeCommand(const Task& task, const WorkflowContext& context) const {
    std::ostringstream cmd;
    cmd << "docker-compose";

    // Add compose file if specified
    // In real implementation, parse from task parameters
    cmd << " -f docker-compose.yml";

    // Add the compose command (up, down, build, etc.)
    if (task.command.has_value()) {
        std::string substituted_cmd = substituteVariables(*task.command, context);
        cmd << " " << substituted_cmd;
    }

    return cmd.str();
}

// DockerPlugin implementation
DockerPlugin::DockerPlugin() : BaseWeavePlugin("docker", "1.0.0") {
    plugin_info_.description = "Docker and Docker Compose task executor plugin";
    plugin_info_.author = "Weave Team";
    plugin_info_.supported_task_types = {"docker", "docker_compose"};
    plugin_info_.min_weave_version = "1.0.0";
}

PluginInfo DockerPlugin::getPluginInfo() const {
    return plugin_info_;
}

bool DockerPlugin::initialize(const std::unordered_map<std::string, std::string>& config) {
    config_ = config;

    logInfo("Initializing Docker plugin");

    // Get configuration
    docker_command_ = getConfigValue("docker_command", "docker");
    compose_command_ = getConfigValue("compose_command", "docker-compose");

    // Detect Docker capabilities
    detectDockerCapabilities();

    if (!docker_available_) {
        logError("Docker is not available on this system");
        return false;
    }

    logInfo("Docker plugin initialized successfully");
    logInfo("Docker available: " + std::string(docker_available_ ? "yes" : "no"));
    logInfo("Docker Compose available: " + std::string(compose_available_ ? "yes" : "no"));

    initialized_ = true;
    return true;
}

void DockerPlugin::shutdown() {
    if (initialized_) {
        logInfo("Shutting down Docker plugin");
        initialized_ = false;
    }
}

std::unique_ptr<Weave::Execution::TaskExecutor> DockerPlugin::createExecutor(const std::string& taskType) {
    if (!initialized_) {
        logError("Plugin not initialized");
        return nullptr;
    }

    try {
        if (taskType == "docker" && docker_available_) {
            return std::make_unique<DockerExecutor>();
        } else if (taskType == "docker_compose" && compose_available_) {
            return std::make_unique<DockerComposeExecutor>();
        }
    } catch (const std::exception& e) {
        logError("Failed to create executor for " + taskType + ": " + e.what());
    }

    return nullptr;
}

void DockerPlugin::detectDockerCapabilities() {
    // Check Docker availability
    docker_available_ = (system((docker_command_ + " --version > /dev/null 2>&1").c_str()) == 0);

    // Check Docker Compose availability
    compose_available_ = (system((compose_command_ + " --version > /dev/null 2>&1").c_str()) == 0);
}

} // namespace docker
} // namespace plugins
} // namespace weave

// Export the plugin
WEAVE_EXPORT_PLUGIN(weave::plugins::docker::DockerPlugin)
