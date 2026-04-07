#include "newpipe/runtime.hpp"

#include <mutex>
#include <optional>
#include <utility>

namespace newpipe {
namespace {

std::mutex g_runtime_mutex;
std::optional<PlaybackRequest> g_pending_playback;
std::string g_last_playback_error;

}  // namespace

void queue_playback(PlaybackRequest request) {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    g_pending_playback = std::move(request);
}

std::optional<PlaybackRequest> take_pending_playback() {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    std::optional<PlaybackRequest> request = std::move(g_pending_playback);
    g_pending_playback.reset();
    return request;
}

void clear_pending_playback() {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    g_pending_playback.reset();
}

void set_last_playback_error(std::string error_message) {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    g_last_playback_error = std::move(error_message);
}

std::string take_last_playback_error() {
    std::lock_guard<std::mutex> lock(g_runtime_mutex);
    std::string error = std::move(g_last_playback_error);
    g_last_playback_error.clear();
    return error;
}

}  // namespace newpipe
