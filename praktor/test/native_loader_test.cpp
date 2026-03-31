#include "util/native_loader.hpp"
#include "yml/task_types.hpp"

#include <catch2/catch_all.hpp>
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("NativeModule struct", "[native]") {
    SECTION("Hooks map") {
        NativeModule mod;
        mod.name = "native_math";
        mod.path = "./native/native_math.dll";
        mod.hooks["call"] = "native_math_dispatch";

        REQUIRE(mod.name == "native_math");
        REQUIRE(mod.path == "./native/native_math.dll");
        REQUIRE(mod.hooks.at("call") == "native_math_dispatch");
    }
}

TEST_CASE("NativeLoader singleton", "[native]") {
    SECTION("Instance returns same object") {
        auto& loader1 = Praktor::Native::NativeLoader::instance();
        auto& loader2 = Praktor::Native::NativeLoader::instance();

        REQUIRE(&loader1 == &loader2);
    }
}

TEST_CASE("NativeLoader error handling", "[native]") {
    auto& loader = Praktor::Native::NativeLoader::instance();

    SECTION("Non-existent module file returns false") {
        NativeModule mod;
        mod.name = "nonexistent";
        mod.path = "./does_not_exist.dll";
        mod.hooks["call"] = "nonexistent_dispatch";

        bool result = loader.loadModuleLibrary(mod, ".");
        REQUIRE_FALSE(result);
    }

    SECTION("loadModuleLibraries throws on failure") {
        NativeModules modules;
        NativeModule mod;
        mod.name = "bad_module";
        mod.path = "./nonexistent.dll";
        mod.hooks["call"] = "bad_dispatch";
        modules.push_back(mod);

        REQUIRE_THROWS_WITH(
            loader.loadModuleLibraries(modules, "."),
            Catch::Matchers::ContainsSubstring("Failed to load native module"));
    }
}

TEST_CASE("NativeModules in Workflow parsing", "[native][parser]") {
    NativeModules modules;

    NativeModule mod1;
    mod1.name = "native_a";
    mod1.path = "./native/native_a.dll";
    mod1.hooks["init"] = "js_init_native_a";
    mod1.hooks["call"] = "native_a_dispatch";
    modules.push_back(mod1);

    NativeModule mod2;
    mod2.name = "native_b";
    mod2.path = "./native/native_b.so";
    mod2.hooks["call"] = "custom_dispatch";
    modules.push_back(mod2);

    REQUIRE(modules.size() == 2);
    REQUIRE(modules[0].name == "native_a");
    REQUIRE(modules[0].hooks.at("call") == "native_a_dispatch");
    REQUIRE(modules[1].hooks.count("init") == 0);
    REQUIRE(modules[1].hooks.at("call") == "custom_dispatch");
}
