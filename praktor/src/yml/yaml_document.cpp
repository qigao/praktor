#include "yaml_document.hpp"

#include <charconv>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace TaskYamlDetail {

namespace {

struct JsonValueDeleter {
    void operator()(json_value_t* value) const noexcept {
        auto* document = reinterpret_cast<turbo_json_doc_t*>(value);
        turbo_free_json(&document);
    }
};

turbo_yaml_node_t* resolve_alias(turbo_yaml_node_t* node) noexcept {
    while (node && turbo_yaml_node_type(node) == TURBO_YAML_NODE_ALIAS) {
        turbo_yaml_node_t* target = turbo_yaml_alias_target(node);
        if (!target || target == node) {
            break;
        }
        node = target;
    }
    return node;
}

std::string scalar_text(const turbo_yaml_doc_t* document, turbo_yaml_node_t* node) {
    node = resolve_alias(node);
    if (!document || !node || turbo_yaml_node_type(node) != TURBO_YAML_NODE_SCALAR) {
        throw std::runtime_error("Expected YAML scalar");
    }

    std::unique_ptr<char, decltype(&turbo_yaml_string_free)> value(
        turbo_yaml_scalar_dup(document, node), &turbo_yaml_string_free);
    if (!value) {
        throw std::runtime_error("Failed to decode YAML scalar");
    }
    return std::string(value.get());
}

bool find_location(turbo_yaml_node_t* node, turbo_yaml_location_t& location,
                   std::size_t depth = 0) noexcept {
    constexpr std::size_t max_location_search_depth = 32;
    node = resolve_alias(node);
    if (!node || depth >= max_location_search_depth) {
        return false;
    }

    turbo_yaml_location_t candidate{};
    if (turbo_yaml_node_location(node, &candidate) && candidate.start_line > 0) {
        location = candidate;
        return true;
    }

    if (turbo_yaml_node_type(node) == TURBO_YAML_NODE_MAPPING &&
        turbo_yaml_mapping_size(node) > 0) {
        return find_location(turbo_yaml_mapping_key(node, 0), location, depth + 1);
    }
    if (turbo_yaml_node_type(node) == TURBO_YAML_NODE_SEQUENCE &&
        turbo_yaml_sequence_size(node) > 0) {
        return find_location(turbo_yaml_sequence_get(node, 0), location, depth + 1);
    }
    return false;
}

} // namespace

YamlNodeRef::YamlNodeRef(const turbo_yaml_doc_t* document, turbo_yaml_node_t* node,
                         turbo_yaml_node_t* key_node) noexcept
    : document_(document), node_(node), key_node_(key_node) {}

turbo_yaml_node_t* YamlNodeRef::resolved_node() const noexcept {
    return resolve_alias(node_);
}

bool YamlNodeRef::valid() const noexcept {
    return document_ && resolved_node();
}

bool YamlNodeRef::is_map() const noexcept {
    turbo_yaml_node_t* node = resolved_node();
    return node && turbo_yaml_node_type(node) == TURBO_YAML_NODE_MAPPING;
}

bool YamlNodeRef::is_seq() const noexcept {
    turbo_yaml_node_t* node = resolved_node();
    return node && turbo_yaml_node_type(node) == TURBO_YAML_NODE_SEQUENCE;
}

bool YamlNodeRef::has_val() const noexcept {
    turbo_yaml_node_t* node = resolved_node();
    return node && turbo_yaml_node_type(node) == TURBO_YAML_NODE_SCALAR;
}

bool YamlNodeRef::has_child(const char* key) const {
    return is_map() && key && turbo_yaml_mapping_contains(document_, resolved_node(), key);
}

std::size_t YamlNodeRef::num_children() const noexcept {
    turbo_yaml_node_t* node = resolved_node();
    if (!node) {
        return 0;
    }
    if (is_map()) {
        return turbo_yaml_mapping_size(node);
    }
    if (is_seq()) {
        return turbo_yaml_sequence_size(node);
    }
    return 0;
}

YamlNodeRef YamlNodeRef::first_child() const noexcept {
    return child_at(0);
}

YamlNodeRef YamlNodeRef::operator[](const char* key) const {
    if (!is_map() || !key) {
        return {};
    }
    turbo_yaml_node_t* mapping = resolved_node();
    turbo_yaml_node_t* value = turbo_yaml_mapping_get(document_, mapping, key);
    if (!value) {
        return {};
    }

    turbo_yaml_node_t* matched_key = nullptr;
    const std::size_t size = turbo_yaml_mapping_size(mapping);
    for (std::size_t index = 0; index < size; ++index) {
        turbo_yaml_node_t* candidate = turbo_yaml_mapping_key(mapping, index);
        if (turbo_yaml_node_type(resolve_alias(candidate)) == TURBO_YAML_NODE_SCALAR &&
            scalar_text(document_, candidate) == key) {
            matched_key = candidate;
            break;
        }
    }
    return YamlNodeRef(document_, value, matched_key);
}

