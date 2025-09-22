#ifndef __ENHANCED_GRAPH_HPP__
#define __ENHANCED_GRAPH_HPP__

#include "graph.hpp"
#include "workflow_context.hpp"
#include "../yml/task.hpp" // Include the full definition of Task and FlowFile

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declaration is no longer sufficient
// struct FlowFile;

/**
 * @class EnhancedGraph
 * @brief An enhanced graph that supports conditional edges and subgraphs.
 *
 * This class extends the base Graph with features like conditional edges,
 * which are only traversed if a certain condition is met, and subgraphs,
 * which allow for nesting workflows.
 *
 * @tparam T The type of the nodes in the graph.
 */
template <typename T>
class EnhancedGraph : public Graph<T> {
public:
    /**
     * @brief Constructs an EnhancedGraph with a given name.
     * @param name The name of the graph.
     */
    EnhancedGraph(std::string const& name) : Graph<T>(name) {}

    /**
     * @brief Gets the in-degree of a node.
     * @param node The node to get the in-degree for.
     * @return The in-degree of the node.
     */
    int getInDegree(const T& node) const {
        int inDegree = 0;
        for (const auto& n : this->getNodes()) {
            for (const auto& edge : this->getEdges(n)) {
                if (edge == node) {
                    inDegree++;
                }
            }
        }
        return inDegree;
    }

    /**
     * @brief Sets a subgraph for a given node.
     * @param node The node to associate the subgraph with.
     * @param subgraph The subgraph to set.
     */
    void setSubgraph(T const& node, std::shared_ptr<EnhancedGraph<T>> subgraph) {
        if (!this->hasNode(node)) { this->addNode(node); }
        subgraphs_[node] = subgraph;
    }

    /**
     * @brief Gets the subgraph for a given node.
     * @param node The node to get the subgraph for.
     * @return A shared pointer to the subgraph, or nullptr if none exists.
     */
    std::shared_ptr<EnhancedGraph<T>> getSubgraph(T const& node) const {
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
     * @return A new EnhancedGraph containing the subgraph.
     */
    EnhancedGraph<T> createSubgraphFor(const T& targetNode) const {
        EnhancedGraph<T> subgraph("subgraph_for_" + targetNode.name);
        
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
    std::unordered_map<T, std::shared_ptr<EnhancedGraph<T>>> subgraphs_;
};

#endif   // __ENHANCED_GRAPH_HPP__