#include "newpipe/youtube_resolver.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"
#include "newpipe/settings_store.hpp"
#include "newpipe/ump.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace newpipe {
namespace {

using nlohmann::json;

constexpr const char* kPlayerApiUrl = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
constexpr const char* kAndroidUserAgent =
    "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
#ifdef __SWITCH__
constexpr const char* kAndroidVrUserAgent =
    "com.google.android.apps.youtube.vr.oculus/1.65.10 "
    "(Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip";
#endif
constexpr const char* kIosUserAgent =
    "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3 like Mac OS X)";
// Apple Vision Pro (visionOS) client. As of 2026 this is the only client that
// still returns full-length 720p/1080p streams (direct URLs and an HLS manifest)
// without a PO token — the ios/android/android_vr GVS URLs 403 after the initial
// CDN burst. Requires visitorData in the player request context.
constexpr const char* kVisionOsUserAgent =
    "com.google.ios.youtube/1.02 (RealityDevice17,1; U; CPU visionOS 26_5 like Mac OS X)";
constexpr const char* kWebUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36";
constexpr const char* kYoutubeOriginHeader = "Origin: https://www.youtube.com";

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string get_string(const json& node, const char* key) {
    if (!node.is_object() || !node.contains(key) || node.at(key).is_null()) {
        return {};
    }

    if (node.at(key).is_string()) {
        return node.at(key).get<std::string>();
    }

    return {};
}

std::optional<std::string> find_query_value(const std::string& url, const std::string& key) {
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
            return url.substr(value_start, value_end == std::string::npos ? std::string::npos
                                                                          : value_end - value_start);
        }

        search_from = pos + 1;
    }
}

std::optional<std::string> extract_path_video_id(
    const std::string& url,
    const std::string& marker,
    bool stop_at_next_slash) {
    const size_t pos = url.find(marker);
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    const size_t id_start = pos + marker.size();
    size_t id_end = url.find_first_of("?#", id_start);
    if (stop_at_next_slash) {
        const size_t slash = url.find('/', id_start);
        if (slash != std::string::npos && (id_end == std::string::npos || slash < id_end)) {
            id_end = slash;
        }
    }

    if (id_start >= url.size()) {
        return std::nullopt;
    }

    return url.substr(id_start, id_end == std::string::npos ? std::string::npos : id_end - id_start);
}

std::optional<json> pick_preferred_format(const json& formats, int preferred_height) {
    if (!formats.is_array()) {
        return std::nullopt;
    }

    std::optional<json> preferred_height_match;
    std::optional<json> best_under_match;
    std::optional<json> best_over_match;
    int best_under_height_value = -1;
    int best_over_height_value = 1 << 30;

    for (const auto& format : formats) {
        if (!format.is_object() || !format.contains("url") || !format.at("url").is_string()) {
            continue;
        }

        const std::string mime = get_string(format, "mimeType");
        if (mime.find("video/mp4") == std::string::npos) {
            continue;
        }

        const int itag = format.value("itag", -1);
        if (itag == 22 && preferred_height >= 720) {
            // itag 22 is the 720p progressive muxed stream; only short-circuit to
            // it when the caller actually wants 720p or higher.
            return format;
        }

        const int height = format.value("height", -1);
        if (height == preferred_height) {
            preferred_height_match = format;
            continue;
        }

        if (height > 0 && height < preferred_height) {
            if (!best_under_match.has_value() || height > best_under_height_value) {
                best_under_match = format;
                best_under_height_value = height;
            }
            continue;
        }

        if (height > preferred_height
            && (!best_over_match.has_value() || height < best_over_height_value)) {
            best_over_match = format;
            best_over_height_value = height;
        }
    }

    if (preferred_height_match.has_value()) {
        return preferred_height_match;
    }

    if (best_under_match.has_value()) {
        return best_under_match;
    }

    if (best_over_match.has_value()) {
        return best_over_match;
    }

    return std::nullopt;
}