std::string YamlNodeRef::key() const {
    return scalar_text(document_, key_node_);
}

std::string YamlNodeRef::scalar() const {
    return scalar_text(document_, resolved_node());
}

std::string YamlNodeRef::location() const {
    turbo_yaml_location_t location{};
    if (!find_location(key_node_, location) && !find_location(resolved_node(), location)) {
        return {};
    }
    return " at line " + std::to_string(location.start_line) + ", column " +
           std::to_string(location.start_column);
}

std::optional<bool> YamlNodeRef::bool_integer_value() const {
    turbo_yaml_node_t* node = resolved_node();
    if (!document_ || !node ||
        turbo_yaml_scalar_kind(document_, node) != TURBO_YAML_SCALAR_INT) {
        return std::nullopt;
    }

    std::unique_ptr<json_value_t, JsonValueDeleter> value(
        turbo_yaml_node_to_json(document_, node));
    if (!value || turbo_json_type(value.get()) != TURBO_JSON_NUMBER) {
        return std::nullopt;
    }

    const double number = turbo_json_number(value.get());
    if (number == 0.0) {
        return false;
    }
    if (number == 1.0) {
        return true;
    }
    return std::nullopt;
}

const YamlNodeRef& YamlNodeRef::operator>>(std::string& value) const {
    value = scalar();
    return *this;
}

const YamlNodeRef& YamlNodeRef::operator>>(int& value) const {
    const std::string text = scalar();
    int parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error("Expected YAML integer" + location());
    }
    value = parsed;
    return *this;
}

YamlNodeRef YamlNodeRef::child_at(std::size_t index) const noexcept {
    turbo_yaml_node_t* node = resolved_node();
    if (!node) {
        return {};
    }
    if (is_map() && index < turbo_yaml_mapping_size(node)) {
        return YamlNodeRef(document_, turbo_yaml_mapping_value(node, index),
                           turbo_yaml_mapping_key(node, index));
    }
    if (is_seq() && index < turbo_yaml_sequence_size(node)) {
        return YamlNodeRef(document_, turbo_yaml_sequence_get(node, index));
    }
    return {};
}

YamlNodeRef::Iterator YamlNodeRef::begin() const noexcept {
    return Iterator(*this, 0);
}

YamlNodeRef::Iterator YamlNodeRef::end() const noexcept {
    return Iterator(*this, num_children());
}

YamlNodeRef::Iterator::Iterator(YamlNodeRef parent, std::size_t index) noexcept
    : parent_(parent), index_(index) {}

YamlNodeRef YamlNodeRef::Iterator::operator*() const noexcept {
    return parent_.child_at(index_);
}

YamlNodeRef::Iterator& YamlNodeRef::Iterator::operator++() noexcept {
    ++index_;
    return *this;
}

bool YamlNodeRef::Iterator::operator==(const Iterator& other) const noexcept {
    return parent_.document_ == other.parent_.document_ &&
           parent_.node_ == other.parent_.node_ && index_ == other.index_;
}

bool YamlNodeRef::Iterator::operator!=(const Iterator& other) const noexcept {
    return !(*this == other);
}

YamlDocument::YamlDocument(std::string_view source) {
    turbo_yaml_error_t error{};
    const int status = turbo_parse_yaml_ex(
        reinterpret_cast<const uint8_t*>(source.data()), source.size(), &document_, &error);
    if (status != 0 || !document_) {
        std::string message = error.message[0] ? error.message : "Invalid YAML input";
        if (error.location.start_line > 0) {
            message += " at line " + std::to_string(error.location.start_line) +
                       ", column " + std::to_string(error.location.start_column);
        }
        throw std::runtime_error(message);
    }
}

YamlDocument::~YamlDocument() {
    turbo_free_yaml(&document_);
}

YamlDocument::YamlDocument(YamlDocument&& other) noexcept
    : document_(std::exchange(other.document_, nullptr)) {}

YamlDocument& YamlDocument::operator=(YamlDocument&& other) noexcept {
    if (this != &other) {
        turbo_free_yaml(&document_);
        document_ = std::exchange(other.document_, nullptr);
    }
    return *this;
}

YamlNodeRef YamlDocument::root() const noexcept {
    return YamlNodeRef(document_, turbo_yaml_root(document_));
}

} // namespace TaskYamlDetail
