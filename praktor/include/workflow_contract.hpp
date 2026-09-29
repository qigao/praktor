#pragma once

#include "data/workflow_value.hpp"
#include "yml/task.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>

class WorkflowContext;

namespace Praktor::Contract {

using InputValues = std::unordered_map<std::string, WorkflowValue>;

class WorkflowContractError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

bool valueMatchesType(const WorkflowValue& value, const std::string& type);
InputValues validateAndApplyInputs(const Workflow& workflow,
                                   const InputValues& inputs);
WorkflowValue inputSchema(const Workflow& workflow);
WorkflowValue outputSchema(const Workflow& workflow);
WorkflowValue projectOutputs(const Workflow& workflow,
                             const WorkflowContext& context);

} // namespace Praktor::Contract
