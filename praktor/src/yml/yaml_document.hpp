#pragma once

#include "yml/yaml_document.h"

#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace TaskYamlDetail {

class YamlNodeRef {
public:
    class Iterator;

    YamlNodeRef() = default;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool is_map() const noexcept;
    [[nodiscard]] bool is_seq() const noexcept;
    [[nodiscard]] bool has_val() const noexcept;
    [[nodiscard]] bool is_string_scalar() const noexcept;
    [[nodiscard]] bool has_child(const char* key) const;
    [[nodiscard]] std::size_t num_children() const noexcept;
    [[nodiscard]] YamlNodeRef first_child() const noexcept;
    [[nodiscard]] YamlNodeRef operator[](const char* key) const;
    [[nodiscard]] std::string key() const;
    [[nodiscard]] std::string scalar() const;
    [[nodiscard]] std::string location() const;
    [[nodiscard]] std::optional<bool> bool_integer_value() const;
    [[nodiscard]] praktor_yaml_node native() const noexcept;

    const YamlNodeRef& operator>>(std::string& value) const;
    const YamlNodeRef& operator>>(int& value) const;

    [[nodiscard]] Iterator begin() const noexcept;
    [[nodiscard]] Iterator end() const noexcept;

private:
    friend class YamlDocument;
    friend class Iterator;

    explicit YamlNodeRef(praktor_yaml_node reference) noexcept;
    [[nodiscard]] YamlNodeRef child_at(std::size_t index) const noexcept;

    praktor_yaml_node reference_{};
};

class YamlNodeRef::Iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = YamlNodeRef;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = YamlNodeRef;

    Iterator() = default;
    Iterator(YamlNodeRef parent, std::size_t index) noexcept;

    [[nodiscard]] YamlNodeRef operator*() const noexcept;
    Iterator& operator++() noexcept;
    [[nodiscard]] bool operator==(const Iterator& other) const noexcept;
    [[nodiscard]] bool operator!=(const Iterator& other) const noexcept;

private:
    YamlNodeRef parent_;
    std::size_t index_ = 0;
};

class YamlDocument {
public:
    explicit YamlDocument(std::string_view source);
    ~YamlDocument();

    YamlDocument(const YamlDocument&) = delete;
    YamlDocument& operator=(const YamlDocument&) = delete;
    YamlDocument(YamlDocument&& other) noexcept;
    YamlDocument& operator=(YamlDocument&& other) noexcept;

    [[nodiscard]] YamlNodeRef root() const noexcept;

private:
    praktor_yaml_document* document_ = nullptr;
};

// Temporary exception/string boundary; field grammar and limits live in C.
std::string read_scalar_or_throw(const YamlNodeRef& node, const std::string& message);
std::string read_system_action_string_or_throw(const YamlNodeRef& node, const std::string& message);
bool read_bool_or_throw(const YamlNodeRef& node, const char* field_name);
bool read_force_terminate_or_throw(const YamlNodeRef& node);

} // namespace TaskYamlDetail
