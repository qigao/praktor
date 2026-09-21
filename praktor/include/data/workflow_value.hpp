#pragma once

#include <json_parser.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

class WorkflowValue;
class WorkflowValueMember;

class WorkflowValue {
public:
    class Proxy;

    WorkflowValue();
    WorkflowValue(std::nullptr_t);
    WorkflowValue(bool value);
    WorkflowValue(const char* value);
    WorkflowValue(std::string value);
    WorkflowValue(std::string_view value);
    WorkflowValue(double value);
    WorkflowValue(float value) : WorkflowValue(static_cast<double>(value)) {}

    template <typename Integer,
              std::enable_if_t<std::is_integral_v<Integer> &&
                                   !std::is_same_v<std::remove_cv_t<Integer>, bool>,
                               int> = 0>
    WorkflowValue(Integer value)
        : value_(createInteger(value)) {
        requireValue(value_, "create integer");
    }

    template <typename T>
    explicit WorkflowValue(const std::vector<T>& values)
        : WorkflowValue(array()) {
        for (const auto& value : values) {
            push_back(WorkflowValue(value));
        }
    }

    WorkflowValue(const WorkflowValue& other);
    WorkflowValue(WorkflowValue&& other) noexcept;
    WorkflowValue& operator=(const WorkflowValue& other);
    WorkflowValue& operator=(WorkflowValue&& other) noexcept;
    ~WorkflowValue() noexcept;

    static WorkflowValue parse(std::string_view input);
    static WorkflowValue object();
    static WorkflowValue object(
        std::initializer_list<std::pair<std::string, WorkflowValue>> members);
    static WorkflowValue array();
    static WorkflowValue array(std::initializer_list<WorkflowValue> values);
    static WorkflowValue null();
    static WorkflowValue adoptJson(json_value_t* value);
    static WorkflowValue copyJson(const json_value_t* value);

    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_number() const noexcept;
    bool is_int64() const noexcept;
    bool is_uint64() const noexcept;
    bool is_double() const noexcept;
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;
    json_type_t type() const noexcept;

    bool empty() const noexcept { return size() == 0; }
    size_t size() const noexcept;
    bool contains(std::string_view key) const;

    WorkflowValue at(std::string_view key) const;
    WorkflowValue at(size_t index) const;
    WorkflowValue operator[](std::string_view key) const { return at(key); }
    WorkflowValue operator[](size_t index) const { return at(index); }
    Proxy operator[](std::string_view key);
    WorkflowValue operator[](size_t index) { return at(index); }

    void set(std::string_view key, const WorkflowValue& value);
    void set(std::string_view key, WorkflowValue&& value);
    void push_back(const WorkflowValue& value);
    void push_back(WorkflowValue&& value);
    void erase_at(size_t index);

    std::vector<WorkflowValueMember> object_range() const;
    std::vector<WorkflowValue> array_range() const;

    std::string to_string() const;
    std::string pretty_string() const;
    std::string as_string() const { return as<std::string>(); }

