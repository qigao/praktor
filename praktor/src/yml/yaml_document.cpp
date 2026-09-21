#include "yaml_document.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace TaskYamlDetail {

namespace {

cyaml_node_t* resolve_alias(cyaml_node_t* node) noexcept {
    while (node && cyaml_is_alias(node)) {
        cyaml_node_t* target = node->alias.target;
        if (!target || target == node) {
            break;
        }
        node = target;
    }
    return node;
}

std::string scalar_text(const cyaml_doc_t* document, cyaml_node_t* node) {
    node = resolve_alias(node);
    if (!document || !node || !cyaml_is_scalar(node)) {
        throw std::runtime_error("Expected YAML scalar");
    }

    std::unique_ptr<char, decltype(&std::free)> value(
        cyaml_scalar_str(document, node), &std::free);
    if (!value) {
        throw std::runtime_error("Failed to decode YAML scalar");
    }
    return std::string(value.get());
}

bool find_location(cyaml_node_t* node, cyaml_span_t& location,
                   std::size_t depth = 0) noexcept {
    constexpr std::size_t max_location_search_depth = 32;
    node = resolve_alias(node);
    if (!node || depth >= max_location_search_depth) {
        return false;
    }

    if (node->span.start_line > 0) {
        location = node->span;
        return true;
    }

    if (cyaml_is_map(node) && cyaml_map_len(node) > 0) {
        const cyaml_pair_t* pair = cyaml_map_at(node, 0);
        return pair && find_location(pair->key, location, depth + 1);
    }
    if (cyaml_is_seq(node) && cyaml_seq_len(node) > 0) {
        return find_location(cyaml_seq_get(node, 0), location, depth + 1);
    }
    return false;
}

} // namespace

YamlNodeRef::YamlNodeRef(const cyaml_doc_t* document, cyaml_node_t* node,
                         cyaml_node_t* key_node) noexcept
    : document_(document), node_(node), key_node_(key_node) {}

cyaml_node_t* YamlNodeRef::resolved_node() const noexcept {
    return resolve_alias(node_);
}

bool YamlNodeRef::valid() const noexcept {
    return document_ && resolved_node();
}

bool YamlNodeRef::is_map() const noexcept {
    return cyaml_is_map(resolved_node());
}

bool YamlNodeRef::is_seq() const noexcept {
    return cyaml_is_seq(resolved_node());
}

bool YamlNodeRef::has_val() const noexcept {
    return cyaml_is_scalar(resolved_node());
}

bool YamlNodeRef::is_string_scalar() const noexcept {
    cyaml_node_t* node = resolved_node();
    return document_ && node && cyaml_is_scalar(node) &&
           cyaml_scalar_kind(document_, node) == CYAML_KIND_STRING;
}

bool YamlNodeRef::has_child(const char* key) const {
    return is_map() && key && cyaml_has(document_, resolved_node(), key);
}

std::size_t YamlNodeRef::num_children() const noexcept {
    cyaml_node_t* node = resolved_node();
    if (!node) {
        return 0;
    }
    if (cyaml_is_map(node)) {
        return cyaml_map_len(node);
    }
    if (cyaml_is_seq(node)) {
        return cyaml_seq_len(node);
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
    cyaml_node_t* mapping = resolved_node();
    cyaml_node_t* value = cyaml_get(document_, mapping, key);
    if (!value) {
        return {};
    }

    cyaml_node_t* matched_key = nullptr;
    const std::size_t size = cyaml_map_len(mapping);
    for (std::size_t index = 0; index < size; ++index) {
        cyaml_pair_t* pair = cyaml_map_at(mapping, static_cast<std::uint32_t>(index));
        if (pair && cyaml_is_scalar(resolve_alias(pair->key)) &&
            scalar_text(document_, pair->key) == key) {
            matched_key = pair->key;
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
    cyaml_span_t location{};
    if (!find_location(key_node_, location) && !find_location(resolved_node(), location)) {
        return {};
    }
    return " at line " + std::to_string(location.start_line) + ", column " +
           std::to_string(location.start_col);
}

std::optional<bool> YamlNodeRef::bool_integer_value() const {
    cyaml_node_t* node = resolved_node();
    if (!document_ || !node ||
        cyaml_scalar_kind(document_, node) != CYAML_KIND_INT) {
        return std::nullopt;
    }

    std::int64_t number = 0;
    if (!cyaml_as_int(document_, node, &number)) {
        return std::nullopt;
    }
    if (number == 0) {
        return false;
    }
    if (number == 1) {
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
    cyaml_node_t* node = resolved_node();
    if (!node) {
        return {};
    }
    if (cyaml_is_map(node) && index < cyaml_map_len(node)) {
        cyaml_pair_t* pair = cyaml_map_at(node, static_cast<std::uint32_t>(index));
        return pair ? YamlNodeRef(document_, pair->val, pair->key) : YamlNodeRef{};
    }
    if (cyaml_is_seq(node) && index < cyaml_seq_len(node)) {
        return YamlNodeRef(document_, cyaml_seq_get(node, static_cast<std::uint32_t>(index)));
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

YamlDocument::YamlDocument(std::string_view source)
    : source_(std::make_unique<std::string>(source)) {
    cyaml_error_t error{};
    document_ = cyaml_parse(source_->data(), source_->size(), nullptr, &error);
    if (!document_) {
        std::string message = error.msg[0] ? error.msg : "Invalid YAML input";
        if (error.span.start_line > 0) {
            message += " at line " + std::to_string(error.span.start_line) +
                       ", column " + std::to_string(error.span.start_col);
        }
        throw std::runtime_error(message);
    }
}

YamlDocument::~YamlDocument() {
    cyaml_free(document_);
}

YamlDocument::YamlDocument(YamlDocument&& other) noexcept
    : source_(std::move(other.source_)),
      document_(std::exchange(other.document_, nullptr)) {}

YamlDocument& YamlDocument::operator=(YamlDocument&& other) noexcept {
    if (this != &other) {
        cyaml_free(document_);
        source_ = std::move(other.source_);
        document_ = std::exchange(other.document_, nullptr);
    }
    return *this;
}

YamlNodeRef YamlDocument::root() const noexcept {
    return YamlNodeRef(document_, cyaml_root(document_));
}

} // namespace TaskYamlDetail
