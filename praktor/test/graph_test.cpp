#include "dag/graph.hpp"
#include "dag/dependency_graph.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("Graph::getInDegree O(1) optimization", "[graph]") {
    Graph<std::string> g("test");

    SECTION("Empty graph returns 0 for any node") {
        REQUIRE(g.getInDegree("nonexistent") == 0);
    }

    SECTION("Node with no incoming edges has in-degree 0") {
        g.addNode("A");
        REQUIRE(g.getInDegree("A") == 0);
    }

    SECTION("Single edge increments in-degree") {
        g.addEdge("A", "B");
        REQUIRE(g.getInDegree("A") == 0);
        REQUIRE(g.getInDegree("B") == 1);
    }

    SECTION("Multiple edges to same node") {
        g.addEdge("A", "C");
        g.addEdge("B", "C");
        g.addEdge("D", "C");
        REQUIRE(g.getInDegree("C") == 3);
        REQUIRE(g.getInDegree("A") == 0);
        REQUIRE(g.getInDegree("B") == 0);
        REQUIRE(g.getInDegree("D") == 0);
    }

    SECTION("Chain of dependencies") {
        // A -> B -> C -> D
        g.addEdge("A", "B");
        g.addEdge("B", "C");
        g.addEdge("C", "D");
        REQUIRE(g.getInDegree("A") == 0);
        REQUIRE(g.getInDegree("B") == 1);
        REQUIRE(g.getInDegree("C") == 1);
        REQUIRE(g.getInDegree("D") == 1);
    }

    SECTION("Diamond dependency pattern") {
        //     A
        //    / \
        //   B   C
        //    \ /
        //     D
        g.addEdge("A", "B");
        g.addEdge("A", "C");
        g.addEdge("B", "D");
        g.addEdge("C", "D");
        REQUIRE(g.getInDegree("A") == 0);
        REQUIRE(g.getInDegree("B") == 1);
        REQUIRE(g.getInDegree("C") == 1);
        REQUIRE(g.getInDegree("D") == 2);
    }
}

TEST_CASE("DependencyGraph inherits getInDegree", "[graph]") {
    DependencyGraph<std::string> dg("dep_test");

    dg.addEdge("task1", "task2");
    dg.addEdge("task1", "task3");
    dg.addEdge("task2", "task4");
    dg.addEdge("task3", "task4");

    REQUIRE(dg.getInDegree("task1") == 0);
    REQUIRE(dg.getInDegree("task2") == 1);
    REQUIRE(dg.getInDegree("task3") == 1);
    REQUIRE(dg.getInDegree("task4") == 2);
}
