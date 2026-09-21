#include "data/structured_document_query.hpp"

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
    turbo_yaml_doc_t* yaml = nullptr;
    if (turbo_parse_yaml(reinterpret_cast<const uint8_t*>(document.data()), document.size(),
                         &yaml) != 0 || !yaml) {
        throw std::invalid_argument("Invalid YAML input");
    }

    const std::string expression(path.empty() ? "/" : path);
    turbo_yaml_path_result_t* result =
        turbo_yaml_path_query(yaml, nullptr, expression.c_str());
    if (!result) {
        turbo_free_yaml(&yaml);
        throw std::invalid_argument("Invalid YPATH expression");
    }
    if (const char* error = turbo_yaml_path_result_error(result); error && error[0] != '\0') {
        const std::string message(error);
        turbo_yaml_path_result_free(result);
        turbo_free_yaml(&yaml);
        throw std::invalid_argument(message);
    }

    WorkflowValue matches = WorkflowValue::array();
    try {
        const size_t count = turbo_yaml_path_result_size(result);
        for (size_t index = 0; index < count; ++index) {
            json_value_t* json = turbo_yaml_node_to_json(
                yaml, turbo_yaml_path_result_get(result, index));
            if (!json) {
                throw std::runtime_error("Failed to convert YPATH result to JSON");
            }
            matches.push_back(WorkflowValue::adoptJson(json));
        }
    } catch (...) {
        turbo_yaml_path_result_free(result);
        turbo_free_yaml(&yaml);
        throw;
    }
    turbo_yaml_path_result_free(result);
    turbo_free_yaml(&yaml);
    return collapseMatches(std::move(matches));
}

WorkflowValue StructuredDocumentQuery::queryXml(std::string_view document,
                                                std::string_view path) {
    turbo_xml_doc_t* xml = nullptr;
    if (turbo_parse_xml(reinterpret_cast<const uint8_t*>(document.data()), document.size(),
                        &xml) != 0 || !xml) {
        throw std::invalid_argument("Invalid XML input");
    }

    const std::string expression(path);
    if (expression.empty()) {
        turbo_free_xml(&xml);
        throw std::invalid_argument("XPath expression must not be empty");
    }

    turbo_xml_list_t result;
    turbo_xml_list_init(&result);
    turbo_xml_xpath_query(xml, expression.c_str(), &result);

    WorkflowValue matches = WorkflowValue::array();
    try {
        for (turbo_xml_list_node_t* entry = result.head; entry; entry = entry->next) {
            const auto* node = static_cast<const turbo_xml_xpath_node_t*>(entry->item);
            const char* text = turbo_xml_xpath_node_text(node);
            if (text) {
                matches.push_back(text);
                continue;
            }
            char* serialized = turbo_xml_xpath_node_xml_dup(node);
            if (!serialized) {
                throw std::runtime_error("Failed to serialize XPath result");
            }
            matches.push_back(serialized);
            turbo_xml_string_free(serialized);
        }
    } catch (...) {
        turbo_xml_list_free(&result);
        turbo_free_xml(&xml);
        throw;
    }
    turbo_xml_list_free(&result);
    turbo_free_xml(&xml);
    return collapseMatches(std::move(matches));
}

WorkflowValue StructuredDocumentQuery::queryCsv(std::string_view document,
                                                std::string_view path) {
    turbo_csv_doc_t* csv = nullptr;
    if (turbo_parse_csv(reinterpret_cast<const uint8_t*>(document.data()), document.size(),
                        &csv) != 0 || !csv) {
        throw std::invalid_argument("Invalid CSV input");
    }

    turbo_dsv_filter_t* filter = turbo_dsv_filter_create(csv, 0);
    if (!filter) {
        turbo_free_csv(&csv);
        throw std::runtime_error("Failed to create CSVPath filter");
    }
    const std::string expression(path);
    if (expression.empty() || !turbo_dsv_filter_compile(filter, expression.c_str())) {
        const char* error = turbo_dsv_filter_error(filter);
        const std::string message = expression.empty()
                                        ? "CSVPath expression must not be empty"
                                        : (error ? error : "Invalid CSVPath expression");
        turbo_dsv_filter_destroy(filter);
        turbo_free_csv(&csv);
        throw std::invalid_argument(message);
    }

    WorkflowValue rows = WorkflowValue::array();
    try {
        const size_t row_count = turbo_csv_row_count(csv);
        const size_t column_count = turbo_csv_column_count(csv);
        for (size_t row = 1; row < row_count; ++row) {
            const int match = turbo_dsv_filter_check_row(filter, row);
            if (match < 0) {
                const char* error = turbo_dsv_filter_error(filter);
                throw std::runtime_error(error ? error : "CSVPath evaluation failed");
            }
            if (match == 0) {
                continue;
            }
            WorkflowValue record = WorkflowValue::object();
            for (size_t column = 0; column < column_count; ++column) {
                const char* name = turbo_csv_get(csv, 0, column);
                const char* value = turbo_csv_get(csv, row, column);
                record.set(name ? name : "", value ? value : "");
            }
            rows.push_back(std::move(record));
        }
    } catch (...) {
        turbo_dsv_filter_destroy(filter);
        turbo_free_csv(&csv);
        throw;
    }
    turbo_dsv_filter_destroy(filter);
    turbo_free_csv(&csv);
    return rows;
}

} // namespace Praktor::Data
