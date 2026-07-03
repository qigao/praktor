#pragma once

#include "util/mustache_substitutor.hpp"
#include "util/template_loader.hpp"
#include "yml/task.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <jsoncons/json.hpp>


namespace Praktor {
namespace Utils {

class DagExporter {
public:
  static bool exportToDot(const Workflow &workflow, const std::string &output_path) {
    std::string graphTmpl = TemplateLoader::loadTemplate("exporters/dot", "graph.dot");

    // Convert workflow to JSON for Mustache
    jsoncons::json data = jsoncons::json::object();
    data["name"] = workflow.name.empty() ? "Praktor Workflow" : workflow.name;
    
    jsoncons::json tasks = jsoncons::json::array();
    for (const auto &task : workflow.tasks) {
      jsoncons::json t = jsoncons::json::object();
      t["name"] = task.name;
      t["description"] = task.description;
      
      jsoncons::json deps = jsoncons::json::array();
      for (const auto &dep : task.depends_on) {
        deps.push_back(dep);
      }
      t["depends_on"] = deps;
      tasks.push_back(t);
    }
    data["tasks"] = tasks;

    // We need a context to use substituteMustache. 
    // Since we just want to render a static structure, we'll put the whole 'data' into a variable.
    WorkflowContext context;
    // context.setValue("workflow", data);
    
    // Adjust template slightly to use the 'workflow' prefix or just wrap the data
    // Actually, I'll just put the fields directly into the context root-level scope if possible.
    // WorkflowContext::setValue takes a string value or json.
    
    for (auto const& [key, value] : data.object_range()) {
        context.setValue(key, value);
    }

    std::string final_content = Praktor::Util::substituteMustache(graphTmpl, context);

    std::ofstream file(output_path);
    if (!file.is_open()) {
      throw std::runtime_error("Failed to open DAG export output: " + output_path);
    }
    file << final_content;
    file.close();

    std::cout << "Successfully exported DAG to " << output_path << std::endl;
    return true;
  }
};

} // namespace Utils
} // namespace Praktor
