#include "yaml_document.hpp"
#include "yml/yaml_value.h"
#include "yml/yaml_contract.h"
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace TaskYamlDetail {
namespace {
struct TextDeleter { void operator()(char* text) const noexcept { tstr_free(text); } };
void check_status(int status, const char* error,
    praktor_yaml_failure origin = PRAKTOR_YAML_FAILURE_INPUT) {
    if (status == SALTS_OK) return;
    if (origin == PRAKTOR_YAML_FAILURE_ALLOCATION) throw std::bad_alloc();
    throw std::runtime_error(error);
}
std::string text_result(int status, tstr text, const char* error,
    praktor_yaml_failure origin = PRAKTOR_YAML_FAILURE_INPUT) {
    std::unique_ptr<char, TextDeleter> owner(text);
    check_status(status, error, origin);
    return std::string(owner.get(), tstr_len(owner.get()));
}
} // namespace

YamlNodeRef::YamlNodeRef(praktor_yaml_node reference) noexcept : reference_(reference) {}
praktor_yaml_node YamlNodeRef::native() const noexcept { return reference_; }
bool YamlNodeRef::valid() const noexcept { return praktor_yaml_valid(reference_); }
bool YamlNodeRef::is_map() const noexcept { return praktor_yaml_is_map(reference_); }
bool YamlNodeRef::is_seq() const noexcept { return praktor_yaml_is_seq(reference_); }
bool YamlNodeRef::has_val() const noexcept { return praktor_yaml_has_val(reference_); }
bool YamlNodeRef::is_string_scalar() const noexcept { return praktor_yaml_is_string_scalar(reference_); }
bool YamlNodeRef::has_child(const char* key) const { return praktor_yaml_has(reference_, key); }
std::size_t YamlNodeRef::num_children() const noexcept { return praktor_yaml_count(reference_); }
YamlNodeRef YamlNodeRef::first_child() const noexcept { return child_at(0); }

YamlNodeRef YamlNodeRef::operator[](const char* key) const {
    praktor_yaml_node result{};
    char error[256];
    praktor_yaml_diagnostic diagnostic{error, sizeof(error), PRAKTOR_YAML_FAILURE_NONE};
    const int status = praktor_yaml_get(reference_, key, &result, &diagnostic);
    if (status == SALTS_ENOENT) return {};
    check_status(status, error, diagnostic.origin);
    return YamlNodeRef(result);
}

std::string YamlNodeRef::key() const {
    tstr text = nullptr;
    char error[256];
    praktor_yaml_diagnostic diagnostic{error, sizeof(error), PRAKTOR_YAML_FAILURE_NONE};
    const int status = praktor_yaml_key(reference_, &text, &diagnostic);
    return text_result(status, text, error, diagnostic.origin);
}
std::string YamlNodeRef::scalar() const {
    tstr text = nullptr;
    char error[256];
    praktor_yaml_diagnostic diagnostic{error, sizeof(error), PRAKTOR_YAML_FAILURE_NONE};
    const int status = praktor_yaml_scalar(reference_, &text, &diagnostic);
    return text_result(status, text, error, diagnostic.origin);
}
std::string YamlNodeRef::location() const {
    tstr text = nullptr;
    const int status = praktor_yaml_location(reference_, &text);
    if (status == SALTS_ENOMEM) throw std::bad_alloc();
    return text_result(status, text, "Failed to locate YAML node");
}
std::optional<bool> YamlNodeRef::bool_integer_value() const {
    bool result = false;
    if (praktor_yaml_bool_integer(reference_, &result) != SALTS_OK) return std::nullopt;
    return result;
}
const YamlNodeRef& YamlNodeRef::operator>>(std::string& value) const {
    value = scalar(); return *this;
}
const YamlNodeRef& YamlNodeRef::operator>>(int& value) const {
    char error[256];
    praktor_yaml_diagnostic diagnostic{error, sizeof(error), PRAKTOR_YAML_FAILURE_NONE};
    const int status = praktor_yaml_integer(reference_, &value, &diagnostic);
    check_status(status, error, diagnostic.origin);
    return *this;
}
YamlNodeRef YamlNodeRef::child_at(std::size_t index) const noexcept {
    return YamlNodeRef(praktor_yaml_at(reference_, index));
}
YamlNodeRef::Iterator YamlNodeRef::begin() const noexcept { return Iterator(*this, 0); }
YamlNodeRef::Iterator YamlNodeRef::end() const noexcept { return Iterator(*this, num_children()); }
YamlNodeRef::Iterator::Iterator(YamlNodeRef parent, std::size_t index) noexcept : parent_(parent), index_(index) {}
YamlNodeRef YamlNodeRef::Iterator::operator*() const noexcept { return parent_.child_at(index_); }
YamlNodeRef::Iterator& YamlNodeRef::Iterator::operator++() noexcept { ++index_; return *this; }
bool YamlNodeRef::Iterator::operator==(const Iterator& other) const noexcept {
    return parent_.reference_.document == other.parent_.reference_.document &&
        parent_.reference_.node == other.parent_.reference_.node && index_ == other.index_;
}
bool YamlNodeRef::Iterator::operator!=(const Iterator& other) const noexcept { return !(*this == other); }

YamlDocument::YamlDocument(std::string_view source) {
    char error[256];
    praktor_yaml_diagnostic diagnostic{error, sizeof(error), PRAKTOR_YAML_FAILURE_NONE};
    const int status = praktor_yaml_document_create(vstr_from_buf(source.data(), source.size()),
        &document_, &diagnostic);
    check_status(status, error, diagnostic.origin);
}
YamlDocument::~YamlDocument() { praktor_yaml_document_destroy(document_); }
YamlDocument::YamlDocument(YamlDocument&& other) noexcept : document_(std::exchange(other.document_, nullptr)) {}
YamlDocument& YamlDocument::operator=(YamlDocument&& other) noexcept {
    if (this != &other) {
        praktor_yaml_document_destroy(document_);
        document_ = std::exchange(other.document_, nullptr);
    }
    return *this;
}
YamlNodeRef YamlDocument::root() const noexcept { return YamlNodeRef(praktor_yaml_root(document_)); }

namespace {
struct FieldError {
    praktor_yaml_field_error value{};
    ~FieldError() { praktor_yaml_field_error_dispose(&value); }
    FieldError() = default;
    FieldError(const FieldError&) = delete;
    FieldError& operator=(const FieldError&) = delete;
    void check(int status) const {
        if (status == SALTS_OK) return;
        if (value.origin == PRAKTOR_YAML_FAILURE_ALLOCATION) throw std::bad_alloc();
        throw std::runtime_error(std::string(value.text, tstr_len(value.text)));
    }
};
using FieldTextReader = int (*)(praktor_yaml_node, vstr, tstr*, praktor_yaml_field_error*);
std::string field_text(const YamlNodeRef& node, const std::string& message, FieldTextReader read) {
    FieldError error;
    tstr text = nullptr;
    const int status = read(node.native(), vstr_from_buf(message.data(), message.size()), &text, &error.value);
    std::unique_ptr<char, TextDeleter> owner(text);
    error.check(status);
    return std::string(owner.get(), tstr_len(owner.get()));
}
bool field_boolean(const YamlNodeRef& node, const char* name, praktor_yaml_boolean_policy policy) {
    FieldError error;
    bool value = false;
    const int status = praktor_yaml_field_boolean(node.native(), vstr_from_cstr(name), policy, &value, &error.value);
    error.check(status);
    return value;
}
} // namespace

std::string read_scalar_or_throw(const YamlNodeRef& node, const std::string& message) {
    return field_text(node, message, praktor_yaml_field_scalar);
}
std::string read_system_action_string_or_throw(const YamlNodeRef& node, const std::string& message) {
    return field_text(node, message, praktor_yaml_field_string);
}
bool read_bool_or_throw(const YamlNodeRef& node, const char* field_name) {
    return field_boolean(node, field_name, PRAKTOR_YAML_BOOLEAN_STRICT);
}
bool read_force_terminate_or_throw(const YamlNodeRef& node) {
    return field_boolean(node, "force_terminate", PRAKTOR_YAML_BOOLEAN_FORCE_TERMINATE);
}
void check_unknown_keys(const YamlNodeRef& node, const std::unordered_set<std::string>& allowed_keys) {
    std::vector<vstr> views;
    views.reserve(allowed_keys.size());
    for (const auto& key : allowed_keys) views.push_back(vstr_from_buf(key.data(), key.size()));
    FieldError error;
    const int status = praktor_yaml_check_keys(node.native(), views.data(), views.size(), &error.value);
    error.check(status);
}
json_value_t* read_contract_json_or_throw(const YamlNodeRef& node) {
    FieldError error;
    json_value_t* result = nullptr;
    const int status = praktor_yaml_value(node.native(), &result, &error.value);
    error.check(status);
    return result;
}

praktor_yaml_task_fields* read_task_fields_or_throw(const YamlNodeRef& node, praktor_yaml_task_field_kind kind) {
    FieldError error;
    praktor_yaml_task_fields* result = nullptr;
    const int status = praktor_yaml_task_fields_create(node.native(), kind, &result, &error.value);
    error.check(status);
    return result;
}
praktor_yaml_action_params* read_action_params_or_throw(const YamlNodeRef& node, praktor_yaml_action_kind kind) {
    FieldError error;
    praktor_yaml_action_params* result = nullptr;
    const int status = praktor_yaml_action_params_create(node.native(), kind, &result, &error.value);
    error.check(status);
    return result;
}

json_value_t* read_workflow_contract_fields_or_throw(
    const YamlNodeRef& node, bool output) {
    FieldError error;
    json_value_t* result = nullptr;
    const int status = praktor_yaml_contract_fields_create(
        node.native(), output, &result, &error.value);
    error.check(status);
    return result;
}

} // namespace TaskYamlDetail
