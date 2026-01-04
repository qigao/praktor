#ifndef __GHA_EXPORTER_HPP__
#define __GHA_EXPORTER_HPP__

#include "yml/task.hpp"
#include "util/template_loader.hpp"
#include "util/string_utils.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Prakter {
namespace Utils {

class GithubActionsExporter {
public:
    static bool exportToYaml(const Workflow& workflow, const std::string& output_path) {
        std::string workflowTmpl = TemplateLoader::loadTemplate("exporters/gha", "workflow.yml");
        std::string stepTmpl = TemplateLoader::loadTemplate("exporters/gha", "step.yml");

        if (workflowTmpl.empty() || stepTmpl.empty()) {
            return false;
        }

        std::string steps_content;
        for (const auto& task : workflow.tasks) {
            std::string step = stepTmpl;
            step = Prakter::util::replaceAll(step, "{{ TASK_NAME }}", task.name);
            
            std::string cmd_str;
            if (task.action == TaskAction::RunCommand) {
                cmd_str = "echo 'Running command task'";
            } else if (task.action == TaskAction::Script) {
                 cmd_str = "echo 'Running script task'";
            }
            step = Prakter::util::replaceAll(step, "{{ TASK_COMMAND }}", cmd_str);

            std::string deps;
            if (!task.depends_on.empty()) {
                deps = "        # depends_on: " + joinStrings(task.depends_on);
            }
            step = Prakter::util::replaceAll(step, "{{ TASK_DEPENDS }}", deps);
            
            steps_content += step + "\n";
        }

        std::string final_content = workflowTmpl;
        final_content = Prakter::util::replaceAll(final_content, "{{ WORKFLOW_NAME }}", 
                                                 workflow.name.empty() ? "Prakter Workflow" : workflow.name);
        final_content = Prakter::util::replaceAll(final_content, "{{ STEPS }}", steps_content);

        std::ofstream file(output_path);
        if (!file.is_open()) return false;
        file << final_content;
        file.close();

        std::cout << "Successfully exported to GitHub Actions: " << output_path << std::endl;
        return true;
    }

private:
    static std::string joinStrings(const std::vector<std::string>& vec) {
        std::string result;
        for (size_t i = 0; i < vec.size(); ++i) {
            result += vec[i];
            if (i < vec.size() - 1) result += ", ";
        }
        return result;
    }
};

} // namespace Utils
} // namespace Prakter

#endif // __GHA_EXPORTER_HPP__
