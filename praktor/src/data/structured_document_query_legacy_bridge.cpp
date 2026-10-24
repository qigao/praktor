#include "data/structured_document_query.hpp"
#include "data/structured_document_query.h"
#include <new>
#include <stdexcept>

// Temporary value/exception conversion for C++ callers. All parsing, query
// dispatch, collection and resource ownership live in the independent C target.
namespace Praktor::Data {
namespace {
WorkflowValue result(int status, json_value_t *value, const char *error) {
    if (status == SALTS_ENOMEM) throw std::bad_alloc();
    if (status == SALTS_EINVAL) throw std::invalid_argument(error);
    if (status != SALTS_OK) throw std::runtime_error(error);
    return WorkflowValue::adoptJson(value);
}
} // namespace

WorkflowValue StructuredDocumentQuery::query(StructuredFormat format,
    std::string_view document, std::string_view path) {
    json_value_t *value = nullptr;
    char error[1024]{};
    const int status = praktor_document_query(static_cast<praktor_document_format>(format),
        document.data(), document.size(), path.data(), path.size(), &value, error, sizeof(error));
    return result(status, value, error);
}

WorkflowValue StructuredDocumentQuery::queryJson(const WorkflowValue& document,
    std::string_view path) {
    json_value_t *value = nullptr;
    char error[1024]{};
    const int status = praktor_document_query_json(document.raw(), path.data(), path.size(),
        &value, error, sizeof(error));
    return result(status, value, error);
}
} // namespace Praktor::Data
