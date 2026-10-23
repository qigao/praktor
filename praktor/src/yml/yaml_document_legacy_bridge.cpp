#include "yaml_document.hpp"
#include <memory>
#include <stdexcept>
#include <utility>

namespace TaskYamlDetail {
namespace {
struct TextDeleter { void operator()(char* text) const noexcept { tstr_free(text); } };
void check_status(int status, const char* error) {
    if (status != SALTS_OK) throw std::runtime_error(error);
}
std::string text_result(int status, tstr text, const char* error) {
    std::unique_ptr<char, TextDeleter> owner(text);
    check_status(status, error);
    return std::string(owner.get(), tstr_len(owner.get()));
}
} // namespace

YamlNodeRef::YamlNodeRef(praktor_yaml_node reference) noexcept : reference_(reference) {}
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
    const int status = praktor_yaml_get(reference_, key, &result, error, sizeof(error));
    if (status == SALTS_ENOENT) return {};
    check_status(status, error);
    return YamlNodeRef(result);
}

std::string YamlNodeRef::key() const {
    tstr text = nullptr;
    char error[256];
    const int status = praktor_yaml_key(reference_, &text, error, sizeof(error));
    return text_result(status, text, error);
}
std::string YamlNodeRef::scalar() const {
    tstr text = nullptr;
    char error[256];
    const int status = praktor_yaml_scalar(reference_, &text, error, sizeof(error));
    return text_result(status, text, error);
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
    check_status(praktor_yaml_integer(reference_, &value, error, sizeof(error)), error);
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
    check_status(praktor_yaml_document_create(vstr_from_buf(source.data(), source.size()),
        &document_, error, sizeof(error)), error);
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
} // namespace TaskYamlDetail
