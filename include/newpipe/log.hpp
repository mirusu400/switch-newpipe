#pragma once

#include <string>

namespace newpipe {

void init_log();
void shutdown_log();
void log_line(const std::string& message);
void logf(const char* format, ...);

}  // namespace newpipe