std::optional<ResolvedPlayback> build_progressive_playback(
    const std::optional<json>& selected,
    const std::string& video_id) {
    if (!selected.has_value()) {
        return std::nullopt;
    }

    ResolvedPlayback result;
    result.stream_url = get_string(*selected, "url");
    result.referer = "https://www.youtube.com/watch?v=" + video_id;
    result.http_header_fields = kYoutubeOriginHeader;
    result.quality_label = get_string(*selected, "qualityLabel");
    return result.stream_url.empty() ? std::nullopt : std::optional<ResolvedPlayback>(std::move(result));
}

std::optional<json> pick_preferred_adaptive_video_format(const json& adaptive_formats, int preferred_height) {
    if (!adaptive_formats.is_array()) {
        return std::nullopt;
    }

    std::optional<json> preferred_height_match;
    std::optional<json> best_under_match;
    std::optional<json> best_over_match;
    int best_under_height_value = -1;
    int best_over_height_value = 1 << 30;

    for (const auto& format : adaptive_formats) {
        if (!format.is_object() || !format.contains("url") || !format.at("url").is_string()) {
            continue;
        }

        const std::string mime = get_string(format, "mimeType");
        if (mime.find("video/mp4") == std::string::npos
            || mime.find("mp4a") != std::string::npos
            || mime.find("avc1") == std::string::npos) {
            continue;
        }

        const int height = format.value("height", -1);
        if (height == preferred_height) {
            preferred_height_match = format;
            continue;
        }

        if (height > 0 && height < preferred_height) {
            if (!best_under_match.has_value() || height > best_under_height_value) {
                best_under_match = format;
                best_under_height_value = height;
            }
            continue;
        }

        if (height > preferred_height
            && (!best_over_match.has_value() || height < best_over_height_value)) {
            best_over_match = format;
            best_over_height_value = height;
        }
    }

    if (preferred_height_match.has_value()) {
        return preferred_height_match;
    }

    if (best_under_match.has_value()) {
        return best_under_match;
    }

    return best_over_match;
}

std::optional<json> pick_preferred_adaptive_audio_format(const json& adaptive_formats) {
    if (!adaptive_formats.is_array()) {
        return std::nullopt;
    }

    std::optional<json> preferred;
    int preferred_bitrate = -1;

    for (const auto& format : adaptive_formats) {
        if (!format.is_object() || !format.contains("url") || !format.at("url").is_string()) {
            continue;
        }

        const std::string mime = get_string(format, "mimeType");
        if (mime.find("audio/mp4") == std::string::npos) {
            continue;
        }

        const int bitrate = format.value("bitrate", 0);
        if (!preferred.has_value() || bitrate > preferred_bitrate) {
            preferred = format;
            preferred_bitrate = bitrate;
        }
    }

    return preferred;
}

bool has_preferred_adaptive_mp4(const json& adaptive_formats, int preferred_height) {
    return pick_preferred_adaptive_video_format(adaptive_formats, preferred_height).has_value();
}

std::optional<ResolvedPlayback> build_adaptive_split_playback(
    const json& adaptive_formats,
    const std::string& video_id,
    int preferred_height) {
    const auto selected_video = pick_preferred_adaptive_video_format(adaptive_formats, preferred_height);
    const auto selected_audio = pick_preferred_adaptive_audio_format(adaptive_formats);
    if (!selected_video.has_value() || !selected_audio.has_value()) {
        return std::nullopt;
    }

    ResolvedPlayback result;
    result.stream_url = get_string(*selected_video, "url");
    result.external_audio_url = get_string(*selected_audio, "url");
    result.referer = "https://www.youtube.com/watch?v=" + video_id;
    result.http_header_fields = kYoutubeOriginHeader;
    const int selected_height = selected_video->value("height", preferred_height);
    result.quality_label = std::to_string(selected_height > 0 ? selected_height : preferred_height) + "p AVC";
    if (result.stream_url.empty() || result.external_audio_url.empty()) {
        return std::nullopt;
    }

    logf("youtube: selected adaptive split video=%s height=%d audio_itag=%d",
         video_id.c_str(),
         selected_height,
         selected_audio->value("itag", -1));
    return result;
}

