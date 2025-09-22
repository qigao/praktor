#ifndef __CAKE_DOT_H__
#define __CAKE_DOT_H__

#include "dot_writer.hpp"
#include "task_tree.hpp"
#include "yml/task.hpp"
#include "yml/task_types.hpp"

#include <fmt/core.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

class DotGraph {
private:
    Graphs subgraphs_;
    std::set<Edge> edges_;

public:
    void create_dot_file(TaskTree const& task_flow, DotWriter& writer, FilterType filter = FilterType::NONE,
                         std::string const& filterNode = "") {
        subgraphs_.clear();
        edges_.clear();

        // Get the task flows from the task tree and process them
        TaskFlows const& flows = task_flow.getTaskTree();

        // Debug output
        std::cout << "TaskTree contains " << flows.size() << " flows:" << std::endl;
        for (auto const& [name, flow] : flows) {
            std::cout << "  Flow name: " << name << ", tasks: " << flow.tasks.size() << std::endl;
        }

        for (auto const& cakePair : flows) {
            // Use the weave name from the map key, not from the FlowFile
            parseCake(cakePair.first, cakePair.second);
        }

        // Create the dot graph
        std::string dotContent = create_dot_graph(flows, filter, filterNode);
        writer.write(dotContent);
        writer.close();
    }

    // Overload to accept TaskFlows directly
    void create_dot_file(TaskFlows const& flows, DotWriter& writer, FilterType filter = FilterType::NONE,
                         std::string const& filterNode = "") {
        subgraphs_.clear();
        edges_.clear();

        // Debug output
        std::cout << "TaskFlows contains " << flows.size() << " flows:" << std::endl;
        for (auto const& [name, flow] : flows) {
            std::cout << "  Flow name: " << name << ", tasks: " << flow.tasks.size() << std::endl;
        }

        // Process the task flows directly
        for (auto const& cakePair : flows) {
            // Use the weave name from the map key, not from the FlowFile
            parseCake(cakePair.first, cakePair.second);
        }

        // Create the dot graph
        std::string dotContent = create_dot_graph(flows, filter, filterNode);
        writer.write(dotContent);
        writer.close();
    }

private:
    // Modified to take the weave name as a parameter
    void parseCake(std::string const& cakeName, FlowFile const& weave) {

        // Debug output
        std::cout << "Parsing weave with name: " << cakeName << std::endl;

        // Initialize the subgraph for this weave
        if (subgraphs_.find(cakeName) == subgraphs_.end()) { subgraphs_[cakeName] = {}; }

        // Add all tasks to the subgraph
        for (auto const& taskPair : weave.tasks) {
            Task const& task = taskPair.second;
            subgraphs_[cakeName].push_back(task.name);

            // Debug output
            std::cout << "Added task " << task.name << " to subgraph " << cakeName << std::endl;

            // Process dependencies
            for (auto const& dep : task.depends) {
                std::string depCakeName = cakeName;
                std::string depTaskName = dep;

                // Check if dependency is from another weave (contains ':')
                size_t colonPos = dep.find(':');
                if (colonPos != std::string::npos) {
                    depCakeName = dep.substr(0, colonPos);
                    depTaskName = dep.substr(colonPos + 1);

                    // Ensure the dependent weave has a subgraph entry
                    if (subgraphs_.find(depCakeName) == subgraphs_.end()) { subgraphs_[depCakeName] = {}; }
                }

                // Create source node (current task)
                std::string source = cakeName + ":" + task.name;

                // Create destination node (dependency)
                std::string dest = depCakeName + ":" + depTaskName;

                // Add the edge to the edges_ set (task -> dependency)
                edges_.insert({source, dest});

                // Debug output
                std::cout << "Added edge " << source << " -> " << dest << std::endl;
            }
        }
    }

