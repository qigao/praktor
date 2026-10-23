#pragma once

#include "data/workflow_value.hpp"

#include <string_view>

namespace Praktor::Data {

enum class StructuredFormat {
    Json,
    Yaml,
    Xml,
    Csv,
};

class StructuredDocumentQuery {
public:
    static WorkflowValue query(StructuredFormat format, std::string_view document,
                               std::string_view path);
    static WorkflowValue queryJson(const WorkflowValue& document, std::string_view path);

};

} // namespace Praktor::Data
