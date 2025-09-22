#pragma once

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <utility>

template <typename Derived>
class Singleton {
public:
    template <typename... Args>
    static void Construct(Args&&... args) {
#ifndef SINGLETON_INJECT_ABSTRACT_CLASS
        using Instance = Derived;
#else
        struct Dummy final : Derived {
            using Derived::Derived;

            void ProhibitConstructFromDerived() const noexcept override {}
        };

        using Instance = Dummy;
#endif   // SINGLETON_INJECT_ABSTRACT_CLASS

        if (!instance_.load(std::memory_order_acquire)) {
            std::lock_guard lock{mutex_};

            if (!instance_.load(std::memory_order_relaxed))
                instance_.store(new Instance{std::forward<Args>(args)...}, std::memory_order_release);
        }
    }

    static void Destruct() {
        if (instance_.load(std::memory_order_acquire)) {
            std::lock_guard lock{mutex_};

            if (auto* instance = instance_.load(std::memory_order_relaxed); instance) {
                delete instance;
                instance_.store(nullptr, std::memory_order_release);
            }
        }
    }

    static Derived* GetInstance() {
        auto* instance = instance_.load(std::memory_order_acquire);

        if (!instance) {
            std::shared_lock lock{mutex_};

            instance = instance_.load(std::memory_order_relaxed);
        }

        return instance;
    }

protected:
    Singleton() = default;
    Singleton(Singleton const&) = delete;
    Singleton(Singleton&&) = delete;
    Singleton& operator=(Singleton const&) = delete;
    Singleton& operator=(Singleton&&) = delete;
#ifndef SINGLETON_INJECT_ABSTRACT_CLASS
    ~Singleton() = default;
#else
    virtual ~SingletonDclp() = default;
#endif   // SINGLETON_INJECT_ABSTRACT_CLASS

private:
#ifdef SINGLETON_INJECT_ABSTRACT_CLASS
    virtual void ProhibitConstructFromDerived() const noexcept = 0;
#endif   // SINGLETON_INJECT_ABSTRACT_CLASS

    inline static std::atomic<Derived*> instance_{nullptr};
    inline static std::shared_mutex mutex_;
};
