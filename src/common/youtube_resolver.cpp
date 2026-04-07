#include "newpipe/youtube_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"
#include "newpipe/settings_store.hpp"

namespace newpipe {
namespace {

using nlohmann::json;

constexpr const char* kPlayerApiUrl = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
constexpr const char* kAndroidUserAgent =
    "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
constexpr const char* kIosUserAgent =
    "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3 like Mac OS X)";
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
        if (itag == 22) {
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
        }
    }
    return result;
}

std::optional<ResolvedPlayback> resolve_ios_hls_playback(
    HttpClient* client,
    const std::string& video_id,
    int preferred_height,
    std::string& error_message) {
    const int requested_height = preferred_height;
    const auto root = fetch_player_response(
        client,
        video_id,
        {
            {"clientName", "IOS"},
            {"clientVersion", "20.10.4"},
            {"deviceMake", "Apple"},
            {"deviceModel", "iPhone16,2"},
            {"hl", "en"},
            {"gl", "US"},
        },
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kIosUserAgent},
            {"X-Youtube-Client-Name", "5"},
            {"X-Youtube-Client-Version", "20.10.4"},
            {"Origin", "https://www.youtube.com"},
        },
        error_message);
    if (!root.has_value()) {
        return std::nullopt;
    }

    const json streaming = root->value("streamingData", json::object());
    const std::string hls_manifest_url = get_string(streaming, "hlsManifestUrl");
    if (hls_manifest_url.empty()) {
        error_message = "iOS HLS manifest unavailable";
        return std::nullopt;
    }

    const auto master_manifest = client->get(hls_manifest_url);
    if (!master_manifest.has_value() || master_manifest->empty()) {
        error_message = "iOS HLS manifest download failed";
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

YouTubeResolver::YouTubeResolver(HttpClient* client)
    : client_(client ? client : &owned_client_) {
}

std::optional<ResolvedPlayback> YouTubeResolver::resolve(
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
    const int preferred_height =
        settings.playback_quality == PlaybackQualityMode::DATA_SAVER ? 480 : 720;
    const bool prefer_progressive_first =
        settings.playback_quality == PlaybackQualityMode::COMPATIBILITY;
    const bool allow_adaptive_720 =
        settings.playback_quality == PlaybackQualityMode::STANDARD_720;

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
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kAndroidUserAgent},
            {"X-Youtube-Client-Name", "3"},
            {"X-Youtube-Client-Version", "20.10.38"},
            {"Origin", "https://www.youtube.com"},
        },
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

    if (allow_adaptive_720) {
        const auto adaptive_playback =
            build_adaptive_split_playback(streaming.value("adaptiveFormats", json::array()), *video_id, 720);
        report_status(on_status, "RESOLVING YOUTUBE STREAM", "REQUESTING 720P HLS STREAM");
        std::string ios_error;
        if (const auto ios_playback =
                resolve_ios_hls_playback(client_, *video_id, 720, ios_error)) {
            auto result = *ios_playback;
            if (adaptive_playback.has_value()) {
                result.fallback_stream_url = adaptive_playback->stream_url;
                result.fallback_referer = adaptive_playback->referer;
                result.fallback_http_header_fields = adaptive_playback->http_header_fields;
                result.fallback_quality_label = adaptive_playback->quality_label;
                result.fallback_external_audio_url = adaptive_playback->external_audio_url;
            } else if (progressive_playback.has_value()) {
                result.fallback_stream_url = progressive_playback->stream_url;
                result.fallback_referer = progressive_playback->referer;
                result.fallback_http_header_fields = progressive_playback->http_header_fields;
                result.fallback_quality_label = progressive_playback->quality_label;
            }
            return result;
        }
        logf("youtube: iOS 720p HLS fallback failed video=%s error=%s",
             video_id->c_str(),
             ios_error.c_str());

        if (adaptive_playback.has_value()) {
            report_status(on_status, "RESOLVING YOUTUBE STREAM", "REQUESTING 720P AVC STREAM");
            auto result = *adaptive_playback;
            if (progressive_playback.has_value()) {
                result.fallback_stream_url = progressive_playback->stream_url;
                result.fallback_referer = progressive_playback->referer;
                result.fallback_http_header_fields = progressive_playback->http_header_fields;
                result.fallback_quality_label = progressive_playback->quality_label;
            }
            return result;
        }
    }

    const std::string dash_manifest_url = get_string(streaming, "dashManifestUrl");
    if (allow_adaptive_720
        && !dash_manifest_url.empty()
        && has_preferred_adaptive_mp4(streaming.value("adaptiveFormats", json::array()), 720)) {
        ResolvedPlayback result;
        result.stream_url = dash_manifest_url;
        result.referer = "https://www.youtube.com/watch?v=" + *video_id;
        result.quality_label = "720p DASH";
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
