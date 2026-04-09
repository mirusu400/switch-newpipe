#include "newpipe/switch_player.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <switch.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengles2.h>
#include <curl/curl.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <mpv/stream_cb.h>

#include "newpipe/i18n.hpp"
#include "newpipe/log.hpp"
#include "newpipe/youtube_resolver.hpp"

namespace newpipe {
namespace {

constexpr const char* kUserAgent =
    "Mozilla/5.0 (Nintendo Switch; Switch-NewPipe)";
constexpr const char* kDownloadUserAgent =
    "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
constexpr const char* kProtocolPrefix = "switchcache://";
constexpr float kPi = 3.14159265f;
constexpr size_t kInitialStreamBufferBytes = 512 * 1024;

std::string translate_loading_text(const std::string& value) {
    if (value == "RESOLVING YOUTUBE STREAM") {
        return newpipe::tr("player/loading/resolving_youtube_stream");
    }
    if (value == "CONTACTING PLAYER API") {
        return newpipe::tr("player/loading/contacting_player_api");
    }
    if (value == "SELECTING PLAYABLE FORMAT") {
        return newpipe::tr("player/loading/selecting_playable_format");
    }
    if (value == "REQUESTING 720P HLS STREAM") {
        return newpipe::tr("player/loading/requesting_720p_hls_stream");
    }
    if (value == "REQUESTING 720P AVC STREAM") {
        return newpipe::tr("player/loading/requesting_720p_avc_stream");
    }
    return value;
}

struct StreamSession {
    class SwitchPlayer* player = nullptr;
    size_t position = 0;
    bool is_audio = false;
};

struct FileDownloadContext {
    int fd = -1;
    size_t offset = 0;
    std::atomic<size_t>* progress = nullptr;
};

struct Glyph {
    char ch;
    std::array<const char*, 7> rows;
};

constexpr std::array<const char*, 7> kBlankRows = {
    "00000",
    "00000",
    "00000",
    "00000",
    "00000",
    "00000",
    "00000",
};

constexpr Glyph kGlyphs[] = {
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
    {'D', {"11100", "10010", "10001", "10001", "10001", "10010", "11100"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01110"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"11111", "00100", "00100", "00100", "00100", "00100", "11111"}},
    {'J', {"00111", "00010", "00010", "00010", "00010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
    {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
    {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
    {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {'5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
    {'6', {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
    {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
    {':', {"00000", "01100", "01100", "00000", "01100", "01100", "00000"}},
    {'/', {"00001", "00010", "00100", "01000", "10000", "00000", "00000"}},
    {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}},
    {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
    {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
    {' ', {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
};

const Glyph& glyph_for_char(char ch) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (const auto& glyph : kGlyphs) {
        if (glyph.ch == upper) {
            return glyph;
        }
    }

    static constexpr Glyph kBlank = {' ', kBlankRows};
    return kBlank;
}

std::string trim_text(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string uppercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch >= 32 && ch <= 126) {
            return static_cast<char>(std::toupper(ch));
        }
        return static_cast<char>(' ');
    });
    return trim_text(value);
}

std::string clamp_text(const std::string& text, size_t max_length) {
    if (text.size() <= max_length) {
        return text;
    }

    if (max_length <= 3) {
        return text.substr(0, max_length);
    }

    return text.substr(0, max_length - 3) + "...";
}

std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

std::vector<std::string> split_header_fields(const std::string& header_fields) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start < header_fields.size()) {
        size_t end = header_fields.find(',', start);
        if (end == std::string::npos) {
            end = header_fields.size();
        }

        std::string field = trim(header_fields.substr(start, end - start));
        if (!field.empty()) {
            fields.push_back(std::move(field));
        }

        start = end + 1;
    }

    return fields;
}

std::string format_megabytes(double bytes) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / (1024.0 * 1024.0));
    return buffer;
}

double clamp_double(double value, double min_value, double max_value) {
    return std::max(min_value, std::min(max_value, value));
}

std::string format_playback_time(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return "--:--";
    }

    const int total_seconds = static_cast<int>(seconds);
    const int hours = total_seconds / 3600;
    const int minutes = (total_seconds % 3600) / 60;
    const int secs = total_seconds % 60;

    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes, secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
    }
    return buffer;
}

bool contains_case_insensitive(std::string haystack, std::string needle) {
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return haystack.find(needle) != std::string::npos;
}

bool is_manifest_url(const std::string& url) {
    return contains_case_insensitive(url, ".m3u8")
        || contains_case_insensitive(url, ".mpd")
        || contains_case_insensitive(url, "/manifest/")
        || contains_case_insensitive(url, "manifest.googlevideo.com");
}

std::optional<uint64_t> find_query_u64(const std::string& url, const std::string& key) {
    const std::string pattern = key + "=";
    size_t search_from = 0;
    while (true) {
        const size_t pos = url.find(pattern, search_from);
        if (pos == std::string::npos) {
            return std::nullopt;
        }

        if (pos == 0 || url[pos - 1] == '?' || url[pos - 1] == '&') {
            const size_t value_start = pos + pattern.size();
            const size_t value_end = url.find_first_of("&#", value_start);
            const std::string value = url.substr(
                value_start,
                value_end == std::string::npos ? std::string::npos : value_end - value_start);
            try {
                return static_cast<uint64_t>(std::stoull(value));
            } catch (...) {
                return std::nullopt;
            }
        }

        search_from = pos + 1;
    }
}

size_t write_at_fd(int fd, const char* data, size_t total_size, size_t offset) {
    if (::lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
        return 0;
    }

    size_t written_total = 0;
    while (written_total < total_size) {
        const ssize_t written =
            ::write(fd, data + written_total, total_size - written_total);
        if (written <= 0) {
            break;
        }
        written_total += static_cast<size_t>(written);
    }
    return written_total;
}

size_t read_at_fd(int fd, char* data, size_t total_size, size_t offset) {
    if (::lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
        return 0;
    }

    size_t read_total = 0;
    while (read_total < total_size) {
        const ssize_t read = ::read(fd, data + read_total, total_size - read_total);
        if (read <= 0) {
            break;
        }
        read_total += static_cast<size_t>(read);
    }
    return read_total;
}

size_t write_download_chunk(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* context = static_cast<FileDownloadContext*>(userdata);
    if (!context || context->fd < 0) {
        return 0;
    }

    const size_t total_size = size * nmemb;
    const size_t written = write_at_fd(
        context->fd, static_cast<const char*>(ptr), total_size, context->offset);
    context->offset += written;
    if (context->progress) {
        context->progress->store(context->offset);
    }
    return written;
}

bool write_text_file(const std::string& path, const std::string& body) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        return false;
    }

    const size_t written = std::fwrite(body.data(), 1, body.size(), file);
    std::fclose(file);
    return written == body.size();
}

int measure_text_width(const std::string& text, int scale) {
    if (text.empty()) {
        return 0;
    }

    return static_cast<int>(text.size()) * scale * 6 - scale;
}

void fill_rect(int x, int y, int width, int height, int screen_height, float r, float g, float b) {
    if (width <= 0 || height <= 0) {
        return;
    }

    glEnable(GL_SCISSOR_TEST);
    glScissor(x, screen_height - y - height, width, height);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void draw_text_line(
    int x,
    int y,
    int scale,
    int screen_height,
    const std::string& text,
    float r,
    float g,
    float b) {
    int cursor_x = x;
    for (char ch : text) {
        const Glyph& glyph = glyph_for_char(ch);
        for (size_t row = 0; row < glyph.rows.size(); row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph.rows[row][col] == '1') {
                    fill_rect(
                        cursor_x + col * scale,
                        y + static_cast<int>(row) * scale,
                        scale,
                        scale,
                        screen_height,
                        r,
                        g,
                        b);
                }
            }
        }
        cursor_x += scale * 6;
    }
}

const char* end_reason_to_string(mpv_end_file_reason reason) {
    switch (reason) {
        case MPV_END_FILE_REASON_EOF:
            return "eof";
        case MPV_END_FILE_REASON_STOP:
            return "stop";
        case MPV_END_FILE_REASON_QUIT:
            return "quit";
        case MPV_END_FILE_REASON_ERROR:
            return "error";
        case MPV_END_FILE_REASON_REDIRECT:
            return "redirect";
        default:
            return "unknown";
    }
}

class SwitchPlayer {
public:
    explicit SwitchPlayer(const PlaybackRequest& request)
        : request_(request) {
    }

    ~SwitchPlayer() {
        cleanup();
    }

    bool run(std::string& error) {
        logf("player: run begin title=%s request_url=%s", request_.title.c_str(), request_.url.c_str());
        appletSetMediaPlaybackState(true);
        set_loading_status(
            newpipe::tr("player/loading/preparing_playback"),
            clamp_text(uppercase_ascii(request_.title), 40));

        if (!init_sdl(error)) {
            logf("player: init_sdl failed error=%s", error.c_str());
            return false;
        }

        start_prepare();
        if (!wait_for_prepare(error)) {
            logf("player: prepare failed error=%s", error.c_str());
            return false;
        }

        if (!init_mpv(error)) {
            logf("player: init_mpv failed error=%s", error.c_str());
            return false;
        }

        set_loading_status(
            newpipe::tr("player/loading/opening_media_stream"),
            active_quality_label_.empty() ? newpipe::tr("player/loading/waiting_for_mpv_load")
                                          : clamp_text(uppercase_ascii(active_quality_label_), 40));
        if (!load_file(error)) {
            logf("player: load_file failed error=%s", error.c_str());
            return false;
        }

        player_input_ready_at_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        mpv_events_pending_.store(true);
        loop();
        error = terminal_error_;
        logf("player: run end ok=%d", terminal_error_.empty() ? 1 : 0);
        return terminal_error_.empty();
    }

private:
    static void* get_proc_address(void*, const char* name) {
        return SDL_GL_GetProcAddress(name);
    }

    static void on_mpv_wakeup(void* context) {
        static_cast<SwitchPlayer*>(context)->mpv_events_pending_.store(true);
    }

    static void on_render_update(void* context) {
        static_cast<SwitchPlayer*>(context)->render_update_pending_.store(true);
    }

    static int stream_open(void* context, char* uri, mpv_stream_cb_info* info) {
        auto* player = static_cast<SwitchPlayer*>(context);
        std::string requested_uri = uri ? uri : "";
        if (requested_uri.rfind(kProtocolPrefix, 0) != 0) {
            logf("player: unsupported stream uri=%s", requested_uri.c_str());
            return MPV_ERROR_LOADING_FAILED;
        }

        auto* stream = new StreamSession();
        stream->player = player;
        stream->is_audio = requested_uri == std::string(kProtocolPrefix) + "audio";
        if (stream->is_audio) {
            std::lock_guard<std::mutex> lock(player->audio_mutex_);
            if (player->audio_cache_fd_ < 0) {
                log_line("player: audio cache handle missing");
                delete stream;
                return MPV_ERROR_LOADING_FAILED;
            }
            logf("player: stream opened cached path=%s kind=audio", player->audio_cache_path_.c_str());
        } else {
            std::lock_guard<std::mutex> lock(player->stream_mutex_);
            if (player->stream_cache_fd_ < 0) {
                log_line("player: stream cache handle missing");
                delete stream;
                return MPV_ERROR_LOADING_FAILED;
            }
            logf("player: stream opened cached path=%s kind=video", player->stream_cache_path_.c_str());
        }
        info->cookie = stream;
        info->read_fn = &SwitchPlayer::stream_read;
        info->seek_fn = nullptr;
        info->size_fn = nullptr;
        info->close_fn = &SwitchPlayer::stream_close;
        return 0;
    }

    static int64_t stream_read(void* cookie, char* buffer, uint64_t bytes) {
        auto* stream = static_cast<StreamSession*>(cookie);
        if (!stream || !stream->player) {
            return MPV_ERROR_GENERIC;
        }

        auto* player = stream->player;
        if (stream->is_audio) {
            std::unique_lock<std::mutex> lock(player->audio_mutex_);
            if (player->audio_cache_fd_ < 0) {
                return MPV_ERROR_GENERIC;
            }
            while (stream->position >= player->audio_downloaded_bytes_.load() && !player->audio_download_done_.load()) {
                player->audio_cv_.wait_for(lock, std::chrono::milliseconds(100));
            }

            const size_t available = player->audio_downloaded_bytes_.load() > stream->position
                ? player->audio_downloaded_bytes_.load() - stream->position
                : 0;
            const bool failed = player->audio_download_done_.load() && !player->audio_download_success_.load();
            if (available == 0) {
                return failed ? MPV_ERROR_GENERIC : 0;
            }

            const size_t to_read = std::min<size_t>(available, static_cast<size_t>(bytes));
            const size_t read = read_at_fd(player->audio_cache_fd_, buffer, to_read, stream->position);
            if (read == 0) {
                return MPV_ERROR_GENERIC;
            }
            stream->position += read;
            return static_cast<int64_t>(read);
        }

        std::unique_lock<std::mutex> lock(player->stream_mutex_);
        if (player->stream_cache_fd_ < 0) {
            return MPV_ERROR_GENERIC;
        }
        while (stream->position >= player->streamed_bytes_ && !player->stream_download_done_) {
            player->stream_cv_.wait_for(lock, std::chrono::milliseconds(100));
        }

        const size_t available = player->streamed_bytes_ > stream->position
            ? player->streamed_bytes_ - stream->position
            : 0;
        const bool failed = player->stream_download_done_ && !player->stream_download_success_;
        if (available == 0) {
            return failed ? MPV_ERROR_GENERIC : 0;
        }

        const size_t to_read = std::min<size_t>(available, static_cast<size_t>(bytes));
        const size_t read = read_at_fd(player->stream_cache_fd_, buffer, to_read, stream->position);
        if (read == 0) {
            return MPV_ERROR_GENERIC;
        }
        stream->position += read;
        return static_cast<int64_t>(read);
    }

    static void stream_close(void* cookie) {
        delete static_cast<StreamSession*>(cookie);
    }

    static int on_curl_progress(
        void* clientp,
        curl_off_t dltotal,
        curl_off_t dlnow,
        curl_off_t /*ultotal*/,
        curl_off_t /*ulnow*/) {
        auto* player = static_cast<SwitchPlayer*>(clientp);
        if (player->stream_abort_.load()) {
            return 1;
        }
        player->update_download_progress(dltotal, dlnow);
        return 0;
    }

    static size_t on_stream_write(void* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* player = static_cast<SwitchPlayer*>(userdata);
        const size_t total_size = size * nmemb;
        std::lock_guard<std::mutex> lock(player->stream_mutex_);
        if (player->stream_cache_fd_ < 0) {
            return 0;
        }

        const size_t write_offset = player->streamed_bytes_;
        const size_t written = write_at_fd(
            player->stream_cache_fd_, static_cast<const char*>(ptr), total_size, write_offset);
        player->streamed_bytes_ += written;
        player->stream_cv_.notify_all();
        return written;
    }

    static size_t on_audio_stream_write(void* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* player = static_cast<SwitchPlayer*>(userdata);
        const size_t total_size = size * nmemb;
        std::lock_guard<std::mutex> lock(player->audio_mutex_);
        if (player->audio_cache_fd_ < 0) {
            return 0;
        }

        const size_t write_offset = player->audio_downloaded_bytes_.load();
        const size_t written = write_at_fd(
            player->audio_cache_fd_, static_cast<const char*>(ptr), total_size, write_offset);
        player->audio_downloaded_bytes_.store(write_offset + written);
        if (write_offset == 0 && written > 0) {
            logf("player: audio first bytes received=%zu", written);
        }
        player->audio_cv_.notify_all();
        return written;
    }

    static int on_audio_curl_progress(
        void* clientp,
        curl_off_t /*dltotal*/,
        curl_off_t /*dlnow*/,
        curl_off_t /*ultotal*/,
        curl_off_t /*ulnow*/) {
        auto* player = static_cast<SwitchPlayer*>(clientp);
        return player->audio_download_abort_.load() ? 1 : 0;
    }

    void set_loading_status(const std::string& title, const std::string& detail) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        const std::string normalized_title = clamp_text(uppercase_ascii(title), 40);
        const std::string normalized_detail = clamp_text(uppercase_ascii(detail), 48);
        if (normalized_title == loading_title_ && normalized_detail == loading_detail_) {
            return;
        }
        loading_title_ = normalized_title;
        loading_detail_ = normalized_detail;
        logf("player: status title=%s detail=%s", loading_title_.c_str(), loading_detail_.c_str());
    }

    std::pair<std::string, std::string> get_loading_status() const {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return {loading_title_, loading_detail_};
    }

    void show_osd_message(const std::string& message, int duration_ms = 2500) {
        osd_message_ = clamp_text(uppercase_ascii(message), 52);
        osd_visible_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    }

    bool is_temporary_osd_visible() const {
        return std::chrono::steady_clock::now() < osd_visible_until_;
    }

    bool should_draw_osd() const {
        return first_frame_rendered_ && (osd_pinned_ || last_pause_state_ || is_temporary_osd_visible());
    }

    void refresh_osd_snapshot(bool force = false) {
        if (!mpv_) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!force && now - last_osd_refresh_ < std::chrono::milliseconds(200)) {
            return;
        }
        last_osd_refresh_ = now;

        double value = 0.0;
        if (mpv_get_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &value) >= 0 && std::isfinite(value)) {
            last_time_pos_ = std::max(0.0, value);
        }
        if (mpv_get_property(mpv_, "duration", MPV_FORMAT_DOUBLE, &value) >= 0 && std::isfinite(value)) {
            last_duration_ = std::max(0.0, value);
        }
        if (mpv_get_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &value) >= 0 && std::isfinite(value)) {
            last_volume_ = clamp_double(value, 0.0, 130.0);
        }

