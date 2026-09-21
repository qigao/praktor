#include "data/workflow_value.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace {

json_value_t* cloneOrThrow(const json_value_t* value) {
    json_value_t* clone = json_clone(value);
    if (!clone) {
        throw std::bad_alloc();
    }
    return clone;
}

void freeJson(json_value_t*& value) noexcept {
    json_free(value);
    value = nullptr;
}

struct SerializedJsonDeleter {
    void operator()(char* text) const noexcept { json_serialize_free(text); }
};

} // namespace

WorkflowValueMember::WorkflowValueMember(std::string key, WorkflowValue value)
    : key_(std::move(key)), value_(std::move(value)) {}

WorkflowValue::WorkflowValue()
    : WorkflowValue(nullptr) {}

WorkflowValue::WorkflowValue(std::nullptr_t)
    : value_(json_create_null()) {
    requireValue(value_, "create null");
}

WorkflowValue::WorkflowValue(bool value)
    : value_(json_create_bool(value)) {
    requireValue(value_, "create boolean");
}

WorkflowValue::WorkflowValue(const char* value)
    : value_(json_create_string(value ? value : "")) {
    requireValue(value_, "create string");
}

WorkflowValue::WorkflowValue(std::string value)
    : WorkflowValue(std::string_view(value)) {}

WorkflowValue::WorkflowValue(std::string_view value)
    : value_(json_create_string_n(value.data(), value.size())) {
    requireValue(value_, "create string");
}

WorkflowValue::WorkflowValue(double value)
    : value_(json_create_number(value)) {
    requireValue(value_, "create number");
}

WorkflowValue::WorkflowValue(const WorkflowValue& other)
    : value_(cloneOrThrow(other.value_)) {}

WorkflowValue::WorkflowValue(WorkflowValue&& other) noexcept
    : value_(std::exchange(other.value_, nullptr)) {}

WorkflowValue& WorkflowValue::operator=(const WorkflowValue& other) {
    if (this != &other) {
        json_value_t* replacement = cloneOrThrow(other.value_);
        reset(replacement);
    }
    return *this;
}

WorkflowValue& WorkflowValue::operator=(WorkflowValue&& other) noexcept {
    if (this != &other) {
        reset(std::exchange(other.value_, nullptr));
    }
    return *this;
}

WorkflowValue::~WorkflowValue() noexcept {
    reset();
}

WorkflowValue::WorkflowValue(json_value_t* value, AdoptTag)
    : value_(value) {
    requireValue(value_, "adopt JSON value");
}

WorkflowValue WorkflowValue::parse(std::string_view input) {
    json_value_t* document = json_parse(input.data(), input.size());
    if (!document) {
        throw std::invalid_argument("Invalid JSON input");
    }
    return WorkflowValue(document, AdoptTag{});
}

WorkflowValue WorkflowValue::object() {
    return WorkflowValue(json_create_object(), AdoptTag{});
}

WorkflowValue WorkflowValue::object(
    std::initializer_list<std::pair<std::string, WorkflowValue>> members) {
    WorkflowValue result = object();
    for (const auto& [key, value] : members) {
        result.set(key, value);
    }
    return result;
}

WorkflowValue WorkflowValue::array() {
    return WorkflowValue(json_create_array(), AdoptTag{});
}

WorkflowValue WorkflowValue::array(std::initializer_list<WorkflowValue> values) {
    WorkflowValue result = array();
    for (const auto& value : values) {
        result.push_back(value);
    }
    return result;
}

WorkflowValue WorkflowValue::null() {
    return WorkflowValue(nullptr);
}

WorkflowValue WorkflowValue::adoptJson(json_value_t* value) {
    return WorkflowValue(value, AdoptTag{});
}

WorkflowValue WorkflowValue::copyJson(const json_value_t* value) {
    if (!value) {
        return null();
    }
    return cloneBorrowed(value);
}

bool WorkflowValue::is_null() const noexcept {
    return !value_ || json_is_null(value_);
}

