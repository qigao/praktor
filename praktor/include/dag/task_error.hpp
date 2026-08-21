#pragma once

#include <stdexcept>
#include <string_view>

namespace Praktor::Execution {

enum class TaskErrorCode {
    None,
    SchemaInvalid,
    ResourceNotFound,
    ResourceTypeMismatch,
    ResourceAlreadyConsumed,
    ResourceLimitExceeded,
    TransportFailed,
    HttpStatusFailed,
    ArchiveFormatInvalid,
    ArchivePolicyRejected,
    FilesystemFailed,
    ProcessSpawnFailed,
    ServiceStateFailed,
    Timeout,
    Cancelled,
    UnsupportedPlatform,
};

constexpr std::string_view taskErrorCodeName(TaskErrorCode code) {
    switch (code) {
    case TaskErrorCode::None:
        return "none";
    case TaskErrorCode::SchemaInvalid:
        return "schema_invalid";
    case TaskErrorCode::ResourceNotFound:
        return "resource_not_found";
    case TaskErrorCode::ResourceTypeMismatch:
        return "resource_type_mismatch";
    case TaskErrorCode::ResourceAlreadyConsumed:
        return "resource_already_consumed";
    case TaskErrorCode::ResourceLimitExceeded:
        return "resource_limit_exceeded";
    case TaskErrorCode::TransportFailed:
        return "transport_failed";
    case TaskErrorCode::HttpStatusFailed:
        return "http_status_failed";
    case TaskErrorCode::ArchiveFormatInvalid:
        return "archive_format_invalid";
    case TaskErrorCode::ArchivePolicyRejected:
        return "archive_policy_rejected";
    case TaskErrorCode::FilesystemFailed:
        return "filesystem_failed";
    case TaskErrorCode::ProcessSpawnFailed:
        return "process_spawn_failed";
    case TaskErrorCode::ServiceStateFailed:
        return "service_state_failed";
    case TaskErrorCode::Timeout:
        return "timeout";
    case TaskErrorCode::Cancelled:
        return "cancelled";
    case TaskErrorCode::UnsupportedPlatform:
        return "unsupported_platform";
    }

    throw std::invalid_argument("Unknown TaskErrorCode");
}

} // namespace Praktor::Execution
