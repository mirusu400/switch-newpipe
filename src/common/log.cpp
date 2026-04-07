#include "newpipe/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace newpipe {
namespace {

std::mutex g_log_mutex;
FILE* g_log_file = nullptr;

const char* log_path() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe.log";
#else
    return "switch_newpipe.log";
#endif
}

}  // namespace

void init_log() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_file) {
        return;
    }

    g_log_file = std::fopen(log_path(), "w");
    if (!g_log_file) {
        return;
    }

    std::fprintf(g_log_file, "=== switch_newpipe log start ===\n");
    std::fflush(g_log_file);
}

void shutdown_log() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_file) {
        return;
    }

    std::fprintf(g_log_file, "=== switch_newpipe log end ===\n");
    std::fflush(g_log_file);
    std::fclose(g_log_file);
    g_log_file = nullptr;
}

void log_line(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_file) {
        return;
    }

    std::fprintf(g_log_file, "%s\n", message.c_str());
    std::fflush(g_log_file);
}

void logf(const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log_file) {
        return;
    }

    va_list args;
    va_start(args, format);
    std::vfprintf(g_log_file, format, args);
    va_end(args);
    std::fputc('\n', g_log_file);
    std::fflush(g_log_file);
}

}  // namespace newpipe
