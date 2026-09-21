#include "data/structured_document_query.hpp"

#include <csv_parser.h>
#include <cyaml.h>
#include <cyaml_json_adapter.h>
#include <dsv_filter.h>
#include <xml_parser/xml_parser.h>

#include <stdexcept>
#include <string>

namespace Praktor::Data {
namespace {

WorkflowValue collapseMatches(WorkflowValue matches) {
    if (matches.size() == 0) {
        return WorkflowValue::null();
    }
    if (matches.size() == 1) {
        return matches.at(0);
    }
    return matches;
}

std::string normalizeJsonPath(std::string_view path) {
    if (path.empty()) {
        return "$";
    }
    if (path.front() == '$') {
        return std::string(path);
    }
    if (path.front() == '[') {
        return "$" + std::string(path);
    }
    return "$." + std::string(path);
}

} // namespace

WorkflowValue StructuredDocumentQuery::query(StructuredFormat format,
                                             std::string_view document,
                                             std::string_view path) {
    switch (format) {
    case StructuredFormat::Json:
        return queryJson(document, path);
    case StructuredFormat::Yaml:
        return queryYaml(document, path);
    case StructuredFormat::Xml:
        return queryXml(document, path);
    case StructuredFormat::Csv:
        return queryCsv(document, path);
    }
    throw std::invalid_argument("Unsupported structured document format");
}

WorkflowValue StructuredDocumentQuery::queryJson(std::string_view document,
                                                 std::string_view path) {
    WorkflowValue root = WorkflowValue::parse(document);
    return queryJson(root, path);
}

WorkflowValue StructuredDocumentQuery::queryJson(const WorkflowValue& root,
                                                 std::string_view path) {
    const std::string expression = normalizeJsonPath(path);
    json_path_result_t* result = json_path_query(root.raw(), expression.c_str());
    if (!result) {
        const char* error = json_path_get_error();
        throw std::invalid_argument(error ? error : "Invalid JSONPath expression");
    }

    WorkflowValue matches = WorkflowValue::array();
    const size_t count = json_path_result_size(result);
    try {
        for (size_t index = 0; index < count; ++index) {
            matches.push_back(WorkflowValue::copyJson(
                json_path_result_get(result, index)));
        }
    } catch (...) {
        json_path_result_free(result);
        throw;
    }
    json_path_result_free(result);
    return collapseMatches(std::move(matches));
}

WorkflowValue StructuredDocumentQuery::queryYaml(std::string_view document,
                                                 std::string_view path) {
    cyaml_error_t parse_error{};
    cyaml_doc_t* yaml = cyaml_parse(document.data(), document.size(), nullptr, &parse_error);
    if (!yaml) {
        throw std::invalid_argument(
            parse_error.msg[0] != '\0' ? parse_error.msg : "Invalid YAML input");
    }

    const std::string expression(path.empty() ? "/" : path);
    cyaml_path_result_t result = cyaml_path_query(yaml, nullptr, expression.c_str());
    if (result.error) {
        const std::string message(result.error);
        cyaml_path_result_free(&result);
        cyaml_free(yaml);
        throw std::invalid_argument(message);
    }

    WorkflowValue matches = WorkflowValue::array();
    try {
        const uint32_t count = cyaml_path_count(&result);
        for (uint32_t index = 0; index < count; ++index) {
            json_value_t* json = json_value_from_cyaml_node(
                yaml, cyaml_path_get(&result, index));
            if (!json) {
                throw std::runtime_error("Failed to convert YPATH result to JSON");
            }
            matches.push_back(WorkflowValue::adoptJson(json));
        }
    } catch (...) {
        cyaml_path_result_free(&result);
        cyaml_free(yaml);
        throw;
    }
    cyaml_path_result_free(&result);
    cyaml_free(yaml);
    return collapseMatches(std::move(matches));
}

WorkflowValue StructuredDocumentQuery::queryXml(std::string_view document,
                                                std::string_view path) {
    salts_xml_document xml{};
    salts_xml_diagnostic parse_diagnostic{};
    if (salts_xml_parse(&xml, document.data(), document.size(), nullptr,
                        &parse_diagnostic) != SALTS_XML_OK) {
        throw std::invalid_argument(
            parse_diagnostic.message[0] != '\0' ? parse_diagnostic.message
                                                  : "Invalid XML input");
    }

    const std::string expression(path);
    if (expression.empty()) {
        salts_xml_document_destroy(&xml);
        throw std::invalid_argument("XPath expression must not be empty");
    }

    salts_xml_node_list result{};
    qvm_diagnostic_t query_diagnostic{};
    if (salts_xml_document_xpath_query(&xml, expression.c_str(), &result, nullptr,
                                       &query_diagnostic) != QVM_STATUS_OK) {
        const std::string message =
            query_diagnostic.message ? query_diagnostic.message : "XPath evaluation failed";
        salts_xml_node_list_destroy(&result);
        salts_xml_document_destroy(&xml);
        throw std::invalid_argument(message);
    }

    WorkflowValue matches = WorkflowValue::array();
    try {
        const size_t count = salts_xml_node_list_size(&result);
        for (size_t index = 0; index < count; ++index) {
            const salts_xml_node node = salts_xml_node_list_at(&result, index);
            const salts_xml_string_view text = salts_xml_node_text_view(node);
            if (text.data) {
                matches.push_back(std::string(text.data, text.size));
                continue;
            }

            size_t serialized_size = 0;
            char* serialized = salts_xml_node_serialize(node, &serialized_size);
            if (!serialized) {
                throw std::runtime_error("Failed to serialize XPath result");
            }
            matches.push_back(std::string(serialized, serialized_size));
            salts_xml_owned_string_free(serialized);
        }
    } catch (...) {
        salts_xml_node_list_destroy(&result);
        salts_xml_document_destroy(&xml);
        throw;
    }
    salts_xml_node_list_destroy(&result);
    salts_xml_document_destroy(&xml);
    return collapseMatches(std::move(matches));
}

WorkflowValue StructuredDocumentQuery::queryCsv(std::string_view document,
                                                std::string_view path) {
    csv_doc_t* csv = csv_parse(document.data(), document.size());
    if (!csv) {
        const char* error = csv_get_error();
        throw std::invalid_argument(error ? error : "Invalid CSV input");
    }

    dsv_filter_t* filter = dsv_filter_create(csv, 0);
    if (!filter) {
        csv_free(csv);
        throw std::runtime_error("Failed to create CSVPath filter");
    }
    const std::string expression(path);
    if (expression.empty() || !dsv_filter_compile(filter, expression.c_str())) {
        const char* error = dsv_filter_error(filter);
        const std::string message = expression.empty()
                                        ? "CSVPath expression must not be empty"
                                        : (error ? error : "Invalid CSVPath expression");
        dsv_filter_destroy(filter);
        csv_free(csv);
        throw std::invalid_argument(message);
    }

    WorkflowValue rows = WorkflowValue::array();
    try {
        const size_t row_count = csv_row_count(csv);
        const size_t column_count = csv_column_count(csv);
        for (size_t row = 1; row < row_count; ++row) {
            const int match = dsv_filter_check_row(filter, row);
            if (match < 0) {
                const char* error = dsv_filter_error(filter);
                throw std::runtime_error(error ? error : "CSVPath evaluation failed");
            }
            if (match == 0) {
                continue;
            }
            WorkflowValue record = WorkflowValue::object();
            for (size_t column = 0; column < column_count; ++column) {
                const char* name = csv_get(csv, 0, column);
                const char* value = csv_get(csv, row, column);
                record.set(name ? name : "", value ? value : "");
            }
            rows.push_back(std::move(record));
        }
    } catch (...) {
        dsv_filter_destroy(filter);
        csv_free(csv);
        throw;
    }
    dsv_filter_destroy(filter);
    csv_free(csv);
    return rows;
}

} // namespace Praktor::Data
