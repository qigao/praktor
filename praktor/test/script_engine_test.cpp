#include "script/script_engine.hpp"
#include "dag/workflow_context.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("Script engine: basics", "[script]") {
    WorkflowContext context;

    SECTION("variable assignment and context read") {
        // TurboScript uses '=' for assignment.
        // We use ctx_set to write to workflow context.
        auto r = Praktor::Script::execute("x = 10; ctx_set(\"x_val\", x);", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("x_val") == 10.0);
    }

    SECTION("arithmetic") {
        auto r = Praktor::Script::execute("ctx_set(\"res\", 2 + 3 * 4);", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("res") == 14.0);
    }
}

TEST_CASE("Script engine: control flow", "[script]") {
    WorkflowContext context;

    SECTION("while loop") {
        auto r = Praktor::Script::execute(R"(
            i = 0;
            sum = 0;
            while (i < 5) {
                sum = sum + i;
                i = i + 1;
            }
            ctx_set("sum", sum);
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("sum") == 10.0);
    }

    SECTION("if/else") {
        auto r = Praktor::Script::execute(R"(
            x = 10;
            if (x > 5) {
                ctx_set("branch", "then");
            } else {
                ctx_set("branch", "else");
            }
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("branch") == "then");
    }
}

TEST_CASE("Script engine: context integration", "[script]") {
    WorkflowContext context;
    context.setValue("input", 42.0);
    context.setValue("BUILD_TARGET", "all");
    context.setValue("variables.BUILD_TARGET", "all");

    SECTION("ctx_get reads from context") {
        auto r = Praktor::Script::execute(R"(
            val = ctx_get("input");
            ctx_set("output", val * 2);
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("output") == 84.0);
    }

    SECTION("dot syntax is normalized before execution") {
        auto r = Praktor::Script::execute(R"(
            val = ctx.get("input");
            ctx.set("copied", val);
            log.info("copied input");
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("copied") == 42.0);
    }

    SECTION("ctx.get returns stable strings") {
        auto r = Praktor::Script::execute(R"(
            target = ctx.get("BUILD_TARGET");
            ctx.set("copied_target", target);
            log.info("Build of target '" + target + "' completed successfully.");
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("copied_target") == "all");
    }

    SECTION("ctx.set stores structured values") {
        auto r = Praktor::Script::execute(R"(
            result = {};
            result.name = "demo";
            result.code = 7;
            items = [];
            items.push("one");
            items.push("two");
            result.items = items;
            ctx.set("payload", json.stringify(result));
        )", context);
        REQUIRE(r.success);
        auto payload = context.getValueByPath("payload");
        CHECK(payload["name"].as<std::string>() == "demo");
        CHECK(payload["code"].as<int>() == 7);
        CHECK(payload["items"][0].as<std::string>() == "one");
        CHECK(payload["items"][1].as<std::string>() == "two");
    }

    SECTION("ctx.output stores structured task outputs") {
        context.pushTaskScope("emit");
        auto r = Praktor::Script::execute(R"(
            result = {};
            result.status = "ok";
            result.value = 3;
            ctx.output("payload", json.stringify(result));
        )", context);
        context.popTaskScope();
        REQUIRE(r.success);
        auto payload = context.getValueByPath("tasks.emit.outputs.payload");
        CHECK(payload["status"].as<std::string>() == "ok");
        CHECK(payload["value"].as<int>() == 3);
    }
}

TEST_CASE("Script engine: builtins and imported modules", "[script]") {
    WorkflowContext context;

    SECTION("math functions are available by default") {
        auto r = Praktor::Script::execute(R"(
            ctx_set("s", sin(0));
            ctx_set("c", cos(0));
        )", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("s") == Catch::Approx(0.0));
        CHECK(context.getValue<double>("c") == Catch::Approx(1.0));
    }

    SECTION("json query (from feeds)") {
        auto r = Praktor::Script::execute(R"(
            import("feeds");
            js = "{\"score\": 42}";
            res = json.query(js, "score");
            ctx_set("score", res);
        )", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("score") == Catch::Approx(42.0));
    }
}
