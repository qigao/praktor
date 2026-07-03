#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @struct PairHash
 * @brief A hash function for pairs, used in unordered maps.
 * @tparam T The type of the elements in the pair.
 */
template <typename T>
struct PairHash {
    /**
     * @brief Computes the hash of a pair.
     * @param p The pair to hash.
     * @return The hash value.
     */
    std::size_t operator()(std::pair<T, T> const& p) const {
        auto h1 = std::hash<T>{}(p.first);
        auto h2 = std::hash<T>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

// Forward declaration
template <typename T>
class Visitor;

/**
 * @class Graph
 * @brief A generic directed graph implementation.
 * @tparam T The type of the nodes in the graph.
 */
template <typename T>
class Graph {
public:
    /**
     * @brief Constructs a graph with a given name.
     * @param name The name of the graph.
     */
    Graph(std::string const& name) : name_(name) {}

    virtual ~Graph() = default;

    /**
     * @brief Gets the name of the graph.
     * @return The name of the graph.
     */
    std::string getName() const { return name_; }

    /**
     * @brief Adds a node to the graph.
     * @param node The node to add.
     */
    void addNode(T const& node) {
        if (adjacencyList_.find(node) == adjacencyList_.end()) {
            adjacencyList_[node] = std::vector<T>();
            inDegree_[node] = 0;
        }
    }

    /**
     * @brief Checks if a node exists in the graph.
     * @param node The node to check.
     * @return True if the node exists, false otherwise.
     */
    bool hasNode(T const& node) const { return adjacencyList_.count(node) > 0; }

    /**
     * @brief Adds an edge between two nodes.
     * @param from The source node.
     * @param to The destination node.
     */
    void addEdge(T const& from, T const& to) {
        addNode(from);
        addNode(to);
        adjacencyList_[from].push_back(to);
        inDegree_[to]++;
    }

    /**
     * @brief Gets the in-degree of a node (O(1) lookup).
     * @param node The node to get the in-degree for.
     * @return The in-degree of the node, or 0 if node doesn't exist.
     */
    int getInDegree(T const& node) const {
        auto it = inDegree_.find(node);
        return (it != inDegree_.end()) ? it->second : 0;
    }

    /**
     * @brief Gets all nodes in the graph.
     * @return A vector of all nodes.
     */
    std::vector<T> getNodes() const {
        std::vector<T> nodes;
        nodes.reserve(adjacencyList_.size());
        for (auto const& pair : adjacencyList_) {
            nodes.push_back(pair.first);
        }
        return nodes;
    }

    /**
     * @brief Gets all edges from a specific node.
     * @param node The node to get the edges from.
     * @return A vector of destination nodes.
     */
    std::vector<T> getEdges(T const& node) const {
        auto it = adjacencyList_.find(node);
        return (it != adjacencyList_.end()) ? it->second : std::vector<T>();
    }

    /**
     * @brief Gets all source nodes (nodes with no incoming edges).
     * @return A vector of source nodes.
     */
    std::vector<T> getSourceNodes() const {
        std::unordered_set<T> nodesWithIncomingEdges;
        for (auto const& pair : adjacencyList_) {
            for (auto const& target : pair.second) {
                nodesWithIncomingEdges.insert(target);
            }
        }

        std::vector<T> sourceNodes;
        for (auto const& pair : adjacencyList_) {
            if (nodesWithIncomingEdges.find(pair.first) == nodesWithIncomingEdges.end()) {
                sourceNodes.push_back(pair.first);
            }
        }
        return sourceNodes;
    }

    /**
     * @brief Gets all sink nodes (nodes with no outgoing edges).
     * @return A vector of sink nodes.
     */
    std::vector<T> getSinkNodes() const {
        std::vector<T> sinkNodes;
        for (auto const& pair : adjacencyList_) {
            if (pair.second.empty()) {
                sinkNodes.push_back(pair.first);
            }
        }
        return sinkNodes;
    }

    /**
     * @brief Checks if the graph has cycles.
     * @return True if the graph has cycles, false otherwise.
     */
    bool hasCycle() const {
        std::unordered_set<T> visited;
        std::unordered_set<T> recursionStack;
        for (auto const& pair : adjacencyList_) {
            if (hasCycleUtil(pair.first, visited, recursionStack)) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Gets a list of all cycles in the graph.
     * @return A vector of vectors, where each inner vector represents a cycle.
     */
    std::vector<std::vector<T>> findCycles() const {
        std::vector<std::vector<T>> cycles;
        std::unordered_set<T> visited;
        std::vector<T> path;
        std::unordered_set<T> inPath;
        for (auto const& pair : adjacencyList_) {
            findCyclesUtil(pair.first, visited, path, inPath, cycles);
        }
        return cycles;
    }

    /**
     * @brief Generates a DOT representation of the graph for visualization.
     * @return A string containing the DOT representation.
     */
    std::string toDot() const {
        std::stringstream ss;
        ss << "digraph " << name_ << " {\n";
        for (auto const& pair : adjacencyList_) {
            ss << "  \"" << pair.first << "\";\n";
        }
        for (auto const& pair : adjacencyList_) {
            for (auto const& target : pair.second) {
                ss << "  \"" << pair.first << "\" -> \"" << target << "\";\n";
            }
        }
        ss << "}\n";
        return ss.str();
    }

protected:
    std::string name_;
    std::unordered_map<T, std::vector<T>> adjacencyList_;
    std::unordered_map<T, int> inDegree_;  // O(1) in-degree lookup

private:
    // Utility function for cycle detection
    bool hasCycleUtil(T const& node, std::unordered_set<T>& visited, std::unordered_set<T>& recursionStack) const {
        // If node is not visited yet, mark it visited and add to recursion stack
        if (visited.find(node) == visited.end()) {
            visited.insert(node);
            recursionStack.insert(node);

            // Check all adjacent nodes
            for (auto const& adjacent : getEdges(node)) {
                // If adjacent node is not visited, check if it forms a cycle
                if (visited.find(adjacent) == visited.end()) {
                    if (hasCycleUtil(adjacent, visited, recursionStack)) { return true; }
                }
                // If adjacent node is in recursion stack, there is a cycle
                else if (recursionStack.find(adjacent) != recursionStack.end()) {
                    return true;
                }
            }
        }

        // Remove node from recursion stack
        recursionStack.erase(node);
        return false;
    }

    // Utility function to find all cycles in the graph
    void findCyclesUtil(T const& node, std::unordered_set<T>& visited, std::vector<T>& path,
                        std::unordered_set<T>& inPath, std::vector<std::vector<T>>& cycles) const {
        // If node is already in path, we found a cycle
        if (inPath.find(node) != inPath.end()) {
            // Find the start of the cycle in the path
            auto cycleStart = std::find(path.begin(), path.end(), node);
            if (cycleStart != path.end()) {
                // Extract the cycle
                std::vector<T> cycle(cycleStart, path.end());
                cycles.push_back(cycle);
            }
            return;
        }

        // If node is already visited, no need to process again
        if (visited.find(node) != visited.end()) { return; }

        // Mark node as visited and add to path
        visited.insert(node);
        path.push_back(node);
        inPath.insert(node);

        // Check all adjacent nodes
        for (auto const& adjacent : getEdges(node)) { findCyclesUtil(adjacent, visited, path, inPath, cycles); }

        // Remove node from path
        path.pop_back();
        inPath.erase(node);
    }
};