std::optional<json> fetch_player_response(
    HttpClient* client,
    const std::string& video_id,
    const json& client_payload,
    const std::vector<HttpHeader>& headers,
    std::string& error_message) {
    json payload = {
        {"videoId", video_id},
        {"contentCheckOk", true},
        {"racyCheckOk", true},
        {"context", {{"client", client_payload}}},
    };
    const auto response_body = client->post(kPlayerApiUrl, payload.dump(), headers);
    if (!response_body.has_value() || response_body->empty()) {
        error_message = "YouTube player API request failed";
        return std::nullopt;
    }

    const json root = json::parse(*response_body, nullptr, false);
    if (root.is_discarded()) {
        error_message = "YouTube player response parse failed";
        return std::nullopt;
    }

    const json playability = root.value("playabilityStatus", json::object());
    const std::string playability_status = get_string(playability, "status");
    if (!playability_status.empty() && playability_status != "OK") {
        const std::string reason = get_string(playability, "reason");
        error_message = "YouTube playback unavailable: "
            + (reason.empty() ? playability_status : reason);
        return std::nullopt;
    }

    return root;
}

std::string generate_web_visitor_id() {
    constexpr char kVisitorAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    static std::atomic<uint64_t> sequence{0};
    uint64_t state = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count())
        ^ (++sequence * 0x9e3779b97f4a7c15ULL);

    std::string result(11, 'A');
    for (char& ch : result) {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        ch = kVisitorAlphabet[state & 0x3fU];
    }
    return result;
}

std::string fetch_web_visitor_data(HttpClient* client, std::string& error_message) {
    const auto response = client->get(
        "https://www.youtube.com/sw.js_data",
        {
            {"Accept-Language", "en-US"},
            {"Accept", "*/*"},
            {"User-Agent", kWebUserAgent},
            {"Referer", "https://www.youtube.com/sw.js"},
            {"Cookie", "PREF=tz=Asia.Seoul;VISITOR_INFO1_LIVE=" + generate_web_visitor_id() + ";"},
        });
    if (!response.has_value()) {
        error_message = "YouTube WEB session request failed";
        return {};
    }

    const size_t json_start = response->find('[');
    if (json_start == std::string::npos) {
        error_message = "YouTube WEB session parse failed";
        return {};
    }
    const json data = json::parse(response->substr(json_start), nullptr, false);
    if (data.is_discarded() || !data.is_array()) {
        error_message = "YouTube WEB session parse failed";
        return {};
    }

    try {
        const json& device_info = data.at(0).at(2).at(0).at(0);
        if (device_info.is_array() && device_info.size() > 13 && device_info.at(13).is_string()) {
            return device_info.at(13).get<std::string>();
        }
    } catch (const json::exception&) {
    }
    error_message = "YouTube WEB visitorData missing";
    return {};
}

std::vector<HttpHeader> android_player_headers(const std::string& cookie = {}) {
    std::vector<HttpHeader> headers = {
        {"Content-Type", "application/json"},
        {"User-Agent", kAndroidUserAgent},
        {"X-Youtube-Client-Name", "3"},
        {"X-Youtube-Client-Version", "20.10.38"},
        {"Origin", "https://www.youtube.com"},
    };
    if (!cookie.empty()) {
        headers.push_back({"Cookie", cookie});
    }
    return headers;
}

#ifdef __SWITCH__
std::vector<HttpHeader> android_vr_player_headers() {
    return {
        {"Content-Type", "application/json"},
        {"User-Agent", kAndroidVrUserAgent},
        {"X-Youtube-Client-Name", "28"},
        {"X-Youtube-Client-Version", "1.65.10"},
        {"Origin", "https://www.youtube.com"},
    };
}

void enable_ump(ResolvedPlayback& playback, int height) {
    playback.use_ump = true;
    playback.quality_label = std::to_string(height > 0 ? height : 720) + "p AVC UMP";
}
#endif

