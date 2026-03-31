
#include "core/ast.hpp"
#include "tinytest.h"

using namespace btdsl;

suite("AST - Node") {

    given("a node") {
        when("created with default values") {
            then("should have empty id and no children") {
                Node node;
                check_true(node.id.empty());
                check_true(node.params.empty());
                check_true(node.children.empty());
                check_int_eq(node.line, 0);
                check_int_eq(node.column, 0);
            }
        }

        when("created with id") {
            then("should store the id") {
                Node node;
                node.id = "Sequence";
                check_str_eq(node.id.c_str(), "Sequence");
            }
        }

        when("adding parameters") {
            then("should store string parameter") {
                Node node;
                node.params["name"] = std::string("test");
                check_true(node.params.count("name") > 0);
                check_str_eq(std::get<std::string>(node.params["name"]).c_str(), "test");
            }

            then("should store double parameter") {
                Node node;
                node.params["timeout"] = 5.0;
                check_true(node.params.count("timeout") > 0);
                check_double_eq(std::get<double>(node.params["timeout"]), 5.0, 0.001);
            }

            then("should store bool parameter") {
                Node node;
                node.params["enabled"] = true;
                check_true(node.params.count("enabled") > 0);
                check_true(std::get<bool>(node.params["enabled"]));
            }
        }

        when("adding children") {
            then("should store child nodes") {
                Node parent;
                parent.id = "Sequence";

                Node child1;
                child1.id = "TaskA";
                Node child2;
                child2.id = "TaskB";

                parent.children.push_back(child1);
                parent.children.push_back(child2);

                check_size_eq(parent.children.size(), 2);
                check_str_eq(parent.children[0].id.c_str(), "TaskA");
                check_str_eq(parent.children[1].id.c_str(), "TaskB");
            }
        }

        when("setting line and column") {
            then("should store position information") {
                Node node;
                node.line = 10;
                node.column = 5;
                check_int_eq(node.line, 10);
                check_int_eq(node.column, 5);
            }
        }
    }
}

suite("AST - Tree") {

    given("a tree") {
        when("created with name and root") {
            then("should store tree information") {
                Tree tree;
                tree.name = "MyTree";
                tree.root.id = "Sequence";

                check_str_eq(tree.name.c_str(), "MyTree");
                check_str_eq(tree.root.id.c_str(), "Sequence");
            }
        }

        when("building a complete tree") {
            then("should maintain structure") {
                Tree tree;
                tree.name = "Navigation";
                tree.root.id = "Sequence";

                Node child1;
                child1.id = "CheckBattery";
                Node child2;
                child2.id = "MoveTo";
                child2.params["target"] = std::string("goal");

                tree.root.children.push_back(child1);
                tree.root.children.push_back(child2);

                check_str_eq(tree.name.c_str(), "Navigation");
                check_str_eq(tree.root.id.c_str(), "Sequence");
                check_size_eq(tree.root.children.size(), 2);
                check_str_eq(tree.root.children[0].id.c_str(), "CheckBattery");
                check_str_eq(tree.root.children[1].id.c_str(), "MoveTo");
                check_str_eq(
                    std::get<std::string>(tree.root.children[1].params["target"]).c_str(),
                    "goal"
                );
            }
        }
    }
}

suite("AST - Program") {

    given("a program") {
        when("created empty") {
            then("should have no trees") {
                Program program;
                check_true(program.trees.empty());
            }
        }

        when("adding single tree") {
            then("should store the tree") {
                Program program;
                Tree tree;
                tree.name = "MainTree";
                tree.root.id = "Sequence";

                program.trees.push_back(tree);

                check_size_eq(program.trees.size(), 1);
                check_str_eq(program.trees[0].name.c_str(), "MainTree");
            }
        }

        when("adding multiple trees") {
            then("should store all trees") {
                Program program;

                Tree tree1;
                tree1.name = "Tree1";
                tree1.root.id = "Sequence";

                Tree tree2;
                tree2.name = "Tree2";
                tree2.root.id = "Fallback";

                program.trees.push_back(tree1);
                program.trees.push_back(tree2);

                check_size_eq(program.trees.size(), 2);
                check_str_eq(program.trees[0].name.c_str(), "Tree1");
                check_str_eq(program.trees[0].root.id.c_str(), "Sequence");
                check_str_eq(program.trees[1].name.c_str(), "Tree2");
                check_str_eq(program.trees[1].root.id.c_str(), "Fallback");
            }
        }
    }
}

suite("AST - Value Variant") {

    given("a Value variant") {
        when("storing different types") {
            then("should handle string values") {
                Value v = std::string("hello");
                check_true(std::holds_alternative<std::string>(v));
                check_str_eq(std::get<std::string>(v).c_str(), "hello");
            }

            then("should handle double values") {
                Value v = 3.14;
                check_true(std::holds_alternative<double>(v));
                check_double_eq(std::get<double>(v), 3.14, 0.001);
            }

            then("should handle bool values") {
                Value v = true;
                check_true(std::holds_alternative<bool>(v));
                check_true(std::get<bool>(v));
            }
        }

        when("reassigning values") {
            then("should change type correctly") {
                Value v = std::string("text");
                check_true(std::holds_alternative<std::string>(v));

                v = 42.0;
                check_true(std::holds_alternative<double>(v));
                check_double_eq(std::get<double>(v), 42.0, 0.001);

                v = false;
                check_true(std::holds_alternative<bool>(v));
                check_false(std::get<bool>(v));
            }
        }
    }
}

suite("AST - Complex Structures") {

    given("a complex behavior tree") {
        when("building nested structure") {
            then("should maintain hierarchy") {
                Tree tree;
                tree.name = "ComplexTree";
                tree.root.id = "Fallback";

                // First branch: Sequence with two tasks
                Node seq;
                seq.id = "Sequence";
                Node task1;
                task1.id = "CheckCondition";
                Node task2;
                task2.id = "Execute";
                seq.children.push_back(task1);
                seq.children.push_back(task2);

                // Second branch: Retry decorator
                Node retry;
                retry.id = "Retry";
                retry.params["num_attempts"] = 3.0;
                Node task3;
                task3.id = "FallbackTask";
                retry.children.push_back(task3);

                tree.root.children.push_back(seq);
                tree.root.children.push_back(retry);

                // Verify structure
                check_str_eq(tree.root.id.c_str(), "Fallback");
                check_size_eq(tree.root.children.size(), 2);

                // First child (Sequence)
                check_str_eq(tree.root.children[0].id.c_str(), "Sequence");
                check_size_eq(tree.root.children[0].children.size(), 2);
                check_str_eq(tree.root.children[0].children[0].id.c_str(), "CheckCondition");
                check_str_eq(tree.root.children[0].children[1].id.c_str(), "Execute");

                // Second child (Retry)
                check_str_eq(tree.root.children[1].id.c_str(), "Retry");
                check_double_eq(std::get<double>(tree.root.children[1].params["num_attempts"]), 3.0, 0.001);
                check_size_eq(tree.root.children[1].children.size(), 1);
                check_str_eq(tree.root.children[1].children[0].id.c_str(), "FallbackTask");
            }
        }
    }
}