bool WorkflowValue::is_bool() const noexcept {
    return value_ && json_type(value_) == JSON_BOOL;
}

bool WorkflowValue::is_number() const noexcept {
    return value_ && json_type(value_) == JSON_NUMBER;
}

bool WorkflowValue::is_int64() const noexcept {
    if (!is_number()) {
        return false;
    }
    size_t length = 0;
    const char* token = json_number_text(value_, &length);
    if (!token || length == 0 || std::string_view(token, length).find_first_of(".eE") !=
                                    std::string_view::npos) {
        return false;
    }
    int64_t parsed{};
    const auto result = std::from_chars(token, token + length, parsed);
    return result.ec == std::errc{} && result.ptr == token + length;
}

bool WorkflowValue::is_uint64() const noexcept {
    if (!is_number()) {
        return false;
    }
    size_t length = 0;
    const char* token = json_number_text(value_, &length);
    if (!token || length == 0 || token[0] == '-' ||
        std::string_view(token, length).find_first_of(".eE") != std::string_view::npos) {
        return false;
    }
    uint64_t parsed{};
    const auto result = std::from_chars(token, token + length, parsed);
    return result.ec == std::errc{} && result.ptr == token + length;
}

bool WorkflowValue::is_double() const noexcept {
    return is_number() && !is_int64() && !is_uint64();
}

bool WorkflowValue::is_string() const noexcept {
    return value_ && json_type(value_) == JSON_STRING;
}

bool WorkflowValue::is_array() const noexcept {
    return value_ && json_type(value_) == JSON_ARRAY;
}

bool WorkflowValue::is_object() const noexcept {
    return value_ && json_type(value_) == JSON_OBJECT;
}

json_type_t WorkflowValue::type() const noexcept {
    return value_ ? json_type(value_) : JSON_NULL;
}

size_t WorkflowValue::size() const noexcept {
    if (is_object()) {
        return json_object_size(value_);
    }
    if (is_array()) {
        return json_array_size(value_);
    }
    if (is_string()) {
        return json_string_len(value_);
    }
    return 0;
}

bool WorkflowValue::contains(std::string_view key) const {
    if (!is_object()) {
        return false;
    }
    const std::string owned_key(key);
    return json_object_get(value_, owned_key.c_str()) != nullptr;
}

WorkflowValue WorkflowValue::at(std::string_view key) const {
    if (!is_object()) {
        throw std::runtime_error("WorkflowValue is not an object");
    }
    const std::string owned_key(key);
    const json_value_t* child = json_object_get(value_, owned_key.c_str());
    if (!child) {
        throw std::out_of_range("WorkflowValue object key not found: " + owned_key);
    }
    return cloneBorrowed(child);
}

WorkflowValue WorkflowValue::at(size_t index) const {
    if (!is_array()) {
        throw std::runtime_error("WorkflowValue is not an array");
    }
    const json_value_t* child = json_array_get(value_, index);
    if (!child) {
        throw std::out_of_range("WorkflowValue array index out of range");
    }
    return cloneBorrowed(child);
}

WorkflowValue::Proxy WorkflowValue::operator[](std::string_view key) {
    return Proxy(*this, {std::string(key)});
}

void WorkflowValue::set(std::string_view key, const WorkflowValue& value) {
    if (!is_object()) {
        throw std::runtime_error("WorkflowValue is not an object");
    }
    json_value_t* clone = cloneOrThrow(value.value_);
    const std::string owned_key(key);
    if (!json_object_add_checked(value_, owned_key.c_str(), clone)) {
        freeJson(clone);
        throw std::runtime_error("Failed to set WorkflowValue object member");
    }
}

void WorkflowValue::set(std::string_view key, WorkflowValue&& value) {
    if (this == &value) {
        set(key, static_cast<const WorkflowValue&>(value));
        return;
    }
    if (!is_object()) {
        throw std::runtime_error("WorkflowValue is not an object");
    }
    const std::string owned_key(key);
    if (!json_object_add_checked(value_, owned_key.c_str(), value.value_)) {
        throw std::runtime_error("Failed to set WorkflowValue object member");
    }
    value.value_ = nullptr;
}

