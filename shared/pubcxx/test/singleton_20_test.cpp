#define ANKERL_NANOBENCH_IMPLEMENT
#include "pubcxx/singleton.hpp"

#include <catch2/catch_all.hpp>
#include <iostream>
#include <nanobench.h>

class Foo : public Singleton<Foo> {
public:
    explicit Foo(int n) : n_{n} {}

    void Bar() {}

private:
    int n_;
};

TEST_CASE("Singleton - Basic Benchmark", "[!benchmark]") {
    Foo::Construct(17);                        // Construct outside the benchmark
    auto* foo_instance = Foo::GetInstance();   // Get instance once
    foo_instance->Bar();                       // Call Bar once

    ankerl::nanobench::Bench().run("get instance bar", [&]() {
        foo_instance->Bar();   // Just benchmark the Bar function call
    });

    Foo::Destruct();   // Destruct after the benchmark
}

static std::atomic_uint32_t init{0};

class Counter : public Singleton<Counter> {
public:
    Counter() { ++init; }

    ~Counter() { --init; }   // Decrement init in destructor

    void Add() { ++count_; }

    std::uint32_t GetCount() const { return count_; }

private:
    std::atomic_uint32_t count_{0};
};

TEST_CASE("Singleton - Atomic Counter", "[!benchmark]") {
    Counter::Construct();
    auto* counter_instance = Counter::GetInstance();

    SECTION("Increment counter") {
        counter_instance->Add();
        REQUIRE(counter_instance->GetCount() == 1);
    }

    SECTION("Check init count") { REQUIRE(init == 1); }

    ankerl::nanobench::Bench().run("increment counter", [&]() { counter_instance->Add(); });

    Counter::Destruct();
    REQUIRE(init == 0);   // Verify destructor was called
}