std::string extract_attribute(const std::string& line, const std::string& key) {
    const std::string pattern = key + "=";
    const size_t start = line.find(pattern);
    if (start == std::string::npos) {
        return {};
    }

    size_t value_start = start + pattern.size();
    if (value_start >= line.size()) {
        return {};
    }

    if (line[value_start] == '"') {
        value_start++;
        const size_t value_end = line.find('"', value_start);
        return line.substr(value_start, value_end == std::string::npos
            ? std::string::npos
            : value_end - value_start);
    }

    const size_t value_end = line.find(',', value_start);
    return line.substr(value_start, value_end == std::string::npos
        ? std::string::npos
        : value_end - value_start);
}

int extract_int_attribute(const std::string& line, const std::string& key) {
    const std::string value = extract_attribute(line, key);
    if (value.empty()) {
        return 0;
    }

    try {
        return std::stoi(value);
    } catch (...) {
        return 0;
    }
}

int extract_resolution_height(const std::string& stream_inf) {
    const std::string resolution = extract_attribute(stream_inf, "RESOLUTION");
    const size_t separator = resolution.find('x');
    if (separator == std::string::npos) {
        return -1;
    }

    try {
        return std::stoi(resolution.substr(separator + 1));
    } catch (...) {
        return -1;
    }
}

std::string absolutize_url(const std::string& base_url, const std::string& candidate_url) {
    if (candidate_url.empty() || candidate_url.find("://") != std::string::npos) {
        return candidate_url;
    }

    const size_t scheme_pos = base_url.find("://");
    if (scheme_pos == std::string::npos) {
        return candidate_url;
    }

    const size_t host_end = base_url.find('/', scheme_pos + 3);
    if (!candidate_url.empty() && candidate_url[0] == '/') {
        return base_url.substr(0, host_end) + candidate_url;
    }

    const size_t directory_end = base_url.rfind('/');
    if (directory_end == std::string::npos) {
        return candidate_url;
    }

    return base_url.substr(0, directory_end + 1) + candidate_url;
}

struct MediaEntry {
    std::string group_id;
    std::string type;
    std::string name;
    std::string language;
    std::string content_id;
    std::string uri;
    std::string raw_line;
    bool is_default = false;
    bool auto_select = false;
};

bool parse_yes_no_attribute(const std::string& line, const std::string& key) {
    return to_lower(extract_attribute(line, key)) == "yes";
}

std::optional<MediaEntry> pick_preferred_media_entry(
    const std::vector<MediaEntry>& media_entries,
    const std::string& group_id,
    const std::string& type) {
    std::optional<MediaEntry> original_entry;
    std::optional<MediaEntry> english_entry;
    std::optional<MediaEntry> fallback;

    for (const auto& entry : media_entries) {
        if (entry.group_id != group_id || entry.type != type) {
            continue;
        }

        if (entry.is_default) {
            return entry;
        }

        const std::string lower_name = to_lower(entry.name);
        const std::string lower_language = to_lower(entry.language);
        const std::string lower_content_id = to_lower(entry.content_id);

        if (lower_name.find("original") != std::string::npos
            || lower_content_id.find("original") != std::string::npos
            || lower_content_id.find(".4") != std::string::npos) {
            original_entry = entry;
            continue;
        }

        if (lower_language == "en" || lower_language == "en-us" || lower_name.find("english") != std::string::npos) {
            english_entry = entry;
            continue;
        }

        if (!fallback.has_value() || entry.name == "Default") {
            fallback = entry;
        }
    }

    if (original_entry.has_value()) {
        return original_entry;
    }

    if (english_entry.has_value()) {
        return english_entry;
    }

    return fallback;
}

struct SelectedHlsPlayback {
    std::string video_url;
    std::string audio_url;
    std::string audio_language;
    int bitrate = 0;
    int selected_height = -1;
};