        int paused_flag = 0;
        if (mpv_get_property(mpv_, "pause", MPV_FORMAT_FLAG, &paused_flag) >= 0) {
            last_pause_state_ = paused_flag != 0;
        }

        char* media_title = mpv_get_property_string(mpv_, "media-title");
        if (media_title) {
            const std::string normalized = clamp_text(uppercase_ascii(media_title), 52);
            if (!normalized.empty()) {
                osd_title_ = normalized;
            }
            mpv_free(media_title);
        }
    }

    std::string get_mpv_property_text(const char* name) const {
        if (!mpv_ || !name) {
            return {};
        }

        char* value = mpv_get_property_string(mpv_, name);
        if (!value) {
            return {};
        }

        std::string result = value;
        mpv_free(value);
        return result;
    }

    void log_audio_state(const char* phase) const {
        const std::string aid = get_mpv_property_text("aid");
        const std::string codec = get_mpv_property_text("audio-codec-name");
        const std::string channels = get_mpv_property_text("audio-params/channel-count");
        const std::string rate = get_mpv_property_text("audio-params/samplerate");
        const std::string ao = get_mpv_property_text("current-ao");
        const std::string device = get_mpv_property_text("audio-device");
        logf("player: audio-state phase=%s aid=%s codec=%s channels=%s rate=%s ao=%s device=%s",
             phase ? phase : "unknown",
             aid.empty() ? "none" : aid.c_str(),
             codec.empty() ? "none" : codec.c_str(),
             channels.empty() ? "none" : channels.c_str(),
             rate.empty() ? "none" : rate.c_str(),
             ao.empty() ? "none" : ao.c_str(),
             device.empty() ? "none" : device.c_str());
    }

    void toggle_osd(bool& force_redraw) {
        osd_pinned_ = !osd_pinned_;
        show_osd_message(
            osd_pinned_ ? newpipe::tr("player/osd/locked")
                        : newpipe::tr("player/osd/auto"),
            1800);
        force_redraw = true;
    }

    void render_playback_osd(int width, int height) {
        refresh_osd_snapshot();
        if (!should_draw_osd()) {
            return;
        }

        const int margin = std::max(20, width / 40);
        const int top_panel_width = std::min(width - margin * 2, width * 3 / 4);
        const int top_panel_height = std::max(92, height / 8);
        const int top_panel_x = margin;
        const int top_panel_y = margin;
        const int bottom_panel_width = width - margin * 2;
        const int bottom_panel_height = std::max(108, height / 6);
        const int bottom_panel_x = margin;
        const int bottom_panel_y = height - bottom_panel_height - margin;

        fill_rect(top_panel_x, top_panel_y, top_panel_width, top_panel_height, height, 0.05f, 0.05f, 0.05f);
        fill_rect(
            bottom_panel_x, bottom_panel_y, bottom_panel_width, bottom_panel_height, height, 0.04f, 0.04f, 0.04f);

        const int title_scale = std::max(2, height / 300);
        const int meta_scale = std::max(2, height / 360);
        const std::string title = clamp_text(
            osd_title_.empty() ? clamp_text(uppercase_ascii(request_.title), 52) : osd_title_, 52);
        const std::string playback_state = last_pause_state_ ? newpipe::tr("player/status/paused")
            : active_is_live_ ? newpipe::tr("player/status/live")
                              : newpipe::tr("player/status/playing");
        const std::string status_line = is_temporary_osd_visible() && !osd_message_.empty()
            ? osd_message_
            : clamp_text(
                  playback_state
                      + (active_quality_label_.empty()
                             ? ""
                             : "  " + clamp_text(uppercase_ascii(active_quality_label_), 18))
                      + "  " + newpipe::tr("player/status/volume")
                      + " " + std::to_string(static_cast<int>(std::lround(last_volume_))),
                  52);

        if (!title.empty()) {
            draw_text_line(
                top_panel_x + 18,
                top_panel_y + 16,
                title_scale,
                height,
                title,
                0.96f,
                0.96f,
                0.96f);
        }
        draw_text_line(
            top_panel_x + 18,
            top_panel_y + 16 + title_scale * 9,
            meta_scale,
            height,
            status_line,
            last_pause_state_ ? 1.0f : 0.78f,
            last_pause_state_ ? 0.78f : 0.78f,
            last_pause_state_ ? 0.32f : 0.78f);

        const int bar_x = bottom_panel_x + 20;
        const int bar_y = bottom_panel_y + 18;
        const int bar_width = bottom_panel_width - 40;
        const int bar_height = std::max(10, height / 72);
        fill_rect(bar_x, bar_y, bar_width, bar_height, height, 0.18f, 0.18f, 0.18f);

        if (active_is_live_) {
            fill_rect(bar_x, bar_y, std::max(72, bar_width / 5), bar_height, height, 0.92f, 0.20f, 0.18f);
        } else if (last_duration_ > 1.0) {
            const double ratio = clamp_double(last_time_pos_ / last_duration_, 0.0, 1.0);
            fill_rect(bar_x, bar_y, static_cast<int>(std::round(bar_width * ratio)), bar_height, height, 0.95f, 0.95f, 0.95f);
        }

        const std::string left_time =
            active_is_live_ ? newpipe::tr("player/status/live") : format_playback_time(last_time_pos_);
        const std::string right_time =
            active_is_live_ ? clamp_text(uppercase_ascii(active_quality_label_), 18) : format_playback_time(last_duration_);
        const std::string center_line = clamp_text(newpipe::tr("player/osd_controls"), 52);

        draw_text_line(bar_x, bar_y + bar_height + 14, meta_scale, height, left_time, 0.94f, 0.94f, 0.94f);
        draw_text_line(
            bar_x + std::max(0, (bar_width - measure_text_width(center_line, meta_scale)) / 2),
            bar_y + bar_height + 14,
            meta_scale,
            height,
            center_line,
            0.62f,
            0.62f,
            0.62f);
        draw_text_line(
            bar_x + std::max(0, bar_width - measure_text_width(right_time, meta_scale)),
            bar_y + bar_height + 14,
            meta_scale,
            height,
            right_time,
            0.82f,
            0.82f,
            0.82f);

        const int volume_bar_y = bottom_panel_y + bottom_panel_height - 28;
        const int volume_bar_width = bottom_panel_width / 4;
        const int volume_bar_x = bottom_panel_x + bottom_panel_width - volume_bar_width - 20;
        fill_rect(volume_bar_x, volume_bar_y, volume_bar_width, 10, height, 0.16f, 0.16f, 0.16f);
        fill_rect(
            volume_bar_x,
            volume_bar_y,
            static_cast<int>(std::round(volume_bar_width * clamp_double(last_volume_ / 100.0, 0.0, 1.0))),
            10,
            height,
            0.72f,
            0.72f,
            0.72f);
    }

    bool prepare_stream(std::string& error) {
        use_stream_bridge_ = false;
        active_local_media_path_.clear();
        active_external_audio_local_path_.clear();
        audio_download_done_.store(false);
        audio_download_success_.store(false);
        audio_download_abort_.store(false);
        audio_downloaded_bytes_.store(0);
        audio_prefetch_min_bytes_ = 0;
        audio_attach_attempted_ = false;
        audio_wait_logged_ = false;
        audio_download_error_.clear();
        active_url_ = request_.url;
        active_referer_ = request_.referer;
        active_http_header_fields_ = request_.http_header_fields;
        active_quality_label_.clear();
        active_hls_bitrate_ = 0;
        active_external_audio_url_.clear();
        fallback_url_.clear();
        fallback_referer_.clear();
        fallback_http_header_fields_.clear();
        fallback_quality_label_.clear();
        fallback_external_audio_url_.clear();
        fallback_attempted_ = false;
        active_is_live_ = false;

        if (!YouTubeResolver::is_youtube_url(request_.url)) {
            set_loading_status(
                newpipe::tr("player/loading/opening_direct_media"),
                newpipe::tr("player/loading/non_youtube_url"));
            return true;
        }

        YouTubeResolver resolver;
        const auto resolved = resolver.resolve(
            request_.url,
            error,
            [this](const std::string& title, const std::string& detail) {
                set_loading_status(translate_loading_text(title), translate_loading_text(detail));
            });
        if (!resolved.has_value()) {
            return false;
        }

        active_url_ = resolved->stream_url;
        active_referer_ = resolved->referer;
        active_http_header_fields_ = resolved->http_header_fields;
        active_quality_label_ = resolved->quality_label;
        active_hls_bitrate_ = resolved->hls_bitrate;
        active_external_audio_url_ = resolved->external_audio_url;
        fallback_url_ = resolved->fallback_stream_url;
        fallback_referer_ = resolved->fallback_referer;
        fallback_http_header_fields_ = resolved->fallback_http_header_fields;
        fallback_quality_label_ = resolved->fallback_quality_label;
        fallback_external_audio_url_ = resolved->fallback_external_audio_url;
        active_is_live_ = resolved->is_live;
        if (!resolved->playlist_body.empty()) {
#ifdef __SWITCH__
            active_local_media_path_ = "sdmc:/switch/switch_newpipe_selected.m3u8";
#else
            active_local_media_path_ = "/tmp/switch_newpipe_selected.m3u8";
#endif
            std::remove(active_local_media_path_.c_str());
            if (!write_text_file(active_local_media_path_, resolved->playlist_body)) {
                error = "local HLS playlist write failed";
                return false;
            }
            active_url_ = active_local_media_path_;
            logf("player: wrote local playlist path=%s bytes=%zu",
                 active_local_media_path_.c_str(),
                 resolved->playlist_body.size());
        }

        std::string detail = active_quality_label_.empty()
            ? newpipe::tr("player/loading/direct_stream_ready")
            : active_quality_label_;
        if (active_is_live_) {
            detail += " " + newpipe::tr("player/status/live");
        }
        set_loading_status(newpipe::tr("player/loading/opening_media_stream"), detail);
        logf("player: resolved youtube url=%s hls_bitrate=%d", active_url_.c_str(), active_hls_bitrate_);
        if (!start_stream_bridge_if_needed(error)) {
            return false;
        }
        start_audio_prefetch_if_needed();
        return true;
    }

    bool start_stream_bridge_if_needed(std::string& error) {
        if (active_is_live_) {
            set_loading_status(
                newpipe::tr("player/loading/opening_live_stream"),
                newpipe::tr("player/loading/direct_playback"));
            return true;
        }

        if (is_manifest_url(active_url_)) {
            set_loading_status(
                newpipe::tr("player/loading/opening_media_stream"),
                newpipe::tr("player/loading/direct_dash_playback"));
            return true;
        }

        if (active_url_.rfind("http://", 0) != 0 && active_url_.rfind("https://", 0) != 0) {
            return true;
        }

        return start_stream_bridge(error);
    }

    void stop_stream_bridge() {
        stream_abort_.store(true);
        stream_cv_.notify_all();

        if (stream_download_thread_.joinable()) {
            stream_download_thread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(stream_mutex_);
            if (stream_cache_fd_ >= 0) {
                ::close(stream_cache_fd_);
                stream_cache_fd_ = -1;
            }
            streamed_bytes_ = 0;
            stream_download_done_ = false;
            stream_download_success_ = false;
            stream_download_error_.clear();
        }

        if (!stream_cache_path_.empty()) {
            std::remove(stream_cache_path_.c_str());
        }

        stream_abort_.store(false);
        use_stream_bridge_ = false;
    }

    void update_download_progress(curl_off_t dltotal, curl_off_t dlnow) {
        const auto now = std::chrono::steady_clock::now();
        if (dlnow > 0 && now - last_progress_update_ < std::chrono::milliseconds(200)
            && (dltotal <= 0 || dlnow < dltotal)) {
            return;
        }

        last_progress_update_ = now;
        std::string detail;
        if (dltotal > 0) {
            detail = format_megabytes(static_cast<double>(dlnow))
                + " / " + format_megabytes(static_cast<double>(dltotal));
        } else {
            detail = format_megabytes(static_cast<double>(dlnow))
                + " " + newpipe::tr("player/loading/received");
        }
        set_loading_status(newpipe::tr("player/loading/downloading_video_data"), detail);
    }

    static std::string build_ranged_url(const std::string& base_url, const std::string& range, int rn) {
        std::string url = base_url;
        url += (url.find('?') == std::string::npos) ? "?" : "&";
        url += "range=" + range + "&rn=" + std::to_string(rn) + "&alr=yes";
        return url;
    }

    bool perform_chunked_ranged_download(CURL* parent_curl, long& status_code) {
        constexpr uint64_t kChunkBytes = 512 * 1024;
        constexpr int kMaxRetries = 5;
        const auto total_size = find_query_u64(active_url_, "clen");
        if (!total_size.has_value() || *total_size == 0) {
            log_line("player: ranged download missing clen");
            return false;
        }

        // Extract settings from parent curl handle, then use fresh handles per chunk
        const auto extra_headers = split_header_fields(active_http_header_fields_);

        int rn = 0;
        while (!stream_abort_.load()) {
            size_t chunk_start = 0;
            {
                std::lock_guard<std::mutex> lock(stream_mutex_);
                chunk_start = streamed_bytes_;
            }

            if (chunk_start >= *total_size) {
                status_code = 206;
                return true;
            }

            const uint64_t chunk_end = std::min<uint64_t>(
                *total_size - 1, static_cast<uint64_t>(chunk_start) + kChunkBytes - 1);
            const std::string range = std::to_string(chunk_start) + "-" + std::to_string(chunk_end);
            const std::string chunk_url = build_ranged_url(active_url_, range, rn);
            update_download_progress(static_cast<curl_off_t>(*total_size), static_cast<curl_off_t>(chunk_start));

            bool chunk_ok = false;
            for (int retry = 0; retry < kMaxRetries && !stream_abort_.load(); ++retry) {
                if (retry > 0) {
                    logf("player: ranged chunk retry %d range=%s", retry, range.c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000 * retry));
                }

                CURL* ch = curl_easy_init();
                if (!ch) break;
                struct curl_slist* ch_headers = nullptr;
                for (const auto& h : extra_headers) {
                    ch_headers = curl_slist_append(ch_headers, h.c_str());
                }
                curl_easy_setopt(ch, CURLOPT_URL, chunk_url.c_str());
                curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(ch, CURLOPT_MAXREDIRS, 10L);
                curl_easy_setopt(ch, CURLOPT_USERAGENT, kDownloadUserAgent);
                curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 15L);
                curl_easy_setopt(ch, CURLOPT_TIMEOUT, 30L);
                curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
                curl_easy_setopt(ch, CURLOPT_HTTPHEADER, ch_headers);
                curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, &SwitchPlayer::on_stream_write);
                curl_easy_setopt(ch, CURLOPT_WRITEDATA, this);
                curl_easy_setopt(ch, CURLOPT_FRESH_CONNECT, 1L);
                curl_easy_setopt(ch, CURLOPT_FORBID_REUSE, 1L);
                if (!active_referer_.empty()) {
                    curl_easy_setopt(ch, CURLOPT_REFERER, active_referer_.c_str());
                }

                const CURLcode result = curl_easy_perform(ch);
                curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status_code);
                curl_slist_free_all(ch_headers);
                curl_easy_cleanup(ch);

                if (result == CURLE_OK && (status_code == 206 || status_code == 200)) {
                    chunk_ok = true;
                    break;
                }
                logf("player: ranged chunk failed range=%s curl=%d status=%ld attempt=%d",
                     range.c_str(),
                     static_cast<int>(result),
                     status_code,
                     retry + 1);
            }

            if (!chunk_ok) {
                return false;
            }

            size_t chunk_written = 0;
            {
                std::lock_guard<std::mutex> lock(stream_mutex_);
                chunk_written = streamed_bytes_ - chunk_start;
            }
            if (chunk_written == 0) {
                logf("player: ranged chunk empty range=%s", range.c_str());
                return false;
            }
            ++rn;
        }

        return false;
    }

    bool start_stream_bridge(std::string& error) {
        set_loading_status(
            newpipe::tr("player/loading/downloading_video_data"),
            newpipe::tr("player/loading/starting_transfer"));

#ifdef __SWITCH__
        stream_cache_path_ = "sdmc:/switch/switch_newpipe_stream.cache";
#else
        stream_cache_path_ = "/tmp/switch_newpipe_stream.cache";
#endif

        std::remove(stream_cache_path_.c_str());
        stream_cache_fd_ = ::open(stream_cache_path_.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0666);
        if (stream_cache_fd_ < 0) {
            error = "stream cache file open failed";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(stream_mutex_);
            streamed_bytes_ = 0;
            stream_download_done_ = false;
            stream_download_success_ = false;
            stream_download_error_.clear();
        }

        stream_abort_.store(false);
        last_progress_update_ = std::chrono::steady_clock::time_point{};
        stream_download_thread_ = std::thread([this]() {
            CURL* curl = curl_easy_init();
            if (!curl) {
                std::lock_guard<std::mutex> lock(stream_mutex_);
                stream_download_done_ = true;
                stream_download_success_ = false;
                stream_download_error_ = "curl init failed";
                stream_cv_.notify_all();
                return;
            }

            struct curl_slist* header_list = nullptr;
            const auto extra_headers = split_header_fields(active_http_header_fields_);
            for (const auto& header : extra_headers) {
                header_list = curl_slist_append(header_list, header.c_str());
            }

            const bool is_googlevideo = contains_case_insensitive(active_url_, "googlevideo.com/videoplayback");
            const char* ua = is_googlevideo ? kDownloadUserAgent : kUserAgent;

            curl_easy_setopt(curl, CURLOPT_URL, active_url_.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, ua);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &SwitchPlayer::on_stream_write);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &SwitchPlayer::on_curl_progress);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            if (!active_referer_.empty()) {
                curl_easy_setopt(curl, CURLOPT_REFERER, active_referer_.c_str());
            }
            long status_code = 0;
            CURLcode result = CURLE_OK;
            const bool has_ratebypass = contains_case_insensitive(active_url_, "ratebypass=yes");
            if (is_googlevideo && !has_ratebypass) {
                log_line("player: using chunked ranged download");
                const bool ok = perform_chunked_ranged_download(curl, status_code);
                result = ok ? CURLE_OK : CURLE_HTTP_RETURNED_ERROR;
            } else {
                if (has_ratebypass) {
                    log_line("player: using direct download (ratebypass)");
                }
                result = curl_easy_perform(curl);
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            }
            curl_slist_free_all(header_list);
            curl_easy_cleanup(curl);

            {
                std::lock_guard<std::mutex> lock(stream_mutex_);
                stream_download_done_ = true;
                stream_download_success_ =
                    !stream_abort_.load() && result == CURLE_OK
                    && ((status_code >= 200 && status_code < 300) || status_code == 206);
                if (!stream_download_success_) {
                    logf("player: stream download failed url=%s curl=%d status=%ld",
                         active_url_.c_str(),
                         static_cast<int>(result),
                         status_code);
                    stream_download_error_ = stream_abort_.load()
                        ? "stream aborted"
                        : "video stream download failed";
                } else {
                    logf("player: stream download complete bytes=%zu", streamed_bytes_);
                }
                stream_cv_.notify_all();
            }
        });

        std::unique_lock<std::mutex> lock(stream_mutex_);
        while (streamed_bytes_ < kInitialStreamBufferBytes && !stream_download_done_) {
            stream_cv_.wait_for(lock, std::chrono::milliseconds(50));
        }

        if (streamed_bytes_ == 0 && stream_download_done_ && !stream_download_success_) {
            error = stream_download_error_.empty() ? "video stream download failed" : stream_download_error_;
            return false;
        }

        use_stream_bridge_ = true;
        set_loading_status(
            newpipe::tr("player/loading/opening_media_stream"),
            streamed_bytes_ >= kInitialStreamBufferBytes
                ? newpipe::tr("player/loading/initial_buffer_ready")
                : newpipe::tr("player/loading/download_complete"));
        return true;
    }

    bool download_url_to_file(
        const std::string& url,
        const std::string& path,
        const std::string& referer,
        const std::string& header_fields,
        std::atomic<bool>& abort_flag,
        std::atomic<size_t>& downloaded_bytes,
        std::string& error) {
        std::remove(path.c_str());
        const int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0666);
        if (fd < 0) {
            error = "audio cache file open failed";
            return false;
        }

        FileDownloadContext download_context;
        download_context.fd = fd;
        download_context.progress = &downloaded_bytes;

        CURL* curl = curl_easy_init();
        if (!curl) {
            ::close(fd);
            std::remove(path.c_str());
            error = "audio curl init failed";
            return false;
        }

        struct curl_slist* header_list = nullptr;
        const auto extra_headers = split_header_fields(header_fields);
        for (const auto& header : extra_headers) {
            header_list = curl_slist_append(header_list, header.c_str());
        }

        const bool is_gv = contains_case_insensitive(url, "googlevideo.com/videoplayback");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, is_gv ? kDownloadUserAgent : kUserAgent);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_download_chunk);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download_context);
        if (!referer.empty()) {
            curl_easy_setopt(curl, CURLOPT_REFERER, referer.c_str());
        }

        long status_code = 0;
        CURLcode result = CURLE_OK;
        if (contains_case_insensitive(url, "googlevideo.com/videoplayback")) {
            const auto total_size = find_query_u64(url, "clen");
            if (!total_size.has_value() || *total_size == 0) {
                result = CURLE_HTTP_RETURNED_ERROR;
                error = "audio ranged download missing clen";
            } else {
                constexpr uint64_t kChunkBytes = 512 * 1024;
                constexpr int kMaxRetries = 3;
                int rn = 0;
                while (!abort_flag.load()) {
                    if (download_context.offset >= *total_size) {
                        status_code = 206;
                        break;
                    }

                    const uint64_t chunk_start = download_context.offset;
                    const uint64_t chunk_end = std::min<uint64_t>(
                        *total_size - 1, chunk_start + kChunkBytes - 1);
                    const std::string range = std::to_string(chunk_start) + "-" + std::to_string(chunk_end);
                    const std::string chunk_url = build_ranged_url(url, range, rn);
                    curl_easy_setopt(curl, CURLOPT_URL, chunk_url.c_str());
                    curl_easy_setopt(curl, CURLOPT_RANGE, nullptr);

                    bool chunk_ok = false;
                    for (int retry = 0; retry < kMaxRetries && !abort_flag.load(); ++retry) {
                        if (retry > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(500 * retry));
                            curl_easy_setopt(curl, CURLOPT_URL, chunk_url.c_str());
                        }
                        result = curl_easy_perform(curl);
                        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
                        if (result == CURLE_OK && (status_code == 206 || status_code == 200)) {
                            chunk_ok = true;
                            break;
                        }
                        logf("player: audio ranged chunk failed range=%s curl=%d status=%ld attempt=%d",
                             range.c_str(),
                             static_cast<int>(result),
                             status_code,
                             retry + 1);
                    }

                    if (!chunk_ok) {
                        error = "audio ranged chunk failed";
                        break;
                    }
                    if (download_context.offset <= chunk_start) {
                        result = CURLE_WRITE_ERROR;
                        error = "audio ranged chunk empty";
                        break;
                    }
                    ++rn;
                }
            }
        } else {
            result = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
        }

        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        ::close(fd);

        const bool ok = !abort_flag.load()
            && result == CURLE_OK
            && status_code >= 200
            && status_code < 300;
        if (!ok) {
            if (error.empty()) {
                error = abort_flag.load() ? "audio download aborted" : "audio download failed";
            }
            std::remove(path.c_str());
            return false;
        }

        return true;
    }

    void start_audio_prefetch_if_needed() {
        const std::string source_url = active_external_audio_url_;
        const std::string source_referer = active_referer_;
        const std::string source_headers = active_http_header_fields_;
        if (source_url.empty()) {
            pending_external_audio_attach_ = false;
            return;
        }

#ifdef __SWITCH__
        audio_cache_path_ = "sdmc:/switch/switch_newpipe_audio.cache";
#else
        audio_cache_path_ = "/tmp/switch_newpipe_audio.cache";
#endif

        std::remove(audio_cache_path_.c_str());
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_cache_fd_ = ::open(audio_cache_path_.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0666);
        }
        if (audio_cache_fd_ < 0) {
            pending_external_audio_attach_ = false;
            audio_download_done_.store(true);
            audio_download_success_.store(false);
            audio_download_error_ = "audio cache file open failed";
            return;
        }

        audio_prefetch_min_bytes_ = contains_case_insensitive(source_url, "googlevideo.com/videoplayback")
            ? 1024 * 1024
            : 0;
        pending_external_audio_attach_ = true;
        logf("player: start audio prefetch kind=split-audio url=%s", source_url.c_str());
        audio_download_thread_ = std::thread([this, source_url, source_referer, source_headers]() {
            CURL* curl = curl_easy_init();
            if (!curl) {
                audio_download_success_.store(false);
                audio_download_error_ = "audio curl init failed";
                audio_download_done_.store(true);
                audio_cv_.notify_all();
                return;
            }

            struct curl_slist* header_list = nullptr;
            const auto extra_headers = split_header_fields(source_headers);
            for (const auto& header : extra_headers) {
                header_list = curl_slist_append(header_list, header.c_str());
            }

            const bool is_googlevideo_audio = contains_case_insensitive(source_url, "googlevideo.com/videoplayback");
            const char* audio_ua = is_googlevideo_audio ? kDownloadUserAgent : kUserAgent;

            curl_easy_setopt(curl, CURLOPT_URL, source_url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, audio_ua);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &SwitchPlayer::on_audio_stream_write);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &SwitchPlayer::on_audio_curl_progress);
            curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            if (!source_referer.empty()) {
                curl_easy_setopt(curl, CURLOPT_REFERER, source_referer.c_str());
            }

            long status_code = 0;
            CURLcode result = CURLE_OK;
            if (contains_case_insensitive(source_url, "googlevideo.com/videoplayback")) {
                // Use fresh curl handle per chunk to avoid connection reuse issues
                // with YouTube CDN concurrent download limits.
                curl_slist_free_all(header_list);
                curl_easy_cleanup(curl);
                curl = nullptr;
                header_list = nullptr;

                const auto total_size = find_query_u64(source_url, "clen");
                if (!total_size.has_value() || *total_size == 0) {
                    result = CURLE_HTTP_RETURNED_ERROR;
                    audio_download_error_ = "audio ranged download missing clen";
                } else {
                    constexpr uint64_t kChunkBytes = 512 * 1024;
                    constexpr int kMaxRetries = 5;
                    int rn = 0;
                    while (!audio_download_abort_.load()) {
                        const size_t chunk_start = audio_downloaded_bytes_.load();
                        if (chunk_start >= *total_size) {
                            status_code = 206;
                            break;
                        }

                        const uint64_t chunk_end = std::min<uint64_t>(
                            *total_size - 1, static_cast<uint64_t>(chunk_start) + kChunkBytes - 1);
                        const std::string range = std::to_string(chunk_start) + "-" + std::to_string(chunk_end);
                        const std::string chunk_url = build_ranged_url(source_url, range, rn);

                        bool chunk_ok = false;
                        for (int retry = 0; retry < kMaxRetries && !audio_download_abort_.load(); ++retry) {
                            if (retry > 0) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1000 * retry));
                            }

                            CURL* ch = curl_easy_init();
                            if (!ch) break;
                            struct curl_slist* ch_headers = nullptr;
                            for (const auto& h : extra_headers) {
                                ch_headers = curl_slist_append(ch_headers, h.c_str());
                            }
                            curl_easy_setopt(ch, CURLOPT_URL, chunk_url.c_str());
                            curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1L);
                            curl_easy_setopt(ch, CURLOPT_MAXREDIRS, 10L);
                            curl_easy_setopt(ch, CURLOPT_USERAGENT, audio_ua);
                            curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
                            curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0L);
                            curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 15L);
                            curl_easy_setopt(ch, CURLOPT_TIMEOUT, 30L);
                            curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
                            curl_easy_setopt(ch, CURLOPT_HTTPHEADER, ch_headers);
                            curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, &SwitchPlayer::on_audio_stream_write);
                            curl_easy_setopt(ch, CURLOPT_WRITEDATA, this);
                            curl_easy_setopt(ch, CURLOPT_FRESH_CONNECT, 1L);
                            curl_easy_setopt(ch, CURLOPT_FORBID_REUSE, 1L);
                            if (!source_referer.empty()) {
                                curl_easy_setopt(ch, CURLOPT_REFERER, source_referer.c_str());
                            }

                            result = curl_easy_perform(ch);
                            curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status_code);
                            curl_slist_free_all(ch_headers);
                            curl_easy_cleanup(ch);

                            if (result == CURLE_OK && (status_code == 206 || status_code == 200)) {
                                chunk_ok = true;
                                break;
                            }
                            logf("player: audio ranged chunk failed range=%s curl=%d status=%ld attempt=%d",
                                 range.c_str(),
                                 static_cast<int>(result),
                                 status_code,
                                 retry + 1);
                        }

                        if (!chunk_ok) {
                            audio_download_error_ = "audio ranged chunk failed";
                            break;
                        }
                        if (audio_downloaded_bytes_.load() <= chunk_start) {
                            result = CURLE_WRITE_ERROR;
                            audio_download_error_ = "audio ranged chunk empty";
                            break;
                        }
                        ++rn;
                    }
                }
            } else {
                result = curl_easy_perform(curl);
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
                curl_slist_free_all(header_list);
                curl_easy_cleanup(curl);
            }
            // curl and header_list already cleaned up above

            const bool ok = !audio_download_abort_.load()
                && result == CURLE_OK
                && ((status_code >= 200 && status_code < 300) || status_code == 206);
            audio_download_success_.store(ok);
            audio_download_error_ = ok
                ? std::string()
                : (!audio_download_error_.empty()
                    ? audio_download_error_
                    : (audio_download_abort_.load() ? "audio download aborted" : "audio stream download failed"));
            audio_download_done_.store(true);
            audio_cv_.notify_all();
            if (ok) {
                logf("player: audio prefetch complete bytes=%zu", audio_downloaded_bytes_.load());
            } else {
                logf("player: audio prefetch failed error=%s curl=%d status=%ld",
                     audio_download_error_.c_str(),
                     static_cast<int>(result),
                     status_code);
            }
        });
    }

    void stop_audio_prefetch() {
        audio_download_abort_.store(true);
        if (audio_download_thread_.joinable()) {
            audio_download_thread_.join();
        }
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            if (audio_cache_fd_ >= 0) {
                ::close(audio_cache_fd_);
                audio_cache_fd_ = -1;
            }
        }
        audio_download_abort_.store(false);
        audio_download_done_.store(false);
        audio_download_success_.store(false);
        audio_downloaded_bytes_.store(0);
        audio_prefetch_min_bytes_ = 0;
        audio_attach_attempted_ = false;
        audio_wait_logged_ = false;
        audio_download_error_.clear();
        if (!audio_cache_path_.empty()) {
            std::remove(audio_cache_path_.c_str());
            audio_cache_path_.clear();
        }
    }

    void start_prepare() {
        prepare_error_.clear();
        prepare_done_.store(false);
        prepare_success_.store(false);

        prepare_thread_ = std::thread([this]() {
            std::string error;
            const bool ok = prepare_stream(error);
            if (!ok) {
                prepare_error_ = std::move(error);
            }

            prepare_success_.store(ok);
            prepare_done_.store(true);
        });
    }

    bool wait_for_prepare(std::string& error) {
        int phase = 0;
        const Uint32 started_at = SDL_GetTicks();

        while (!prepare_done_.load() || SDL_GetTicks() - started_at < 250) {
            render_loading_screen(phase);
            phase = (phase + 1) % 12;
            SDL_PumpEvents();
            SDL_Delay(50);
        }

        if (prepare_thread_.joinable()) {
            prepare_thread_.join();
        }

        if (!prepare_success_.load()) {
            error = prepare_error_.empty() ? "stream preparation failed" : prepare_error_;
            return false;
        }

        return true;
    }

    bool init_sdl(std::string& error) {
        log_line("player: SDL_Init");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
            error = std::string("SDL_Init failed: ") + SDL_GetError();
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        const AppletOperationMode mode = appletGetOperationMode();
        int width = mode == AppletOperationMode_Console ? 1920 : 1280;
        int height = mode == AppletOperationMode_Console ? 1080 : 720;

        window_ = SDL_CreateWindow("Switch-NewPipe Player", 0, 0, width, height, SDL_WINDOW_SHOWN);
        if (!window_) {
            error = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
            return false;
        }
        SDL_ShowWindow(window_);

        gl_context_ = SDL_GL_CreateContext(window_);
        if (!gl_context_) {
            error = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
            return false;
        }

        SDL_GL_MakeCurrent(window_, gl_context_);
        SDL_GL_SetSwapInterval(1);
        SDL_GameControllerEventState(SDL_ENABLE);

        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                auto* controller = SDL_GameControllerOpen(i);
                if (controller) {
                    game_controllers_.push_back(controller);
                    has_game_controller_ = true;
                }
            } else {
                auto* joystick = SDL_JoystickOpen(i);
                if (joystick) {
                    joysticks_.push_back(joystick);
                }
            }
        }

        return true;
    }

    bool init_mpv(std::string& error) {
        std::setlocale(LC_NUMERIC, "C");
        mpv_ = mpv_create();
        if (!mpv_) {
            error = "mpv_create failed";
            return false;
        }

        mpv_set_option_string(mpv_, "vo", "libmpv");
        mpv_set_option_string(mpv_, "hwdec", "auto-safe");
        mpv_set_option_string(mpv_, "profile", "sw-fast");
        mpv_set_option_string(mpv_, "osc", "no");
        mpv_set_option_string(mpv_, "terminal", "no");
        mpv_set_option_string(mpv_, "config", "no");
        mpv_set_option_string(mpv_, "keep-open", "yes");
        mpv_set_option_string(mpv_, "force-seekable", "no");
        mpv_set_option_string(mpv_, "ytdl", "no");
        mpv_set_option_string(mpv_, "tls-verify", "no");
        mpv_set_option_string(mpv_, "access-references", "yes");
        if (!active_local_media_path_.empty()) {
            mpv_set_option_string(mpv_, "load-unsafe-playlists", "yes");
            mpv_set_option_string(
                mpv_, "demuxer-lavf-o", "protocol_whitelist=file,http,https,tcp,tls,crypto,data,subfile");
        }
        const bool is_hls = contains_case_insensitive(active_quality_label_, "hls");
        if (is_hls) {
            const std::string hls_bitrate = active_hls_bitrate_ > 0
                ? std::to_string(active_hls_bitrate_)
                : std::string("max");
            mpv_set_option_string(mpv_, "hls-bitrate", hls_bitrate.c_str());
            mpv_set_option_string(mpv_, "load-unsafe-playlists", "yes");
            mpv_set_option_string(
                mpv_, "demuxer-lavf-o",
                "protocol_whitelist=file,http,https,tcp,tls,crypto,data,subfile");
        }
        if (use_stream_bridge_) {
            mpv_set_option_string(
                mpv_, "demuxer-lavf-o", "protocol_whitelist=file,http,https,tcp,tls,crypto,data,switchcache");
        }
        // Use Android UA for YouTube CDN to avoid rejection
        const bool is_youtube_stream = contains_case_insensitive(active_url_, "googlevideo.com")
            || contains_case_insensitive(active_url_, "youtube.com");
        mpv_set_option_string(mpv_, "user-agent",
            is_youtube_stream ? kDownloadUserAgent : kUserAgent);
        if (!active_referer_.empty()) {
            mpv_set_option_string(mpv_, "referrer", active_referer_.c_str());
        }
        if (!active_http_header_fields_.empty()) {
            mpv_set_option_string(mpv_, "http-header-fields", active_http_header_fields_.c_str());
        }

        if (use_stream_bridge_ && mpv_stream_cb_add_ro(mpv_, "switchcache", this, &SwitchPlayer::stream_open) < 0) {
            error = "mpv stream callback setup failed";
            return false;
        }

        if (mpv_initialize(mpv_) < 0) {
            error = "mpv_initialize failed";
            return false;
        }

        mpv_request_log_messages(mpv_, "warn");
        mpv_set_wakeup_callback(mpv_, &SwitchPlayer::on_mpv_wakeup, this);

        mpv_opengl_init_params gl_init{};
        gl_init.get_proc_address = &SwitchPlayer::get_proc_address;
        gl_init.get_proc_address_ctx = nullptr;

        int advanced_control = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        if (mpv_render_context_create(&render_context_, mpv_, params) < 0) {
            error = "mpv_render_context_create failed";
            return false;
        }

        mpv_render_context_set_update_callback(render_context_, &SwitchPlayer::on_render_update, this);
        render_update_pending_.store(true);
        return true;
    }

    void destroy_mpv() {
        if (render_context_) {
            mpv_render_context_set_update_callback(render_context_, nullptr, nullptr);
            mpv_render_context_free(render_context_);
            render_context_ = nullptr;
        }

        if (mpv_) {
            mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
            mpv_destroy(mpv_);
            mpv_ = nullptr;
        }
    }

    bool load_file(std::string& error) {
        const std::string playback_url = use_stream_bridge_ ? std::string(kProtocolPrefix) + "playback" : active_url_;
        logf("player: loadfile url=%s", playback_url.c_str());
        const char* command[] = {"loadfile", playback_url.c_str(), nullptr};
        if (mpv_command(mpv_, command) < 0) {
            error = "mpv loadfile failed";
            return false;
        }

        load_started_at_ = std::chrono::steady_clock::now();
        file_loaded_ = false;
        pending_external_audio_attach_ = !active_external_audio_url_.empty();
        return true;
    }

    void attach_external_audio_if_needed() {
        if (!pending_external_audio_attach_ || audio_attach_attempted_ || audio_cache_fd_ < 0 || !mpv_) {
            return;
        }

        if (!audio_download_done_.load() && audio_downloaded_bytes_.load() < audio_prefetch_min_bytes_) {
            if (!audio_wait_logged_) {
                audio_wait_logged_ = true;
                logf("player: waiting for audio prefetch bytes=%zu need=%zu",
                     audio_downloaded_bytes_.load(),
                     audio_prefetch_min_bytes_);
            }
            return;
        }

        if (audio_download_done_.load() && !audio_download_success_.load()) {
            pending_external_audio_attach_ = false;
            audio_attach_attempted_ = true;
            logf("player: audio prefetch unavailable error=%s", audio_download_error_.c_str());
            show_osd_message(newpipe::tr("player/osd/audio_download_failed"), 2500);
            return;
        }

        pending_external_audio_attach_ = false;
        audio_attach_attempted_ = true;
        if (!first_frame_rendered_) {
            set_loading_status(
                newpipe::tr("player/loading/opening_media_stream"),
                newpipe::tr("player/loading/attaching_audio_track"));
        }
        const char* audio_url = "switchcache://audio";
        logf("player: add external audio url=%s", audio_url);
        const char* audio_command[] = {"audio-add", audio_url, "select", nullptr};
        if (mpv_command_async(mpv_, 0, audio_command) < 0) {
            log_line("player: mpv audio-add failed, continuing video-only");
            show_osd_message(newpipe::tr("player/osd/audio_attach_failed"), 2500);
            return;
        }

        log_line("player: external audio attach queued");
    }

    void maybe_attach_external_audio() {
        if (!pending_external_audio_attach_ || audio_attach_attempted_) {
            return;
        }
        attach_external_audio_if_needed();
    }

    bool retry_with_fallback(std::string& error) {
        if (fallback_attempted_ || fallback_url_.empty()) {
            return false;
        }

        fallback_attempted_ = true;
        logf("player: retry with fallback quality=%s url=%s",
             fallback_quality_label_.c_str(),
             fallback_url_.c_str());
        set_loading_status(
            newpipe::tr("player/loading/stream_720_failed"),
            newpipe::tr("player/loading/fallback_safe_stream"));

        destroy_mpv();
        stop_stream_bridge();
        stop_audio_prefetch();
        if (!active_local_media_path_.empty()) {
            std::remove(active_local_media_path_.c_str());
            active_local_media_path_.clear();
        }
        if (!active_external_audio_local_path_.empty()) {
            std::remove(active_external_audio_local_path_.c_str());
            active_external_audio_local_path_.clear();
        }

        active_url_ = fallback_url_;
        active_referer_ = fallback_referer_;
        active_http_header_fields_ = fallback_http_header_fields_;
        active_quality_label_ = fallback_quality_label_;
        active_hls_bitrate_ = 0;
        active_external_audio_url_ = fallback_external_audio_url_;
        pending_external_audio_attach_ = false;
        active_is_live_ = false;

        if (!start_stream_bridge_if_needed(error)) {
            return false;
        }
        start_audio_prefetch_if_needed();
        if (!init_mpv(error)) {
            return false;
        }
        if (!load_file(error)) {
            return false;
        }

        first_frame_rendered_ = false;
        file_loaded_ = false;
        terminal_error_.clear();
        mpv_events_pending_.store(true);
        render_update_pending_.store(true);
        return true;
    }

    void loop() {
        bool running = true;
        bool paused = false;
        bool force_redraw = true;
        int loading_phase = 0;

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                } else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                    handle_controller_button(event.cbutton.button, running, paused, force_redraw);
                } else if (!has_game_controller_ && event.type == SDL_JOYBUTTONDOWN) {
                    handle_joy_button(event.jbutton.button, running, paused, force_redraw);
                } else if (!has_game_controller_ && event.type == SDL_JOYHATMOTION) {
                    handle_hat(event.jhat.value, force_redraw);
                }
            }

            if (mpv_events_pending_.exchange(false) && !drain_mpv_events()) {
                running = false;
            }

            maybe_attach_external_audio();

            bool frame_ready = false;
            if (render_context_ && render_update_pending_.exchange(false)) {
                frame_ready = (mpv_render_context_update(render_context_) & MPV_RENDER_UPDATE_FRAME) != 0;
            }

            if (!first_frame_rendered_) {
                if (!file_loaded_ && !fallback_attempted_ && !fallback_url_.empty()
                    && std::chrono::steady_clock::now() - load_started_at_ > std::chrono::seconds(30)) {
                    std::string fallback_error;
                    log_line("player: load timeout before file-loaded, attempting fallback");
                    if (retry_with_fallback(fallback_error)) {
                        loading_phase = 0;
                        continue;
                    }
                    if (!fallback_error.empty()) {
                        terminal_error_ = fallback_error;
                        logf("player: fallback after timeout failed error=%s", fallback_error.c_str());
                        running = false;
                        continue;
                    }
                }

                if (frame_ready) {
                    render_frame();
                    first_frame_rendered_ = true;
                    refresh_osd_snapshot(true);
                    show_osd_message(newpipe::tr("player/osd/playback_ready"), 3500);
                    force_redraw = paused;
                    set_loading_status(
                        newpipe::tr("player/loading/playback_started"),
                        active_quality_label_.empty()
                            ? newpipe::tr("player/loading/video_frame_ready")
                            : clamp_text(uppercase_ascii(active_quality_label_), 40));
                    continue;
                }

                render_loading_screen(loading_phase);
                loading_phase = (loading_phase + 1) % 12;
                SDL_Delay(50);
                continue;
            }

            bool should_render = force_redraw || frame_ready || paused || osd_pinned_ || is_temporary_osd_visible();
            if (should_render) {
                render_frame();
                force_redraw = paused;
                if (!frame_ready) {
                    SDL_Delay(16);
                }
            } else {
                SDL_Delay(10);
            }
        }
    }

    void handle_controller_button(Uint8 button, bool& running, bool& paused, bool& force_redraw) {
        if (std::chrono::steady_clock::now() < player_input_ready_at_) {
            logf("player: ignored early controller button=%u", static_cast<unsigned int>(button));
            return;
        }
        if (!first_frame_rendered_ && button == SDL_CONTROLLER_BUTTON_A) {
            log_line("player: ignored pause during loading");
            return;
        }
        logf("player: controller button=%u", static_cast<unsigned int>(button));
        switch (button) {
            case SDL_CONTROLLER_BUTTON_A:
                running = false;
                break;
            case SDL_CONTROLLER_BUTTON_B:
                toggle_pause(paused, force_redraw);
                break;
            case SDL_CONTROLLER_BUTTON_X:
            case SDL_CONTROLLER_BUTTON_Y:
                toggle_osd(force_redraw);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                change_volume(5, force_redraw);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                change_volume(-5, force_redraw);
                break;
            default:
                break;
        }
    }

    void handle_joy_button(Uint8 button, bool& running, bool& paused, bool& force_redraw) {
        if (std::chrono::steady_clock::now() < player_input_ready_at_) {
            logf("player: ignored early joystick button=%u", static_cast<unsigned int>(button));
            return;
        }
        if (!first_frame_rendered_ && button == 0) {
            log_line("player: ignored joystick pause during loading");
            return;
        }
        logf("player: joystick button=%u", static_cast<unsigned int>(button));
        switch (button) {
            case 0:
                running = false;
                break;
            case 1:
                toggle_pause(paused, force_redraw);
                break;
            case 2:
            case 3:
                toggle_osd(force_redraw);
                break;
            case 13:
                change_volume(5, force_redraw);
                break;
            case 15:
                change_volume(-5, force_redraw);
                break;
            default:
                break;
        }
    }

    void handle_hat(Uint8 hat_value, bool& force_redraw) {
        if (hat_value & SDL_HAT_UP) {
            change_volume(5, force_redraw);
        } else if (hat_value & SDL_HAT_DOWN) {
            change_volume(-5, force_redraw);
        }
    }

    void toggle_pause(bool& paused, bool& force_redraw) {
        if (!mpv_) {
            return;
        }

        const char* command[] = {"cycle", "pause", nullptr};
        mpv_command(mpv_, command);
        paused = !paused;
        last_pause_state_ = paused;
        show_osd_message(
            paused ? newpipe::tr("player/status/paused")
                   : newpipe::tr("player/status/playing"),
            3000);
        refresh_osd_snapshot(true);
        logf("player: pause toggled paused=%d", paused ? 1 : 0);
        force_redraw = true;
    }

    void change_volume(int delta, bool& force_redraw) {
        if (!mpv_) {
            return;
        }

        const std::string delta_text = std::to_string(delta);
        const char* command[] = {"add", "volume", delta_text.c_str(), nullptr};
        mpv_command(mpv_, command);
        last_volume_ = clamp_double(last_volume_ + static_cast<double>(delta), 0.0, 130.0);
        show_osd_message(
            newpipe::tr("player/osd/volume", static_cast<int>(std::lround(last_volume_))),
            2200);
        refresh_osd_snapshot(true);
        logf("player: volume delta=%d", delta);
        force_redraw = true;
    }

    void render_loading_screen(int phase) {
        int width = 1280;
        int height = 720;
        SDL_GL_GetDrawableSize(window_, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(0.02f, 0.02f, 0.02f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const int panel_w = width * 2 / 3;
        const int panel_h = height / 3;
        const int panel_x = (width - panel_w) / 2;
        const int panel_y = (height - panel_h) / 2;
        fill_rect(panel_x, panel_y, panel_w, panel_h, height, 0.10f, 0.10f, 0.10f);

        const int cx = width / 2;
        const int cy = height / 2 - height / 20;
        const int radius = std::min(width, height) / 12;
        const int dot_size = std::max(14, std::min(width, height) / 32);

        for (int i = 0; i < 12; i++) {
            const float angle = (static_cast<float>(i) / 12.0f) * 2.0f * kPi;
            const int x = static_cast<int>(std::round(cx + std::cos(angle) * radius)) - dot_size / 2;
            const int y = static_cast<int>(std::round(cy + std::sin(angle) * radius)) - dot_size / 2;
            const int active = (phase + 12 - i) % 12;
            const float shade = active == 0 ? 1.0f : std::max(0.25f, 0.88f - static_cast<float>(active) * 0.08f);
            fill_rect(x, y, dot_size, dot_size, height, shade, shade, shade);
        }

        const auto [title, detail] = get_loading_status();
        const int title_scale = std::max(3, height / 240);
        const int detail_scale = std::max(2, height / 320);
        const int title_y = cy + radius + dot_size + height / 30;
        const int detail_y = title_y + title_scale * 9;

        if (!title.empty()) {
            draw_text_line(
                (width - measure_text_width(title, title_scale)) / 2,
                title_y,
                title_scale,
                height,
                title,
                0.92f,
                0.92f,
                0.92f);
        }

        if (!detail.empty()) {
            draw_text_line(
                (width - measure_text_width(detail, detail_scale)) / 2,
                detail_y,
                detail_scale,
                height,
                detail,
                0.65f,
                0.65f,
                0.65f);
        }

        SDL_GL_SwapWindow(window_);
        glFinish();
    }

    bool drain_mpv_events() {
        while (true) {
            mpv_event* event = mpv_wait_event(mpv_, 0);
            if (!event || event->event_id == MPV_EVENT_NONE) {
                return true;
            }

            if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
                const auto* message = static_cast<mpv_event_log_message*>(event->data);
                if (message && message->prefix && message->level && message->text) {
                    std::string text = message->text;
                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
                        text.pop_back();
                    }
                    logf("player: mpv [%s] %s: %s",
                         message->level,
                         message->prefix,
                         text.c_str());
                }
                continue;
            }

            if (event->event_id == MPV_EVENT_START_FILE) {
                set_loading_status(
                    newpipe::tr("player/loading/opening_media_stream"),
                    use_stream_bridge_ ? newpipe::tr("player/loading/reading_stream_buffer")
                                       : newpipe::tr("player/loading/contacting_video_cdn"));
                continue;
            }

            if (event->event_id == MPV_EVENT_FILE_LOADED) {
                file_loaded_ = true;
                maybe_attach_external_audio();
                log_line("player: mpv file-loaded event");
                std::string detail = active_quality_label_.empty()
                    ? newpipe::tr("player/loading/buffering_first_frame")
                    : active_is_live_ ? active_quality_label_ + " " + newpipe::tr("player/status/live")
                                      : active_quality_label_;
                set_loading_status(newpipe::tr("player/loading/buffering_first_frame"), detail);
                continue;
            }

            if (event->event_id == MPV_EVENT_AUDIO_RECONFIG) {
                log_line("player: mpv audio-reconfig event");
                continue;
            }

            if (event->event_id == MPV_EVENT_END_FILE) {
                const auto* end_file = static_cast<mpv_event_end_file*>(event->data);
                std::string reason = end_file ? end_reason_to_string(end_file->reason) : "unknown";
                logf("player: mpv end-file reason=%s", reason.c_str());
                if (end_file && end_file->reason == MPV_END_FILE_REASON_REDIRECT) {
                    set_loading_status(
                        newpipe::tr("player/loading/opening_media_stream"),
                        newpipe::tr("player/loading/following_playlist_redirect"));
                    continue;
                }
                if (end_file && end_file->reason == MPV_END_FILE_REASON_ERROR) {
                    if (!first_frame_rendered_) {
                        std::string fallback_error;
                        if (retry_with_fallback(fallback_error)) {
                            continue;
                        }
                        if (!fallback_error.empty()) {
                            logf("player: fallback failed error=%s", fallback_error.c_str());
                        }
                    }
                    terminal_error_ = end_file->error != 0
                        ? std::string("mpv load failed: ") + mpv_error_string(end_file->error)
                        : "mpv load failed";
                }
                return false;
            }

            if (event->event_id == MPV_EVENT_SHUTDOWN) {
                if (terminal_error_.empty()) {
                    terminal_error_ = "mpv shutdown";
                }
                log_line("player: mpv shutdown");
                return false;
            }
        }
    }

    void render_frame() {
        int width = 1280;
        int height = 720;
        SDL_GL_GetDrawableSize(window_, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        mpv_opengl_fbo fbo{};
        fbo.fbo = 0;
        fbo.w = width;
        fbo.h = height;
        fbo.internal_format = 0;

        int flip_y = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };

        mpv_render_context_render(render_context_, params);
        render_playback_osd(width, height);
        SDL_GL_SwapWindow(window_);
        mpv_render_context_report_swap(render_context_);
    }

    void cleanup() {
        stop_stream_bridge();
        stop_audio_prefetch();
        destroy_mpv();

        if (gl_context_) {
            SDL_GL_MakeCurrent(window_, nullptr);
            SDL_GL_DeleteContext(gl_context_);
            gl_context_ = nullptr;
        }

        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }

        if (prepare_thread_.joinable()) {
            prepare_thread_.join();
        }
        if (!active_local_media_path_.empty()) {
            std::remove(active_local_media_path_.c_str());
            active_local_media_path_.clear();
        }
        if (!active_external_audio_local_path_.empty()) {
            std::remove(active_external_audio_local_path_.c_str());
            active_external_audio_local_path_.clear();
        }

        for (auto* controller : game_controllers_) {
            SDL_GameControllerClose(controller);
        }
        game_controllers_.clear();

        for (auto* joystick : joysticks_) {
            SDL_JoystickClose(joystick);
        }
        joysticks_.clear();

        SDL_Quit();
        appletSetMediaPlaybackState(false);
    }

    PlaybackRequest request_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    mpv_handle* mpv_ = nullptr;
    mpv_render_context* render_context_ = nullptr;
    std::atomic<bool> mpv_events_pending_{false};
    std::atomic<bool> render_update_pending_{false};
    std::string terminal_error_;
    std::thread prepare_thread_;
    std::atomic<bool> prepare_done_{false};
    std::atomic<bool> prepare_success_{false};
    std::string prepare_error_;
    std::string active_url_;
    std::string active_referer_;
    std::string active_http_header_fields_;
    std::string active_quality_label_;
    int active_hls_bitrate_ = 0;
    std::string active_external_audio_url_;
    std::string active_external_audio_local_path_;
    bool pending_external_audio_attach_ = false;
    std::thread audio_download_thread_;
    std::atomic<bool> audio_download_done_{false};
    std::atomic<bool> audio_download_success_{false};
    std::atomic<bool> audio_download_abort_{false};
    std::atomic<size_t> audio_downloaded_bytes_{0};
    std::string audio_download_error_;
    size_t audio_prefetch_min_bytes_ = 0;
    bool audio_attach_attempted_ = false;
    bool audio_wait_logged_ = false;
    std::string fallback_url_;
    std::string fallback_referer_;
    std::string fallback_http_header_fields_;
    std::string fallback_quality_label_;
    std::string fallback_external_audio_url_;
    bool active_is_live_ = false;
    bool fallback_attempted_ = false;
    bool use_stream_bridge_ = false;
    bool file_loaded_ = false;
    bool first_frame_rendered_ = false;
    bool osd_pinned_ = false;
    std::thread stream_download_thread_;
    std::string stream_cache_path_;
    std::string audio_cache_path_;
    std::string active_local_media_path_;
    int stream_cache_fd_ = -1;
    int audio_cache_fd_ = -1;
    mutable std::mutex stream_mutex_;
    mutable std::mutex audio_mutex_;
    std::condition_variable stream_cv_;
    std::condition_variable audio_cv_;
    size_t streamed_bytes_ = 0;
    bool stream_download_done_ = false;
    bool stream_download_success_ = false;
    std::string stream_download_error_;
    std::atomic<bool> stream_abort_{false};
    std::vector<SDL_GameController*> game_controllers_;
    std::vector<SDL_Joystick*> joysticks_;
    bool has_game_controller_ = false;
    mutable std::mutex status_mutex_;
    std::string loading_title_;
    std::string loading_detail_;
    std::chrono::steady_clock::time_point last_progress_update_{};
    std::chrono::steady_clock::time_point load_started_at_{};
    std::chrono::steady_clock::time_point osd_visible_until_{};
    std::chrono::steady_clock::time_point last_osd_refresh_{};
    std::chrono::steady_clock::time_point player_input_ready_at_{};
    std::string osd_title_ = clamp_text(uppercase_ascii(request_.title), 52);
    std::string osd_message_;
    double last_time_pos_ = 0.0;
    double last_duration_ = 0.0;
    double last_volume_ = 100.0;
    bool last_pause_state_ = false;
};

}  // namespace

bool run_switch_player(const PlaybackRequest& request, std::string& error) {
    SwitchPlayer player(request);
    return player.run(error);
}

}  // namespace newpipe
