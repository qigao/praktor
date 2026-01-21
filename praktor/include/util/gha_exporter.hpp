#ifndef __GHA_EXPORTER_HPP__
#define __GHA_EXPORTER_HPP__

#include "yml/task.hpp"
#include "util/template_loader.hpp"
#include "util/mustache_substitutor.hpp"
#include <jsoncons/json.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Praktor {
namespace Utils {

class GithubActionsExporter {
public:
    static bool exportToYaml(const Workflow& workflow, const std::string& output_path) {
        std::string workflowTmpl = TemplateLoader::loadTemplate("exporters/gha", "workflow.yml");

        if (workflowTmpl.empty()) {
            return false;
        }

        // Convert workflow to JSON for Mustache
        jsoncons::json data = jsoncons::json::object();
        data["name"] = workflow.name.empty() ? "Praktor Workflow" : workflow.name;
        
        jsoncons::json tasks = jsoncons::json::array();
        for (const auto& task : workflow.tasks) {
            jsoncons::json t = jsoncons::json::object();
            t["name"] = task.name;
            
            if (task.action == TaskAction::RunCommand) {
                // If it's a string, use it. If it's a list, join it.
                if (std::holds_alternative<std::string>(std::get<RunCommandParams>(task.specifics).command)) {
                    t["command"] = std::get<std::string>(std::get<RunCommandParams>(task.specifics).command);
                } else {
                    t["command"] = "echo 'Complex command list'"; // Simplified for exporter
                }
            } else if (task.action == TaskAction::Script) {
                t["script"] = true;
            }

            jsoncons::json deps = jsoncons::json::array();
            for (const auto& dep : task.depends_on) {
                deps.push_back(dep);
            }
            t["depends_on"] = deps;
            tasks.push_back(t);
        }
        data["tasks"] = tasks;

        Praktor::Util::WorkflowContext context;
        for (auto const& [key, value] : data.object_range()) {
            context.setValue(key, value);
        }

        std::string final_content = Praktor::Util::substituteMustache(workflowTmpl, context);

        std::ofstream file(output_path);
        if (!file.is_open()) return false;
        file << final_content;
        file.close();

        std::cout << "Successfully exported to GitHub Actions: " << output_path << std::endl;
        return true;
    }
};

} // namespace Utils
} // namespace Praktor

#endif // __GHA_EXPORTER_HPP__