std::optional<SelectedHlsPlayback> pick_preferred_hls_playback(
    const std::string& manifest_url,
    const std::string& manifest_body,
    int preferred_height) {
    std::vector<std::string> lines;
    std::stringstream stream(manifest_body);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    std::vector<MediaEntry> media_entries;
    std::string selected_stream_inf;
    std::string selected_uri;
    std::string selected_audio_group;
    int selected_bitrate = 0;
    int selected_height = -1;
    int best_under_height = -1;
    int best_over_height = 1 << 30;

    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& current = lines[i];
        if (current.rfind("#EXT-X-MEDIA:", 0) == 0) {
            MediaEntry entry;
            entry.group_id = extract_attribute(current, "GROUP-ID");
            entry.type = extract_attribute(current, "TYPE");
            entry.name = extract_attribute(current, "NAME");
            entry.language = extract_attribute(current, "LANGUAGE");
            entry.content_id = extract_attribute(current, "YT-EXT-AUDIO-CONTENT-ID");
            entry.uri = absolutize_url(manifest_url, extract_attribute(current, "URI"));
            entry.raw_line = current;
            entry.is_default = parse_yes_no_attribute(current, "DEFAULT");
            entry.auto_select = parse_yes_no_attribute(current, "AUTOSELECT");
            if (!entry.group_id.empty() && !entry.type.empty()) {
                media_entries.push_back(std::move(entry));
            }
            continue;
        }

        if (current.rfind("#EXT-X-STREAM-INF:", 0) != 0 || i + 1 >= lines.size()) {
            continue;
        }

        const int height = extract_resolution_height(current);
        const std::string candidate_uri = absolutize_url(manifest_url, lines[i + 1]);
        auto choose_candidate = [&]() {
            selected_stream_inf = current;
            selected_uri = candidate_uri;
            selected_audio_group = extract_attribute(current, "AUDIO");
            selected_height = height;
            selected_bitrate = extract_int_attribute(current, "BANDWIDTH");
        };

        if (height == preferred_height) {
            choose_candidate();
            best_under_height = preferred_height;
            break;
        }

        if (height > 0 && height < preferred_height) {
            if (height > best_under_height) {
                best_under_height = height;
                choose_candidate();
            }
            continue;
        }

        if (height > preferred_height && height < best_over_height && best_under_height < 0) {
            best_over_height = height;
            choose_candidate();
        }
    }

    if (selected_stream_inf.empty() || selected_uri.empty()) {
        return std::nullopt;
    }

    SelectedHlsPlayback result;
    result.video_url = selected_uri;
    result.bitrate = selected_bitrate;
    result.selected_height = selected_height;
    std::optional<MediaEntry> selected_audio_entry;
    if (!selected_audio_group.empty()) {
        selected_audio_entry = pick_preferred_media_entry(media_entries, selected_audio_group, "AUDIO");
        if (selected_audio_entry.has_value()) {
            result.audio_url = selected_audio_entry->uri;
            result.audio_language = selected_audio_entry->language;
        }
    }
    return result;
}


std::optional<ResolvedPlayback> resolve_visionos_hls_playback(
    HttpClient* client,
    const std::string& video_id,
    const std::string& visitor_data,
    int preferred_height,
    std::string& error_message) {
    const int requested_height = preferred_height;
    const auto root = fetch_player_response(
        client,
        video_id,
        {
            {"clientName", "VISIONOS"},
            {"clientVersion", "1.02"},
            {"deviceMake", "Apple"},
            {"deviceModel", "RealityDevice17,1"},
            {"osName", "visionOS"},
            {"osVersion", "26.5.23O471"},
            {"hl", "en"},
            {"gl", "US"},
            {"visitorData", visitor_data},
        },
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kVisionOsUserAgent},
            {"X-Youtube-Client-Name", "101"},
            {"X-Youtube-Client-Version", "1.02"},
            {"Origin", "https://www.youtube.com"},
        },
        error_message);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const json streaming = root->value("streamingData", json::object());
    const std::string hls_manifest_url = get_string(streaming, "hlsManifestUrl");
    if (hls_manifest_url.empty()) {
        error_message = "visionOS HLS manifest unavailable";
        return std::nullopt;
    }

    const auto master_manifest = client->get(hls_manifest_url);
    if (!master_manifest.has_value() || master_manifest->empty()) {
        error_message = "visionOS HLS manifest download failed";
        return std::nullopt;
    }

    const auto selected_playback = pick_preferred_hls_playback(
        hls_manifest_url, *master_manifest, requested_height);
    if (!selected_playback.has_value()) {
        error_message = std::to_string(requested_height) + "p HLS variant not found";
        return std::nullopt;
    }

    ResolvedPlayback result;
    result.stream_url = hls_manifest_url;
    result.referer = "https://www.youtube.com/watch?v=" + video_id;
    result.http_header_fields = kYoutubeOriginHeader;
    result.hls_bitrate = selected_playback->bitrate;
    // The visionOS HLS master lists several audio renditions (incl. YouTube AI
    // "dubbed-auto" tracks) with none marked DEFAULT, so mpv otherwise picks the
    // dub or silence. Steer mpv to the original-language rendition via --alang.
    result.audio_language = selected_playback->audio_language;
    const int selected_height =
        selected_playback->selected_height > 0 ? selected_playback->selected_height : requested_height;
    result.quality_label = std::to_string(selected_height) + "p HLS";
    logf("youtube: selected HLS variant video=%s height=%d bitrate=%d audio=%s",
         video_id.c_str(),
         selected_height,
         result.hls_bitrate,
         selected_playback->audio_url.empty() ? "none" : "yes");
    return result;
}

