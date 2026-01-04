#ifndef __DEPENDENCY_GRAPH_HPP__
#define __DEPENDENCY_GRAPH_HPP__

#include "graph.hpp"
#include "workflow_context.hpp"
#include "../yml/task.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

/**
 * @class DependencyGraph
 * @brief A graph that manages task dependencies with support for conditional edges and subgraphs.
 *
 * This class extends the base Graph with features like conditional edges,
 * which are only traversed if a certain condition is met, and subgraphs,
 * which allow for nesting workflows.
 *
 * @tparam T The type of the nodes in the graph.
 */
template <typename T>
class DependencyGraph : public Graph<T> {
public:
    /**
     * @brief Constructs a DependencyGraph with a given name.
     * @param name The name of the graph.
     */
    DependencyGraph(std::string const& name) : Graph<T>(name) {}

    // Note: getInDegree() is inherited from Graph<T> with O(1) complexity

    /**
     * @brief Sets a subgraph for a given node.
     * @param node The node to associate the subgraph with.
     * @param subgraph The subgraph to set.
     */
    void setSubgraph(T const& node, std::shared_ptr<DependencyGraph<T>> subgraph) {
        if (!this->hasNode(node)) { this->addNode(node); }
        subgraphs_[node] = subgraph;
    }

    /**
     * @brief Gets the subgraph for a given node.
     * @param node The node to get the subgraph for.
     * @return A shared pointer to the subgraph, or nullptr if none exists.
     */
    std::shared_ptr<DependencyGraph<T>> getSubgraph(T const& node) const {
        auto it = subgraphs_.find(node);
        return (it != subgraphs_.end()) ? it->second : nullptr;
    }

    /**
     * @brief Checks if a node has a subgraph.
     * @param node The node to check.
     * @return True if the node has a subgraph, false otherwise.
     */
    bool hasSubgraph(T const& node) const { return subgraphs_.count(node) > 0; }

    /**
     * @brief Creates a new graph containing a target node and all its dependencies.
     * @param targetNode The name of the task to run.
     * @return A new DependencyGraph containing the subgraph.
     */
    DependencyGraph<T> createSubgraphFor(const T& targetNode) const {
        DependencyGraph<T> subgraph("subgraph_for_" + targetNode.name);

        std::vector<T> to_visit;
        to_visit.push_back(targetNode);

        std::unordered_set<T> visited;
        visited.insert(targetNode);

        int head = 0;
        while(head < to_visit.size()){
            const auto& current_node = to_visit[head++];
            subgraph.addNode(current_node);

            // Find all nodes that have an edge to the current node
            for (const auto& n : this->getNodes()) {
                for (const auto& edge : this->getEdges(n)) {
                    if (edge == current_node) {
                        // n is a dependency of current_node
                        subgraph.addEdge(n, current_node);
                        if (visited.find(n) == visited.end()) {
                            visited.insert(n);
                            to_visit.push_back(n);
                        }
                    }
                }
            }
        }

        return subgraph;
    }

private:
    /// @brief A map of nodes to their associated subgraphs.
    std::unordered_map<T, std::shared_ptr<DependencyGraph<T>>> subgraphs_;
};

#endif   // __DEPENDENCY_GRAPH_HPP__