    std::string create_dot_graph(TaskFlows const& flows, FilterType filter, std::string const& filterNode) {
        std::stringstream dot_graph_stream;
        dot_graph_stream << "digraph CakeFlow {\n";
        dot_graph_stream << "    compound=true;\n";

        // Debug output
        std::cout << "Creating DOT graph with " << flows.size() << " flows and " << subgraphs_.size() << " subgraphs"
                  << std::endl;
        for (auto const& [name, flow] : flows) {
            std::cout << "Flow: " << name << ", tasks: " << flow.tasks.size() << std::endl;
        }
        for (auto const& [name, tasks] : subgraphs_) {
            std::cout << "Subgraph: " << name << ", tasks: " << tasks.size() << std::endl;
        }

        if (filter == NO_SUBGRAPHS) {
            for (auto const& cakePair : flows) {
                std::cout << "Processing flow " << cakePair.first << " for NO_SUBGRAPHS" << std::endl;
                for (auto const& taskName : subgraphs_[cakePair.first]) {
                    dot_graph_stream << "    \"" << cakePair.first << ":" << taskName << "\";\n";
                    std::cout << "Added node " << cakePair.first << ":" << taskName << std::endl;
                }
            }
        } else {
            for (auto const& subgraphPair : subgraphs_) {
                std::string cakeName = subgraphPair.first;
                if (subgraphPair.second.empty()) {
                    std::cout << "Skipping empty subgraph " << cakeName << std::endl;
                    continue;   // skip empty subgraphs
                }

                // Create the subgraph with the weave name
                dot_graph_stream << "    subgraph cluster_" << sanitizeGraphName(cakeName) << " {\n";
                dot_graph_stream << "        label = \"" << cakeName << "\";\n";
                std::cout << "Created subgraph for " << cakeName << std::endl;

                // Add all tasks to the subgraph with their weave prefix
                for (auto const& taskName : subgraphPair.second) {
                    // Make sure we have the weave name prefix
                    dot_graph_stream << "        \"" << cakeName << ":" << taskName << "\";\n";
                    std::cout << "Added node " << cakeName << ":" << taskName << " to subgraph" << std::endl;
                }
                dot_graph_stream << "    }\n";
            }
        }

        for (auto const& edge : edges_) {
            if (filter == NODE_CHAIN && !isNodeInChain(edge.first, edge.second, filterNode, edges_)) {
                std::cout << "Skipping edge " << edge.first << " -> " << edge.second << " due to filter" << std::endl;
                continue;
            }

            // Write the edge with the full qualified names
            dot_graph_stream << "    \"" << edge.first << "\" -> \"" << edge.second << "\";\n";
            std::cout << "Added edge " << edge.first << " -> " << edge.second << " to graph" << std::endl;
        }

        dot_graph_stream << "}\n";
        return dot_graph_stream.str();
    }

    bool isNodeInChain(std::string const& source, std::string const& dest, std::string const& filterNode,
                       std::set<Edge> const& edges) {
        if (filterNode.empty()) return true;
        if (source == filterNode || dest == filterNode) return true;

        std::set<std::string> nodesInChain;
        StrList queue;
        queue.push_back(filterNode);
        nodesInChain.insert(filterNode);

        // BFS for dependents
        int head = 0;
        while (head < queue.size()) {
            std::string current_node = queue[head++];
            for (auto const& edge : edges) {
                if (edge.first == current_node && nodesInChain.find(edge.second) == nodesInChain.end()) {
                    nodesInChain.insert(edge.second);
                    queue.push_back(edge.second);
                }
                if (edge.second == current_node && nodesInChain.find(edge.first) == nodesInChain.end()) {
                    nodesInChain.insert(edge.first);
                    queue.push_back(edge.first);
                }
            }
        }
        return nodesInChain.count(source) || nodesInChain.count(dest);
    }

    std::string sanitizeGraphName(std::string const& name) {
        std::string sanitizedName = name;
        for (char& c : sanitizedName) {
            if (!isalnum(c)) { c = '_'; }
        }
        return sanitizedName;
    }
};

#endif   // __CAKE_DOT_H__