void report_status(const ResolverStatusCallback& callback, const std::string& title, const std::string& detail) {
    if (callback) {
        callback(title, detail);
    }
}

}  // namespace

int preferred_height_for_quality(PlaybackQualityMode mode) {
    switch (mode) {
        case PlaybackQualityMode::HD_1080:
            return 1080;
        case PlaybackQualityMode::HD_720:
            return 720;
        case PlaybackQualityMode::LOW_320:
            return 360;
        case PlaybackQualityMode::BEST:
        default:
#ifdef __SWITCH__
            return appletGetOperationMode() == AppletOperationMode_Console ? 1080 : 720;
#else
            return 1080;
#endif
    }
}

YouTubeResolver::YouTubeResolver(HttpClient* client)
    : client_(client ? client : &owned_client_),
      throttle_decrypter_(client ? client : &owned_client_) {
}

void YouTubeResolver::apply_throttle_transform(ResolvedPlayback& playback) {
    if (!playback.stream_url.empty()) {
        playback.stream_url = throttle_decrypter_.apply(playback.stream_url);
    }
    if (!playback.external_audio_url.empty()) {
        playback.external_audio_url = throttle_decrypter_.apply(playback.external_audio_url);
    }
    if (!playback.fallback_stream_url.empty()) {
        playback.fallback_stream_url = throttle_decrypter_.apply(playback.fallback_stream_url);
    }
    if (!playback.fallback_external_audio_url.empty()) {
        playback.fallback_external_audio_url = throttle_decrypter_.apply(playback.fallback_external_audio_url);
    }
}

std::optional<ResolvedPlayback> YouTubeResolver::resolve(
    const std::string& url,
    std::string& error_message,
    ResolverStatusCallback on_status) {
    auto result = resolve_internal(url, error_message, on_status);
    if (result.has_value()) {
        apply_throttle_transform(*result);
    }
    return result;
}

