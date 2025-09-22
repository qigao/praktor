#pragma once

#include <functional>
#include <type_traits>
#include <utility>

template <typename Fn, typename... Args>
    requires std::invocable<Fn, Args...>
decltype(auto) Callable(Fn&& fun, Args&&... args) {
    return std::invoke(std::forward<Fn>(fun), std::forward<Args>(args)...);
}

// IsTrue函数模板，用于处理所有类型
template <typename T>
bool IsTrue(T&& value) {
    if constexpr (std::is_invocable_v<T>) {
        // 如果是可调用对象，调用它并返回结果
        return std::forward<T>(value)();
    } else {
        // 否则，将其转换为bool
        return static_cast<bool>(std::forward<T>(value));
    }
}

// AllTrue函数模板，用于折叠表达式的短路求值
template <typename... Args>
bool AllTrue(Args&&... args) {
    return (... && IsTrue(std::forward<Args>(args)));
}
