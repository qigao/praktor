#include "data/workflow_value.hpp"

#include <array>
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

void checkValue(int status, const char* error, const char* legacy_error = nullptr) {
    if (status == SALTS_OK) return;
    if (status == SALTS_ENOMEM && std::strcmp(error, "Failed to clone WorkflowValue") == 0)
        throw std::bad_alloc();
    if (status == SALTS_ENOENT) throw std::out_of_range(error);
    throw std::runtime_error(legacy_error ? legacy_error : error);
}

std::array<vstr, PRAKTOR_VALUE_MAX_PATH> pathViews(const std::vector<std::string>& keys) {
    if (keys.size() > PRAKTOR_VALUE_MAX_PATH)
        throw std::runtime_error("Invalid WorkflowValue path or source");
    std::array<vstr, PRAKTOR_VALUE_MAX_PATH> views{};
    for (size_t index = 0; index < keys.size(); ++index)
        views[index] = vstr_from_cstr(keys[index].c_str());
    return views;
}

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
    return praktor_value_is_int64(value_);
}

bool WorkflowValue::is_uint64() const noexcept {
    return praktor_value_is_uint64(value_);
}

void WorkflowValue::checkInteger(int status, const char* error) {
    if (status == SALTS_ERANGE) throw std::out_of_range(error);
    if (status != SALTS_OK) throw std::runtime_error(error);
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
    return praktor_value_size(value_);
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
    if (!is_object()) throw std::runtime_error("WorkflowValue is not an object");
    const std::string owned_key(key);
    const vstr view = vstr_from_cstr(owned_key.c_str());
    if (!value.value_) throw std::bad_alloc();
    char error[128];
    const int status = praktor_value_set_copy(&value_, &view, 1, value.value_, error, sizeof(error));
    checkValue(status, error, "Failed to set WorkflowValue object member");
}

void WorkflowValue::set(std::string_view key, WorkflowValue&& value) {
    if (!is_object()) throw std::runtime_error("WorkflowValue is not an object");
    const std::string owned_key(key);
    const vstr view = vstr_from_cstr(owned_key.c_str());
    char error[128];
    const int status = praktor_value_set_take(&value_, &view, 1, &value.value_, error, sizeof(error));
    checkValue(status, error, "Failed to set WorkflowValue object member");
}

void WorkflowValue::push_back(const WorkflowValue& value) {
    if (!is_array()) throw std::runtime_error("WorkflowValue is not an array");
    if (!value.value_) throw std::bad_alloc();
    char error[128];
    const int status = praktor_value_append_copy(&value_, nullptr, 0, value.value_, error, sizeof(error));
    checkValue(status, error, "Failed to append WorkflowValue array member");
}

void WorkflowValue::push_back(WorkflowValue&& value) {
    if (!is_array()) throw std::runtime_error("WorkflowValue is not an array");
    char error[128];
    const int status = praktor_value_append_take(&value_, nullptr, 0, &value.value_, error, sizeof(error));
    checkValue(status, error, "Failed to append WorkflowValue array member");
}

void WorkflowValue::erase_at(size_t index) {
    char error[128];
    const int status = praktor_value_erase(&value_, index, error, sizeof(error));
    checkValue(status, error);
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

const json_value_t* WorkflowValue::findPath(const std::vector<std::string>& keys) const {
    const auto views = pathViews(keys);
    const json_value_t* found = nullptr;
    const int status = praktor_value_find(value_, views.data(), keys.size(), &found);
    return status == SALTS_OK ? found : nullptr;
}

void WorkflowValue::setPath(const std::vector<std::string>& keys, const WorkflowValue& value) {
    if (keys.empty()) { *this = value; return; }
    const auto views = pathViews(keys);
    if (!value.value_) throw std::bad_alloc();
    char error[128];
    const int status = praktor_value_set_copy(&value_, views.data(), keys.size(), value.value_, error, sizeof(error));
    checkValue(status, error);
}

void WorkflowValue::setPath(const std::vector<std::string>& keys, WorkflowValue&& value) {
    if (keys.empty()) { *this = std::move(value); return; }
    const auto views = pathViews(keys);
    char error[128];
    const int status = praktor_value_set_take(&value_, views.data(), keys.size(), &value.value_, error, sizeof(error));
    checkValue(status, error);
}

void WorkflowValue::appendPath(const std::vector<std::string>& keys, const WorkflowValue& value) {
    const auto views = pathViews(keys);
    if (!value.value_) throw std::bad_alloc();
    char error[128];
    const int status = praktor_value_append_copy(&value_, views.data(), keys.size(), value.value_, error, sizeof(error));
    checkValue(status, error);
}

void WorkflowValue::appendPath(const std::vector<std::string>& keys, WorkflowValue&& value) {
    const auto views = pathViews(keys);
    char error[128];
    const int status = praktor_value_append_take(&value_, views.data(), keys.size(), &value.value_, error, sizeof(error));
    checkValue(status, error);
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
