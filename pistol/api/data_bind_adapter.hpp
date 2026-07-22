#pragma once

#include "praktor.h"
#include "workflow_runner.hpp"

#include <data_bind.h>

namespace praktor::api {

class DataBindAdapter final {
public:
    DataBindAdapter() = default;
    ~DataBindAdapter();

    DataBindAdapter(const DataBindAdapter&) = delete;
    DataBindAdapter& operator=(const DataBindAdapter&) = delete;

    DataBindStatus initialize(const praktor_execute_request& request, DataBindError& error);
    DataBindStatus decodeInput(const praktor_execute_request& request,
                               WorkflowInputs& inputs,
                               DataBindError& error) const;
    DataBindStatus encodeResult(const praktor_execute_request& request,
                                const WorkflowValue& result,
                                praktor_owned_data& output,
                                DataBindError& error) const;

    static void release(praktor_owned_data& data) noexcept;

private:
    DataBind* codec_{nullptr};
};

} // namespace praktor::api
