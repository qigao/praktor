#pragma once

#include "data/workflow_value.hpp"

#include <string_view>

namespace Praktor::Schema {

// Projects trusted, repository-owned IDL. The editor overlay owns YAML syntax
// and cross-field policies; it may not replace IDL-owned field metadata.
WorkflowValue project(std::string_view idl, WorkflowValue editor);

}
