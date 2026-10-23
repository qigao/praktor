#include "praktor_test.hpp"
#include "dag/graph.hpp"
#include "dag/dependency_graph.hpp"


[[maybe_unused]] static void test_graph_getindegree_o_1_optimization_1(int section_index) {
    Graph<std::string> g("test");

    if (section_index == 0) {
        check((g.getInDegree("nonexistent") == 0));
    }

    if (section_index == 1) {
        g.addNode("A");
        check((g.getInDegree("A") == 0));
    }

    if (section_index == 2) {
        g.addEdge("A", "B");
        check((g.getInDegree("A") == 0));
        check((g.getInDegree("B") == 1));
    }

    if (section_index == 3) {
        g.addEdge("A", "C");
        g.addEdge("B", "C");
        g.addEdge("D", "C");
        check((g.getInDegree("C") == 3));
        check((g.getInDegree("A") == 0));
        check((g.getInDegree("B") == 0));
        check((g.getInDegree("D") == 0));
    }

    if (section_index == 4) {
        // A -> B -> C -> D
        g.addEdge("A", "B");
        g.addEdge("B", "C");
        g.addEdge("C", "D");
        check((g.getInDegree("A") == 0));
        check((g.getInDegree("B") == 1));
        check((g.getInDegree("C") == 1));
        check((g.getInDegree("D") == 1));
    }

    if (section_index == 5) {
        //     A
        //    / \
        //   B   C
        //    \ /
        //     D
        g.addEdge("A", "B");
        g.addEdge("A", "C");
        g.addEdge("B", "D");
        g.addEdge("C", "D");
        check((g.getInDegree("A") == 0));
        check((g.getInDegree("B") == 1));
        check((g.getInDegree("C") == 1));
        check((g.getInDegree("D") == 2));
    }
}

spec("Graph::getInDegree O(1) optimization") {
    it("Empty graph returns 0 for any node") { test_graph_getindegree_o_1_optimization_1(0); }
    it("Node with no incoming edges has in-degree 0") { test_graph_getindegree_o_1_optimization_1(1); }
    it("Single edge increments in-degree") { test_graph_getindegree_o_1_optimization_1(2); }
    it("Multiple edges to same node") { test_graph_getindegree_o_1_optimization_1(3); }
    it("Chain of dependencies") { test_graph_getindegree_o_1_optimization_1(4); }
    it("Diamond dependency pattern") { test_graph_getindegree_o_1_optimization_1(5); }
}

[[maybe_unused]] static void test_dependencygraph_inherits_getindegree_2() {
    DependencyGraph<std::string> dg("dep_test");

    dg.addEdge("task1", "task2");
    dg.addEdge("task1", "task3");
    dg.addEdge("task2", "task4");
    dg.addEdge("task3", "task4");

    check((dg.getInDegree("task1") == 0));
    check((dg.getInDegree("task2") == 1));
    check((dg.getInDegree("task3") == 1));
    check((dg.getInDegree("task4") == 2));
}

spec("DependencyGraph inherits getInDegree") {
    it("complete case") { test_dependencygraph_inherits_getindegree_2(); }
}