    template <typename T>
    T as() const {
        if constexpr (std::is_same_v<T, WorkflowValue>) {
            return *this;
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!is_string()) {
                throw std::runtime_error("WorkflowValue is not a string");
            }
            const char* text = json_string(value_);
            return std::string(text ? text : "", json_string_len(value_));
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!is_bool()) {
                throw std::runtime_error("WorkflowValue is not a boolean");
            }
            return json_bool(value_);
        } else if constexpr (std::is_floating_point_v<T>) {
            if (!is_number()) {
                throw std::runtime_error("WorkflowValue is not a number");
            }
            return static_cast<T>(json_number(value_));
        } else if constexpr (std::is_integral_v<T>) {
            return parseInteger<T>();
        } else {
            static_assert(!sizeof(T), "Unsupported WorkflowValue conversion");
        }
    }

    const json_value_t* raw() const noexcept { return value_; }

    class Proxy {
    public:
        Proxy(WorkflowValue& owner, std::vector<std::string> keys)
            : owner_(&owner), keys_(std::move(keys)) {}

        Proxy& operator=(const WorkflowValue& value);
        Proxy& operator=(WorkflowValue&& value);

        template <typename T>
        Proxy& operator=(T&& value) {
            return operator=(WorkflowValue(std::forward<T>(value)));
        }

        Proxy operator[](std::string_view key) const;
        WorkflowValue operator[](size_t index) const;
        operator WorkflowValue() const { return get(); }

        WorkflowValue get() const;
        bool is_null() const { return get().is_null(); }
        bool is_bool() const { return get().is_bool(); }
        bool is_number() const { return get().is_number(); }
        bool is_int64() const { return get().is_int64(); }
        bool is_uint64() const { return get().is_uint64(); }
        bool is_double() const { return get().is_double(); }
        bool is_string() const { return get().is_string(); }
        bool is_array() const { return get().is_array(); }
        bool is_object() const { return get().is_object(); }
        bool contains(std::string_view key) const { return get().contains(key); }
        size_t size() const { return get().size(); }
        std::string to_string() const { return get().to_string(); }
        std::vector<WorkflowValueMember> object_range() const;
        std::vector<WorkflowValue> array_range() const { return get().array_range(); }

        template <typename T>
        T as() const {
            return get().as<T>();
        }

        void push_back(const WorkflowValue& value);
        void push_back(WorkflowValue&& value);

    private:
        WorkflowValue* owner_;
        std::vector<std::string> keys_;
    };

private:
    struct AdoptTag {};

    explicit WorkflowValue(json_value_t* value, AdoptTag);
    static WorkflowValue cloneBorrowed(const json_value_t* value);
    static void requireValue(const json_value_t* value, std::string_view operation);

    template <typename Integer>
    static json_value_t* createInteger(Integer value) {
        if constexpr (std::is_signed_v<Integer>) {
            return json_create_int64(static_cast<int64_t>(value));
        } else {
            return json_create_uint64(static_cast<uint64_t>(value));
        }
    }

    template <typename Integer>
    Integer parseInteger() const {
        if (!is_number()) {
            throw std::runtime_error("WorkflowValue is not a number");
        }

        size_t length = 0;
        const char* token = json_number_text(value_, &length);
        if (token && length > 0 &&
            std::string_view(token, length).find_first_of(".eE") == std::string_view::npos) {
            Integer result{};
            const auto parsed = std::from_chars(token, token + length, result);
            if (parsed.ec == std::errc{} && parsed.ptr == token + length) {
                return result;
            }
            throw std::out_of_range("WorkflowValue integer is out of range");
        }

        const double number = json_number(value_);
        if (!std::isfinite(number) || std::trunc(number) != number ||
            number < static_cast<double>(std::numeric_limits<Integer>::lowest()) ||
            number > static_cast<double>(std::numeric_limits<Integer>::max())) {
            throw std::out_of_range("WorkflowValue number cannot be represented as an integer");
        }
        return static_cast<Integer>(number);
    }

    json_value_t* findPath(const std::vector<std::string>& keys) const;
    json_value_t* ensureObjectPath(const std::vector<std::string>& keys, size_t count);
    void setPath(const std::vector<std::string>& keys, const WorkflowValue& value);
    void setPath(const std::vector<std::string>& keys, WorkflowValue&& value);
    void appendPath(const std::vector<std::string>& keys, const WorkflowValue& value);
    void appendPath(const std::vector<std::string>& keys, WorkflowValue&& value);
    void reset(json_value_t* value = nullptr) noexcept;

    json_value_t* value_ = nullptr;
};

class WorkflowValueMember {
public:
    WorkflowValueMember(std::string key, WorkflowValue value);

    const std::string& key() const noexcept { return key_; }
    const WorkflowValue& value() const noexcept { return value_; }

private:
    std::string key_;
    WorkflowValue value_;
};
