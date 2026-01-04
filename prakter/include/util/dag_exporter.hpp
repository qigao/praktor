#ifndef __DAG_EXPORTER_HPP__
#define __DAG_EXPORTER_HPP__

#include "yml/task.hpp"
#include "util/template_loader.hpp"
#include "util/string_utils.hpp"
#include <fstream>
#include <iostream>
#include <string>

namespace Prakter {
namespace Utils {

class DagExporter {
public:
    static bool exportToDot(const Workflow& workflow, const std::string& output_path) {
        std::string graphTmpl = TemplateLoader::loadTemplate("exporters/dot", "graph.dot");
        std::string nodeTmpl = TemplateLoader::loadTemplate("exporters/dot", "node.dot");
        std::string edgeTmpl = TemplateLoader::loadTemplate("exporters/dot", "edge.dot");

        if (graphTmpl.empty() || nodeTmpl.empty() || edgeTmpl.empty()) {
            return false;
        }

        std::string nodes_content;
        for (const auto& task : workflow.tasks) {
            std::string node = nodeTmpl;
            node = Prakter::util::replaceAll(node, "{{ NODE_ID }}", task.name);
            
            std::string label = task.name;
            if (!task.description.empty()) {
                label += "\\n(" + task.description + ")";
            }
            node = Prakter::util::replaceAll(node, "{{ NODE_LABEL }}", label);
            nodes_content += node;
        }

        std::string edges_content;
        for (const auto& task : workflow.tasks) {
            for (const auto& dep : task.depends_on) {
                std::string edge = edgeTmpl;
                edge = Prakter::util::replaceAll(edge, "{{ SOURCE_ID }}", dep);
                edge = Prakter::util::replaceAll(edge, "{{ TARGET_ID }}", task.name);
                edges_content += edge;
            }
        }

        std::string final_content = graphTmpl;
        final_content = Prakter::util::replaceAll(final_content, "{{ WORKFLOW_NAME }}", 
                                                 workflow.name.empty() ? "Prakter Workflow" : workflow.name);
        final_content = Prakter::util::replaceAll(final_content, "{{ NODES }}", nodes_content);
        final_content = Prakter::util::replaceAll(final_content, "{{ EDGES }}", edges_content);

        std::ofstream file(output_path);
        if (!file.is_open()) return false;
        file << final_content;
        file.close();

        std::cout << "Successfully exported DAG to " << output_path << std::endl;
        return true;
    }
};

} // namespace Utils
} // namespace Prakter

#endif // __DAG_EXPORTER_HPP__
