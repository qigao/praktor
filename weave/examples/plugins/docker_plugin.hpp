#pragma once

#include "dag/plugin_system.hpp"
#include "dag/task_executor.hpp"

namespace weave {
namespace plugins {
namespace docker {

/**
 * @brief Docker task executor for container operations
 */
class DockerExecutor : public Weave::Execution::TaskExecutor {
public:
    DockerExecutor();
    virtual ~DockerExecutor() = default;

    Weave::Execution::TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "docker"; }

private:
    bool isDockerAvailable() const;
    std::string buildDockerCommand(const Task& task, const WorkflowContext& context) const;
    void parseDockerOutput(const std::string& output, WorkflowContext& context, const Task& task) const;
};

/**
 * @brief Docker Compose executor for multi-container applications
 */
class DockerComposeExecutor : public Weave::Execution::TaskExecutor {
public:
    DockerComposeExecutor();
    virtual ~DockerComposeExecutor() = default;

    Weave::Execution::TaskResult execute(const Task& task, WorkflowContext& context) override;
    std::string getTaskType() const override { return "docker_compose"; }

private:
    std::string buildComposeCommand(const Task& task, const WorkflowContext& context) const;
};

/**
 * @brief Docker plugin implementation
 */
class DockerPlugin : public BaseWeavePlugin {
public:
    DockerPlugin();
    virtual ~DockerPlugin() = default;

    // IWeavePlugin interface
    PluginInfo getPluginInfo() const override;
    bool initialize(const std::unordered_map<std::string, std::string>& config) override;
    void shutdown() override;
    std::unique_ptr<Weave::Execution::TaskExecutor> createExecutor(const std::string& taskType) override;

private:
    bool docker_available_;
    bool compose_available_;
    std::string docker_command_;
    std::string compose_command_;

    void detectDockerCapabilities();
};

} // namespace docker
} // namespace plugins
} // namespace weave
