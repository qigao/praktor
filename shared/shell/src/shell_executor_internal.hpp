#pragma once

#include "praktor/shell/shell_executor.hpp"

#include <string>

namespace Praktor::Shell::detail {

void emitShellLogLine(const std::string& line);
void mirrorWithPrefix(const char* buffer, size_t bytes_read, bool stream_output,
                      std::string& pending_line);
void flushPendingLine(bool stream_output, std::string& pending_line);

} // namespace Praktor::Shell::detail