void WorkflowValue::push_back(const WorkflowValue& value) {
    if (!is_array()) {
        throw std::runtime_error("WorkflowValue is not an array");
    }
    json_value_t* clone = cloneOrThrow(value.value_);
    if (!json_array_add_checked(value_, clone)) {
        freeJson(clone);
        throw std::runtime_error("Failed to append WorkflowValue array member");
    }
}

void WorkflowValue::push_back(WorkflowValue&& value) {
    if (this == &value) {
        push_back(static_cast<const WorkflowValue&>(value));
        return;
    }
    if (!is_array()) {
        throw std::runtime_error("WorkflowValue is not an array");
    }
    if (!json_array_add_checked(value_, value.value_)) {
        throw std::runtime_error("Failed to append WorkflowValue array member");
    }
    value.value_ = nullptr;
}

void WorkflowValue::erase_at(size_t index) {
    if (!is_array() || index >= size()) {
        throw std::out_of_range("WorkflowValue array index out of range");
    }
    WorkflowValue replacement = array();
    for (size_t current = 0; current < size(); ++current) {
        if (current != index) {
            replacement.push_back(at(current));
        }
    }
    *this = std::move(replacement);
}

std::vector<WorkflowValueMember> WorkflowValue::object_range() const {
    if (!is_object()) {
        throw std::runtime_error("WorkflowValue is not an object");
    }
    std::vector<WorkflowValueMember> result;
    result.reserve(size());
    for (size_t index = 0; index < size(); ++index) {
        const char* key = json_object_key(value_, index);
        result.emplace_back(key ? key : "", cloneBorrowed(json_object_value(value_, index)));
    }
    return result;
}

std::vector<WorkflowValue> WorkflowValue::array_range() const {
    if (!is_array()) {
        throw std::runtime_error("WorkflowValue is not an array");
    }
    std::vector<WorkflowValue> result;
    result.reserve(size());
    for (size_t index = 0; index < size(); ++index) {
        result.push_back(cloneBorrowed(json_array_get(value_, index)));
    }
    return result;
}

std::string WorkflowValue::to_string() const {
    size_t length = 0;
    std::unique_ptr<char, SerializedJsonDeleter> text(json_serialize(value_, &length));
    if (!text) {
        throw std::runtime_error("Failed to serialize WorkflowValue");
    }
    return std::string(text.get(), length);
}

std::string WorkflowValue::pretty_string() const {
    size_t length = 0;
    std::unique_ptr<char, SerializedJsonDeleter> text(
        json_serialize_pretty(value_, &length));
    if (!text) {
        throw std::runtime_error("Failed to pretty-print WorkflowValue");
    }
    return std::string(text.get(), length);
}

WorkflowValue WorkflowValue::cloneBorrowed(const json_value_t* value) {
    return WorkflowValue(cloneOrThrow(value), AdoptTag{});
}

void WorkflowValue::requireValue(const json_value_t* value, std::string_view operation) {
    if (!value) {
        throw std::runtime_error("Turbo Parser failed to " + std::string(operation));
    }
}

json_value_t* WorkflowValue::findPath(const std::vector<std::string>& keys) const {
    json_value_t* current = value_;
    for (const auto& key : keys) {
        if (!current || json_type(current) != JSON_OBJECT) {
            return nullptr;
        }
        current = json_object_get(current, key.c_str());
    }
    return current;
}

json_value_t* WorkflowValue::ensureObjectPath(const std::vector<std::string>& keys, size_t count) {
    if (is_null()) {
        json_value_t* object = json_create_object();
        requireValue(object, "create root object");
        reset(object);
    }
    json_value_t* current = value_;
    for (size_t index = 0; index < count; ++index) {
        const std::string& key = keys[index];
        if (!current || json_type(current) != JSON_OBJECT) {
            throw std::runtime_error("WorkflowValue path traverses a non-object value");
        }
        json_value_t* child = json_object_get(current, key.c_str());
        if (!child) {
            json_value_t* created = json_create_object();
            requireValue(created, "create nested object");
            if (!json_object_add_checked(current, key.c_str(), created)) {
                freeJson(created);
                throw std::runtime_error("Failed to create nested WorkflowValue object");
            }
            child = json_object_get(current, key.c_str());
        }
        current = child;
    }
    return current;
}