std::optional<ResolvedPlayback> YouTubeResolver::resolve_internal(
    const std::string& url,
    std::string& error_message,
    ResolverStatusCallback on_status) {
    const auto video_id = extract_video_id(url);
    if (!video_id.has_value()) {
        error_message = "not a supported YouTube URL";
        return std::nullopt;
    }

    report_status(on_status, "RESOLVING YOUTUBE STREAM", "CONTACTING PLAYER API");
    const AppSettings settings = SettingsStore::instance().settings();
    const int preferred_height = preferred_height_for_quality(settings.playback_quality);
    // Low quality (<=360p) is served most reliably by the muxed progressive
    // stream (itag 18), which is small and throttle-free. Higher qualities go
    // through the adaptive/HLS path so we can reach 720p and 1080p.
    const bool prefer_progressive_first = preferred_height <= 360;
    const bool allow_adaptive = preferred_height >= 720;
    logf("youtube: quality mode=%d preferred_height=%d",
         static_cast<int>(settings.playback_quality),
         preferred_height);

    const auto root = fetch_player_response(
        client_,
        *video_id,
        {
            {"clientName", "ANDROID"},
            {"clientVersion", "20.10.38"},
            {"androidSdkVersion", 30},
            {"hl", "en"},
            {"gl", "US"},
        },
        android_player_headers(),
        error_message);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const json streaming = root->value("streamingData", json::object());
    report_status(on_status, "RESOLVING YOUTUBE STREAM", "SELECTING PLAYABLE FORMAT");
    const auto progressive_playback = build_progressive_playback(
        pick_preferred_format(streaming.value("formats", json::array()), preferred_height),
        *video_id);

    if (prefer_progressive_first && progressive_playback.has_value()) {
        return progressive_playback;
    }

    if (allow_adaptive) {
        const auto adaptive_playback =
            build_adaptive_split_playback(
                streaming.value("adaptiveFormats", json::array()), *video_id, preferred_height);

        // visitorData unlocks the Apple Vision Pro (visionOS) player response,
        // which is currently the only client that yields full-length 720p/1080p
        // streams without a PO token. Fetched once and reused by the UMP path.
        std::string visitor_error;
        const std::string visitor_data = fetch_web_visitor_data(client_, visitor_error);

        report_status(on_status, "RESOLVING YOUTUBE STREAM", "REQUESTING HLS STREAM");
        std::string vision_error;
        if (!visitor_data.empty()) {
            if (const auto vision_playback = resolve_visionos_hls_playback(
                    client_, *video_id, visitor_data, preferred_height, vision_error)) {
                auto result = *vision_playback;
                // Fallback: progressive (ratebypass=yes, no throttle) > adaptive.
                if (progressive_playback.has_value()) {
                    result.fallback_stream_url = progressive_playback->stream_url;
                    result.fallback_referer = progressive_playback->referer;
                    result.fallback_http_header_fields = progressive_playback->http_header_fields;
                    result.fallback_quality_label = progressive_playback->quality_label;
                } else if (adaptive_playback.has_value()) {
                    result.fallback_stream_url = adaptive_playback->stream_url;
                    result.fallback_referer = adaptive_playback->referer;
                    result.fallback_http_header_fields = adaptive_playback->http_header_fields;
                    result.fallback_quality_label = adaptive_playback->quality_label;
                    result.fallback_external_audio_url = adaptive_playback->external_audio_url;
                }
                return result;
            }
        }
        logf("youtube: visionOS %dp HLS unavailable video=%s error=%s",
             preferred_height,
             video_id->c_str(),
             visitor_data.empty() ? visitor_error.c_str() : vision_error.c_str());

#ifdef __SWITCH__
        // Legacy tokenless Android VR UMP path (kept as a last resort; YouTube now
        // 403s these beyond the initial burst, so it rarely helps).
        std::string ump_error;
        if (!visitor_data.empty() && adaptive_playback.has_value()) {
            report_status(on_status, "RESOLVING YOUTUBE STREAM", "REQUESTING UMP STREAM");
            json vr_client = {
                {"clientName", "ANDROID_VR"},
                {"clientVersion", "1.65.10"},
                {"androidSdkVersion", 32},
                {"deviceMake", "Oculus"},
                {"deviceModel", "Quest 3"},
                {"hl", "en"},
                {"gl", "US"},
                {"visitorData", visitor_data},
            };
            const auto vr_root = fetch_player_response(
                client_,
                *video_id,
                vr_client,
                android_vr_player_headers(),
                ump_error);
            if (vr_root.has_value()) {
                const json vr_streaming =
                    vr_root->value("streamingData", json::object());
                auto ump_playback = build_adaptive_split_playback(
                    vr_streaming.value("adaptiveFormats", json::array()),
                    *video_id,
                    preferred_height);
                if (ump_playback.has_value()) {
                    enable_ump(*ump_playback, preferred_height);
                    if (progressive_playback.has_value()) {
                        ump_playback->fallback_stream_url = progressive_playback->stream_url;
                        ump_playback->fallback_referer = progressive_playback->referer;
                        ump_playback->fallback_http_header_fields = progressive_playback->http_header_fields;
                        ump_playback->fallback_quality_label = progressive_playback->quality_label;
                    }
                    logf("youtube: selected tokenless Android VR UMP video=%s", video_id->c_str());
                    return *ump_playback;
                }
                ump_error = "Android VR response did not contain "
                    + std::to_string(preferred_height) + "p split formats";
            }
            logf("youtube: tokenless UMP path unavailable video=%s error=%s",
                 video_id->c_str(), ump_error.c_str());
        } else if (visitor_data.empty()) {
            logf("youtube: UMP skipped video=%s error=%s", video_id->c_str(), ump_error.c_str());
        }
#endif

        // Retain the reliable 360p fallback if Android VR UMP is unavailable.
        if (progressive_playback.has_value()) {
            report_status(on_status, "RESOLVING YOUTUBE STREAM", "REQUESTING PROGRESSIVE STREAM");
            auto result = *progressive_playback;
            if (adaptive_playback.has_value()) {
                result.fallback_stream_url = adaptive_playback->stream_url;
                result.fallback_referer = adaptive_playback->referer;
                result.fallback_http_header_fields = adaptive_playback->http_header_fields;
                result.fallback_quality_label = adaptive_playback->quality_label;
                result.fallback_external_audio_url = adaptive_playback->external_audio_url;
            }
            return result;
        }

        if (adaptive_playback.has_value()) {
            report_status(on_status, "RESOLVING YOUTUBE STREAM", "REQUESTING AVC STREAM");
            return *adaptive_playback;
        }
    }

    if (progressive_playback.has_value()) {
        return progressive_playback;
    }

    const std::string dash_manifest_url = get_string(streaming, "dashManifestUrl");
    if (allow_adaptive
        && !dash_manifest_url.empty()
        && has_preferred_adaptive_mp4(
               streaming.value("adaptiveFormats", json::array()), preferred_height)) {
        ResolvedPlayback result;
        result.stream_url = dash_manifest_url;
        result.referer = "https://www.youtube.com/watch?v=" + *video_id;
        result.quality_label = std::to_string(preferred_height) + "p DASH";
        return result;
    }

    if (progressive_playback.has_value()) {
        return progressive_playback;
    }

    const std::string hls_manifest_url = get_string(streaming, "hlsManifestUrl");
    if (!hls_manifest_url.empty()) {
        ResolvedPlayback result;
        result.stream_url = hls_manifest_url;
        result.referer = "https://www.youtube.com/watch?v=" + *video_id;
        result.quality_label = "HLS";
        result.is_live = true;
        return result;
    }

    if (!dash_manifest_url.empty()) {
        ResolvedPlayback result;
        result.stream_url = dash_manifest_url;
        result.referer = "https://www.youtube.com/watch?v=" + *video_id;
        result.quality_label = "DASH";
        return result;
    }

    error_message = "YouTube response did not contain a playable stream URL";
    return std::nullopt;
}

bool YouTubeResolver::is_youtube_url(const std::string& url) {
    return extract_video_id(url).has_value();
}

std::optional<std::string> YouTubeResolver::extract_video_id(const std::string& url) {
    const std::string lower = to_lower(url);
    if (lower.find("youtube.com") == std::string::npos && lower.find("youtu.be/") == std::string::npos) {
        return std::nullopt;
    }

    if (const auto value = find_query_value(url, "v"); value.has_value() && !value->empty()) {
        return value;
    }

    if (const auto value = extract_path_video_id(url, "youtu.be/", true); value.has_value() && !value->empty()) {
        return value;
    }

    if (const auto value = extract_path_video_id(url, "/embed/", true); value.has_value() && !value->empty()) {
        return value;
    }

    if (const auto value = extract_path_video_id(url, "/shorts/", true); value.has_value() && !value->empty()) {
        return value;
    }

    return std::nullopt;
}

}  // namespace newpipe
