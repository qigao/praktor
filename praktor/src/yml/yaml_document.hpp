#pragma once

#include <turbo_parser.h>

#include <cstddef>
#include <iterator>
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
    [[nodiscard]] bool has_child(const char* key) const;
    [[nodiscard]] std::size_t num_children() const noexcept;
    [[nodiscard]] YamlNodeRef first_child() const noexcept;
    [[nodiscard]] YamlNodeRef operator[](const char* key) const;
    [[nodiscard]] std::string key() const;
    [[nodiscard]] std::string scalar() const;
    [[nodiscard]] std::string location() const;

    const YamlNodeRef& operator>>(std::string& value) const;
    const YamlNodeRef& operator>>(int& value) const;

    [[nodiscard]] Iterator begin() const noexcept;
    [[nodiscard]] Iterator end() const noexcept;

private:
    friend class YamlDocument;
    friend class Iterator;

    YamlNodeRef(const turbo_yaml_doc_t* document, turbo_yaml_node_t* node,
                turbo_yaml_node_t* key_node = nullptr) noexcept;
    [[nodiscard]] turbo_yaml_node_t* resolved_node() const noexcept;
    [[nodiscard]] YamlNodeRef child_at(std::size_t index) const noexcept;

    const turbo_yaml_doc_t* document_ = nullptr;
    turbo_yaml_node_t* node_ = nullptr;
    turbo_yaml_node_t* key_node_ = nullptr;
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
    turbo_yaml_doc_t* document_ = nullptr;
};

} // namespace TaskYamlDetail