void WorkflowValue::setPath(const std::vector<std::string>& keys, const WorkflowValue& value) {
    if (keys.empty()) {
        *this = value;
        return;
    }
    json_value_t* parent = ensureObjectPath(keys, keys.size() - 1);
    const std::string& key = keys.back();
    json_value_t* clone = cloneOrThrow(value.value_);
    if (!json_object_add_checked(parent, key.c_str(), clone)) {
        freeJson(clone);
        throw std::runtime_error("Failed to assign WorkflowValue path");
    }
}

void WorkflowValue::setPath(const std::vector<std::string>& keys, WorkflowValue&& value) {
    if (this == &value) {
        setPath(keys, static_cast<const WorkflowValue&>(value));
        return;
    }
    if (keys.empty()) {
        *this = std::move(value);
        return;
    }
    json_value_t* parent = ensureObjectPath(keys, keys.size() - 1);
    if (!json_object_add_checked(parent, keys.back().c_str(), value.value_)) {
        throw std::runtime_error("Failed to assign WorkflowValue path");
    }
    value.value_ = nullptr;
}

void WorkflowValue::appendPath(const std::vector<std::string>& keys, const WorkflowValue& value) {
    json_value_t* array = findPath(keys);
    if (!array || json_type(array) != JSON_ARRAY) {
        throw std::runtime_error("WorkflowValue path is not an array");
    }
    json_value_t* clone = cloneOrThrow(value.value_);
    if (!json_array_add_checked(array, clone)) {
        freeJson(clone);
        throw std::runtime_error("Failed to append WorkflowValue path");
    }
}

void WorkflowValue::appendPath(const std::vector<std::string>& keys, WorkflowValue&& value) {
    if (this == &value) {
        appendPath(keys, static_cast<const WorkflowValue&>(value));
        return;
    }
    json_value_t* array = findPath(keys);
    if (!array || json_type(array) != JSON_ARRAY) {
        throw std::runtime_error("WorkflowValue path is not an array");
    }
    if (!json_array_add_checked(array, value.value_)) {
        throw std::runtime_error("Failed to append WorkflowValue path");
    }
    value.value_ = nullptr;
}

void WorkflowValue::reset(json_value_t* value) noexcept {
    freeJson(value_);
    value_ = value;
}

WorkflowValue::Proxy& WorkflowValue::Proxy::operator=(const WorkflowValue& value) {
    owner_->setPath(keys_, value);
    return *this;
}

WorkflowValue::Proxy& WorkflowValue::Proxy::operator=(WorkflowValue&& value) {
    owner_->setPath(keys_, std::move(value));
    return *this;
}

WorkflowValue::Proxy WorkflowValue::Proxy::operator[](std::string_view key) const {
    auto nested = keys_;
    nested.emplace_back(key);
    return Proxy(*owner_, std::move(nested));
}

WorkflowValue WorkflowValue::Proxy::operator[](size_t index) const {
    return get().at(index);
}

WorkflowValue WorkflowValue::Proxy::get() const {
    const json_value_t* value = owner_->findPath(keys_);
    if (!value) {
        throw std::out_of_range("WorkflowValue path not found");
    }
    return WorkflowValue::cloneBorrowed(value);
}

std::vector<WorkflowValueMember> WorkflowValue::Proxy::object_range() const {
    return get().object_range();
}

void WorkflowValue::Proxy::push_back(const WorkflowValue& value) {
    owner_->appendPath(keys_, value);
}

void WorkflowValue::Proxy::push_back(WorkflowValue&& value) {
    owner_->appendPath(keys_, std::move(value));
}
