#include "newpipe/youtube_catalog_service.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"

namespace newpipe {
namespace {

using nlohmann::json;

constexpr const char* kSearchApiUrl = "https://www.youtube.com/youtubei/v1/search?prettyPrint=false";
constexpr const char* kBrowseApiUrl = "https://www.youtube.com/youtubei/v1/browse?prettyPrint=false";
constexpr const char* kPlayerApiUrl = "https://www.youtube.com/youtubei/v1/player?prettyPrint=false";
constexpr const char* kChannelFeedUrlPrefix = "https://www.youtube.com/feeds/videos.xml?channel_id=";
constexpr const char* kAndroidClientVersion = "20.10.38";
constexpr const char* kAndroidUserAgent =
    "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
constexpr const char* kWebUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36";
constexpr const char* kWebClientVersion = "2.20250403.01.00";

struct FeedPreset {
    const char* id;
    const char* title;
    const char* query;
    bool allow_short_videos = false;
};

constexpr std::array<FeedPreset, 4> kFeedPresets = {{
    {"recommended", "추천", "best videos today", false},
    {"live", "라이브", "live now", true},
    {"music", "음악", "music video", false},
    {"gaming", "게임", "gaming trailer", false},
}};

std::string get_string(const json& node, const char* key) {
    if (!node.is_object() || !node.contains(key) || node.at(key).is_null()) {
        return {};
    }

    if (node.at(key).is_string()) {
        return node.at(key).get<std::string>();
    }

    return {};
}

std::string get_text(const json& node) {
    if (node.is_null()) {
        return {};
    }

    if (node.is_string()) {
        return node.get<std::string>();
    }

    if (node.is_object()) {
        if (node.contains("content") && node.at("content").is_string()) {
            return node.at("content").get<std::string>();
        }

        if (node.contains("simpleText") && node.at("simpleText").is_string()) {
            return node.at("simpleText").get<std::string>();
        }

        if (node.contains("runs") && node.at("runs").is_array()) {
            std::string text;
            for (const auto& run : node.at("runs")) {
                const std::string part = get_string(run, "text");
                if (!part.empty()) {
                    text += part;
                }
            }
            return text;
        }
    }

    return {};
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

int parse_duration_seconds(const std::string& duration_text) {
    if (duration_text.empty()) {
        return -1;
    }

    int total = 0;
    int current = 0;
    int parts = 0;
    for (char ch : duration_text) {
        if (ch == ':') {
            total = total * 60 + current;
            current = 0;
            parts++;
            continue;
        }

        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return -1;
        }

        current = current * 10 + (ch - '0');
    }

    if (parts == 0 && current == 0) {
        return -1;
    }

    return total * 60 + current;
}

std::string get_thumbnail_url(const json& renderer) {
    if (!renderer.is_object()) {
        return {};
    }

    const auto thumbnail_it = renderer.find("thumbnail");
    if (thumbnail_it == renderer.end() || !thumbnail_it->is_object()) {
        return {};
    }

    const auto thumbs_it = thumbnail_it->find("thumbnails");
    if (thumbs_it == thumbnail_it->end() || !thumbs_it->is_array() || thumbs_it->empty()) {
        return {};
    }

    const auto& last = thumbs_it->back();
    return get_string(last, "url");
}

std::string get_channel_url(const json& text_node) {
    if (!text_node.is_object() || !text_node.contains("runs") || !text_node.at("runs").is_array()) {
        return {};
    }

    for (const auto& run : text_node.at("runs")) {
        if (!run.is_object() || !run.contains("navigationEndpoint")) {
            continue;
        }

        const auto& endpoint = run.at("navigationEndpoint");
        if (!endpoint.is_object()) {
            continue;
        }

        if (endpoint.contains("browseEndpoint") && endpoint.at("browseEndpoint").is_object()) {
            const auto& browse = endpoint.at("browseEndpoint");
            const std::string canonical = get_string(browse, "canonicalBaseUrl");
            if (!canonical.empty()) {
                return "https://www.youtube.com" + canonical;
            }

            const std::string browse_id = get_string(browse, "browseId");
            if (!browse_id.empty()) {
                return "https://www.youtube.com/channel/" + browse_id;
            }
        }

        if (endpoint.contains("commandMetadata") && endpoint.at("commandMetadata").is_object()) {
            const auto& metadata = endpoint.at("commandMetadata");
            if (metadata.contains("webCommandMetadata") && metadata.at("webCommandMetadata").is_object()) {
                const std::string url = get_string(metadata.at("webCommandMetadata"), "url");
                if (!url.empty() && url[0] == '/') {
                    return "https://www.youtube.com" + url;
                }
            }
        }
    }

    return {};
}

std::string get_channel_id(const json& text_node) {
    if (!text_node.is_object() || !text_node.contains("runs") || !text_node.at("runs").is_array()) {
        return {};
    }

    for (const auto& run : text_node.at("runs")) {
        if (!run.is_object() || !run.contains("navigationEndpoint")) {
            continue;
        }

        const auto& endpoint = run.at("navigationEndpoint");
        if (!endpoint.is_object() || !endpoint.contains("browseEndpoint")
            || !endpoint.at("browseEndpoint").is_object()) {
            continue;
        }

        const std::string browse_id = get_string(endpoint.at("browseEndpoint"), "browseId");
        if (!browse_id.empty()) {
            return browse_id;
        }
    }

    return {};
}

std::string get_thumbnail_url_from_node(const json& thumbnail_node) {
    if (!thumbnail_node.is_object()) {
        return {};
    }

    const auto thumbs_it = thumbnail_node.find("thumbnails");
    if (thumbs_it == thumbnail_node.end() || !thumbs_it->is_array() || thumbs_it->empty()) {
        return {};
    }

    const auto& last = thumbs_it->back();
    return get_string(last, "url");
}

std::string get_thumbnail_url_from_sources(const json& image_node) {
    if (!image_node.is_object()) {
        return {};
    }

    const auto sources_it = image_node.find("sources");
    if (sources_it == image_node.end() || !sources_it->is_array() || sources_it->empty()) {
        return {};
    }

    const auto& last = sources_it->back();
    return get_string(last, "url");
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

std::optional<std::string> extract_video_id_from_url(const std::string& url) {
    if (const auto value = find_query_value(url, "v")) {
        return value;
    }

    for (const char* marker : {"/watch/", "/shorts/", "/live/", "youtu.be/"}) {
        const size_t pos = url.find(marker);
        if (pos == std::string::npos) {
            continue;
        }

        const size_t value_start = pos + std::char_traits<char>::length(marker);
        const size_t value_end = url.find_first_of("/?#", value_start);
        if (value_start < url.size()) {
            return url.substr(
                value_start,
                value_end == std::string::npos ? std::string::npos : value_end - value_start);
        }
    }

    return std::nullopt;
}

std::optional<std::string> extract_playlist_id_from_url(const std::string& url) {
    if (const auto value = find_query_value(url, "list"); value.has_value() && !value->empty()) {
        return value;
    }

    return std::nullopt;
}

std::string extract_channel_id_from_url(const std::string& channel_url) {
    const std::string marker = "/channel/";
    const size_t pos = channel_url.find(marker);
    if (pos == std::string::npos) {
        return {};
    }

    const size_t value_start = pos + marker.size();
    const size_t value_end = channel_url.find_first_of("/?#", value_start);
    return channel_url.substr(
        value_start,
        value_end == std::string::npos ? std::string::npos : value_end - value_start);
}

std::string absolutize_youtube_url(const std::string& url) {
    if (url.empty()) {
        return {};
    }

    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) {
        return url;
    }

    if (url[0] == '/') {
        return "https://www.youtube.com" + url;
    }

    return url;
}

std::string get_command_url(const json& command_node) {
    if (!command_node.is_object()) {
        return {};
    }

    if (command_node.contains("commandMetadata") && command_node.at("commandMetadata").is_object()) {
        const auto& metadata = command_node.at("commandMetadata");
        if (metadata.contains("webCommandMetadata") && metadata.at("webCommandMetadata").is_object()) {
            return absolutize_youtube_url(get_string(metadata.at("webCommandMetadata"), "url"));
        }
    }

    if (command_node.contains("innertubeCommand") && command_node.at("innertubeCommand").is_object()) {
        return get_command_url(command_node.at("innertubeCommand"));
    }

    return {};
}

std::string format_duration_text_from_seconds(const std::string& raw_seconds) {
    if (raw_seconds.empty()) {
        return {};
    }

    int total = 0;
    try {
        total = std::stoi(raw_seconds);
    } catch (...) {
        return {};
    }

    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int seconds = total % 60;

    std::ostringstream stream;
    if (hours > 0) {
        stream << hours << ':';
        if (minutes < 10) {
            stream << '0';
        }
    }
    stream << minutes << ':';
    if (seconds < 10) {
        stream << '0';
    }
    stream << seconds;
    return stream.str();
}

std::string format_view_count_text(const std::string& raw_views) {
    if (raw_views.empty()) {
        return {};
    }
    return raw_views + " views";
}

std::string decode_xml_entities(std::string text) {
    const std::array<std::pair<const char*, const char*>, 5> replacements = {{
        {"&amp;", "&"},
        {"&quot;", "\""},
        {"&apos;", "'"},
        {"&lt;", "<"},
        {"&gt;", ">"},
    }};

    for (const auto& [from, to] : replacements) {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, std::char_traits<char>::length(from), to);
            pos += std::char_traits<char>::length(to);
        }
    }
    return text;
}

std::string extract_xml_tag(const std::string& xml, const std::string& tag) {
    const std::string open_tag = "<" + tag + ">";
    const std::string close_tag = "</" + tag + ">";
    const size_t start = xml.find(open_tag);
    if (start == std::string::npos) {
        return {};
    }
    const size_t value_start = start + open_tag.size();
    const size_t end = xml.find(close_tag, value_start);
    if (end == std::string::npos) {
        return {};
    }
    return decode_xml_entities(xml.substr(value_start, end - value_start));
}

std::string extract_xml_attribute(
    const std::string& xml,
    const std::string& tag_start,
    const std::string& attribute) {
    const size_t start = xml.find(tag_start);
    if (start == std::string::npos) {
        return {};
    }

    const std::string pattern = attribute + "=\"";
    const size_t attr_start = xml.find(pattern, start);
    if (attr_start == std::string::npos) {
        return {};
    }
    const size_t value_start = attr_start + pattern.size();
    const size_t value_end = xml.find('"', value_start);
    if (value_end == std::string::npos) {
        return {};
    }
    return decode_xml_entities(xml.substr(value_start, value_end - value_start));
}

bool is_live_renderer(const json& renderer) {
    const auto check_badge_list = [](const json& badge_list) {
        if (!badge_list.is_array()) {
            return false;
        }

        for (const auto& badge : badge_list) {
            if (!badge.is_object()) {
                continue;
            }

            std::string label;
            if (badge.contains("metadataBadgeRenderer")) {
                label = get_string(badge.at("metadataBadgeRenderer"), "label");
            } else if (badge.contains("thumbnailOverlayTimeStatusRenderer")) {
                label = get_text(badge.at("thumbnailOverlayTimeStatusRenderer").value("text", json::object()));
                if (label.empty()) {
                    label = get_string(badge.at("thumbnailOverlayTimeStatusRenderer"), "style");
                }
            } else if (badge.contains("badgeRenderer")) {
                label = get_string(badge.at("badgeRenderer"), "label");
            } else if (badge.contains("thumbnailOverlayNowPlayingRenderer")) {
                label = "LIVE";
            }

            if (contains_case_insensitive(label, "live")) {
                return true;
            }
        }

        return false;
    };

    return check_badge_list(renderer.value("badges", json::array()))
        || check_badge_list(renderer.value("thumbnailOverlays", json::array()));
}

std::string get_lockup_duration_text(const json& content_image) {
    const auto thumbnail_view = content_image.value("thumbnailViewModel", json::object());
    for (const auto& overlay : thumbnail_view.value("overlays", json::array())) {
        const auto overlay_model = overlay.value("thumbnailBottomOverlayViewModel", json::object());
        for (const auto& badge : overlay_model.value("badges", json::array())) {
            const std::string text = get_text(badge.value("thumbnailBadgeViewModel", json::object()).value(
                "text", json::object()));
            if (!text.empty()) {
                return text;
            }
        }
    }

    return {};
}

void parse_lockup_metadata_rows(
    const json& metadata_rows,
    std::string& channel_name,
    std::string& view_count_text,
    std::string& published_text) {
    if (!metadata_rows.is_array()) {
        return;
    }

    if (metadata_rows.size() >= 1) {
        const auto& row = metadata_rows.at(0);
        for (const auto& part : row.value("metadataParts", json::array())) {
            const std::string text = get_text(part.value("text", json::object()));
            if (!text.empty()) {
                channel_name = text;
                break;
            }
        }
    }

    if (metadata_rows.size() >= 2) {
        const auto& row = metadata_rows.at(1);
        std::vector<std::string> parts;
        for (const auto& part : row.value("metadataParts", json::array())) {
            const std::string text = get_text(part.value("text", json::object()));
            if (!text.empty()) {
                parts.push_back(text);
            }
        }

        if (!parts.empty()) {
            view_count_text = parts.front();
        }
        if (parts.size() >= 2) {
            published_text = parts.back();
        }
    }
}

std::optional<StreamItem> parse_lockup_stream_item(const json& lockup, bool allow_short_videos) {
    if (!lockup.is_object()) {
        return std::nullopt;
    }

    const std::string content_id = get_string(lockup, "contentId");
    if (content_id.empty()) {
        return std::nullopt;
    }

    const std::string content_type = get_string(lockup, "contentType");
    if (!content_type.empty() && !contains_case_insensitive(content_type, "video")) {
        return std::nullopt;
    }

    StreamItem item;
    item.id = content_id;

    const auto command = lockup.value("rendererContext", json::object())
                             .value("commandContext", json::object())
                             .value("onTap", json::object())
                             .value("innertubeCommand", json::object());
    item.url = get_command_url(command);
    if (item.url.empty()) {
        item.url = "https://www.youtube.com/watch?v=" + item.id;
    }
    if (!allow_short_videos && item.url.find("/shorts/") != std::string::npos) {
        return std::nullopt;
    }

    const auto metadata = lockup.value("metadata", json::object()).value("lockupMetadataViewModel", json::object());
    item.title = get_text(metadata.value("title", json::object()));

    parse_lockup_metadata_rows(
        metadata.value("metadata", json::object())
            .value("contentMetadataViewModel", json::object())
            .value("metadataRows", json::array()),
        item.channel_name,
        item.view_count_text,
        item.published_text);

    const auto avatar_command = metadata.value("image", json::object())
                                    .value("decoratedAvatarViewModel", json::object())
                                    .value("rendererContext", json::object())
                                    .value("commandContext", json::object())
                                    .value("onTap", json::object())
                                    .value("innertubeCommand", json::object());
    item.channel_url = get_command_url(avatar_command);
    item.channel_id =
        get_string(avatar_command.value("browseEndpoint", json::object()), "browseId");

    item.thumbnail_url = get_thumbnail_url_from_sources(
        lockup.value("contentImage", json::object())
            .value("thumbnailViewModel", json::object())
            .value("image", json::object()));
    item.duration_text = get_lockup_duration_text(lockup.value("contentImage", json::object()));
    item.is_live = contains_case_insensitive(item.duration_text, "live")
        || contains_case_insensitive(item.duration_text, "재생 중");
    if (item.is_live) {
        item.duration_text.clear();
    }

    if (item.title.empty()) {
        return std::nullopt;
    }

    const int duration_seconds = parse_duration_seconds(item.duration_text);
    if (!allow_short_videos && !item.is_live && duration_seconds > 0 && duration_seconds < 120) {
        return std::nullopt;
    }

    return item;
}

std::optional<StreamItem> parse_stream_item(const json& renderer, bool allow_short_videos) {
    if (!renderer.is_object()) {
        return std::nullopt;
    }

    StreamItem item;
    item.id = get_string(renderer, "videoId");
    if (item.id.empty()) {
        return std::nullopt;
    }

    item.url = "https://www.youtube.com/watch?v=" + item.id;
    item.title = get_text(renderer.value("title", json::object()));
    item.channel_name = get_text(renderer.value("longBylineText", json::object()));
    if (item.channel_name.empty()) {
        item.channel_name = get_text(renderer.value("shortBylineText", json::object()));
    }
    item.channel_id = get_channel_id(renderer.value("longBylineText", json::object()));
    if (item.channel_id.empty()) {
        item.channel_id = get_channel_id(renderer.value("shortBylineText", json::object()));
    }
    item.channel_url = get_channel_url(renderer.value("longBylineText", json::object()));
    if (item.channel_url.empty()) {
        item.channel_url = get_channel_url(renderer.value("shortBylineText", json::object()));
    }
    item.thumbnail_url = get_thumbnail_url(renderer);
    item.duration_text = get_text(renderer.value("lengthText", json::object()));
    item.view_count_text = get_text(renderer.value("viewCountText", json::object()));
    item.published_text = get_text(renderer.value("publishedTimeText", json::object()));
    item.is_live = is_live_renderer(renderer);

    if (item.title.empty()) {
        return std::nullopt;
    }

    const int duration_seconds = parse_duration_seconds(item.duration_text);
    if (!allow_short_videos && !item.is_live && duration_seconds > 0 && duration_seconds < 120) {
        return std::nullopt;
    }

    return item;
}

std::optional<StreamItem> parse_playlist_stream_item(const json& renderer, bool allow_short_videos) {
    if (!renderer.is_object()) {
        return std::nullopt;
    }

    StreamItem item;
    item.id = get_string(renderer, "videoId");
    if (item.id.empty() || !renderer.value("isPlayable", true)) {
        return std::nullopt;
    }

    item.url = get_command_url(renderer.value("navigationEndpoint", json::object()));
    if (item.url.empty()) {
        item.url = "https://www.youtube.com/watch?v=" + item.id;
    }
    item.title = get_text(renderer.value("title", json::object()));
    item.channel_name = get_text(renderer.value("shortBylineText", json::object()));
    item.channel_id = get_channel_id(renderer.value("shortBylineText", json::object()));
    item.channel_url = get_channel_url(renderer.value("shortBylineText", json::object()));
    item.thumbnail_url = get_thumbnail_url_from_node(renderer.value("thumbnail", json::object()));
    item.duration_text = get_text(renderer.value("lengthText", json::object()));
    if (item.duration_text.empty()) {
        item.duration_text = format_duration_text_from_seconds(get_string(renderer, "lengthSeconds"));
    }
    item.is_live = item.duration_text.empty();

    if (item.title.empty()) {
        return std::nullopt;
    }

    const int duration_seconds = parse_duration_seconds(item.duration_text);
    if (!allow_short_videos && !item.is_live && duration_seconds > 0 && duration_seconds < 120) {
        return std::nullopt;
    }

    return item;
}

void collect_stream_items(
    const json& node,
    bool allow_short_videos,
    size_t limit,
    std::unordered_set<std::string>& seen_ids,
    std::vector<StreamItem>& out_items) {
    if (out_items.size() >= limit) {
        return;
    }

    if (node.is_object()) {
        if (const auto item = parse_lockup_stream_item(node, allow_short_videos);
            item.has_value() && seen_ids.insert(item->id).second) {
            out_items.push_back(*item);
            if (out_items.size() >= limit) {
                return;
            }
        }

        for (const char* key : {"compactVideoRenderer", "videoRenderer", "gridVideoRenderer"}) {
            if (node.contains(key)) {
                const auto item = parse_stream_item(node.at(key), allow_short_videos);
                if (item.has_value() && seen_ids.insert(item->id).second) {
                    out_items.push_back(*item);
                    if (out_items.size() >= limit) {
                        return;
                    }
                }
            }
        }

        for (const auto& entry : node.items()) {
            collect_stream_items(entry.value(), allow_short_videos, limit, seen_ids, out_items);
            if (out_items.size() >= limit) {
                return;
            }
        }
        return;
    }

    if (node.is_array()) {
        for (const auto& child : node) {
            collect_stream_items(child, allow_short_videos, limit, seen_ids, out_items);
            if (out_items.size() >= limit) {
                return;
            }
        }
    }
}

void collect_playlist_items(
    const json& node,
    bool allow_short_videos,
    size_t limit,
    std::unordered_set<std::string>& seen_ids,
    std::vector<StreamItem>& out_items) {
    if (out_items.size() >= limit) {
        return;
    }

    if (node.is_object()) {
        if (node.contains("playlistVideoRenderer")) {
            const auto item = parse_playlist_stream_item(node.at("playlistVideoRenderer"), allow_short_videos);
            if (item.has_value() && seen_ids.insert(item->id).second) {
                out_items.push_back(*item);
                if (out_items.size() >= limit) {
                    return;
                }
            }
        }

        for (const auto& entry : node.items()) {
            collect_playlist_items(entry.value(), allow_short_videos, limit, seen_ids, out_items);
            if (out_items.size() >= limit) {
                return;
            }
        }
        return;
    }

    if (node.is_array()) {
        for (const auto& child : node) {
            collect_playlist_items(child, allow_short_videos, limit, seen_ids, out_items);
            if (out_items.size() >= limit) {
                return;
            }
        }
    }
}

std::optional<std::string> extract_json_assignment(
    const std::string& text,
    const std::string& marker) {
    const size_t marker_pos = text.find(marker);
    if (marker_pos == std::string::npos) {
        return std::nullopt;
    }

    size_t json_start = std::string::npos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (size_t i = marker_pos + marker.size(); i < text.size(); i++) {
        const char ch = text[i];
        if (json_start == std::string::npos) {
            if (ch == '{' || ch == '[') {
                json_start = i;
                depth = 1;
            }
            continue;
        }

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }

        if (ch == '{' || ch == '[') {
            depth++;
            continue;
        }

        if (ch == '}' || ch == ']') {
            depth--;
            if (depth == 0) {
                return text.substr(json_start, i - json_start + 1);
            }
        }
    }

    return std::nullopt;
}

std::optional<json> fetch_watch_page_initial_data(HttpClient* client, const std::string& watch_url) {
    const std::string watch_url_with_flags =
        watch_url + (watch_url.find('?') == std::string::npos ? "?" : "&")
        + "bpctr=9999999999&has_verified=1";
    const auto response = client->get(
        watch_url_with_flags,
        {
            {"User-Agent", kWebUserAgent},
            {"Accept-Language", "ko-KR,ko;q=0.9,en-US;q=0.8"},
            {"Cookie", "PREF=hl=ko&gl=KR"},
        });
    if (!response.has_value() || response->empty()) {
        return std::nullopt;
    }

    std::optional<std::string> json_text;
    for (const char* marker : {"var ytInitialData = ", "window[\"ytInitialData\"] = ", "ytInitialData = "}) {
        json_text = extract_json_assignment(*response, marker);
        if (json_text.has_value()) {
            break;
        }
    }
    if (!json_text.has_value()) {
        return std::nullopt;
    }

    const json initial_data = json::parse(*json_text, nullptr, false);
    if (initial_data.is_discarded()) {
        return std::nullopt;
    }

    return initial_data;
}

std::string extract_comments_continuation_token(const json& initial_data) {
    const json contents = initial_data.value("contents", json::object())
                              .value("twoColumnWatchNextResults", json::object())
                              .value("results", json::object())
                              .value("results", json::object())
                              .value("contents", json::array());

    std::string token;
    const auto walk = [&](const auto& self, const json& node) -> void {
        if (!token.empty() || node.is_null()) {
            return;
        }

        if (node.is_object()) {
            if (node.contains("continuationItemRenderer")
                && node.at("continuationItemRenderer").is_object()) {
                token = get_string(
                    node.at("continuationItemRenderer")
                        .value("continuationEndpoint", json::object())
                        .value("continuationCommand", json::object()),
                    "token");
                if (!token.empty()) {
                    return;
                }
            }

            for (const auto& entry : node.items()) {
                self(self, entry.value());
                if (!token.empty()) {
                    return;
                }
            }
            return;
        }

        if (node.is_array()) {
            for (const auto& child : node) {
                self(self, child);
                if (!token.empty()) {
                    return;
                }
            }
        }
    };

    walk(walk, contents);
    return token;
}

std::vector<StreamItem> fetch_watch_related_items(
    HttpClient* client,
    const std::string& watch_url,
    const std::string& current_video_id,
    bool allow_short_videos) {
    const auto initial_data = fetch_watch_page_initial_data(client, watch_url);
    if (!initial_data.has_value()) {
        return {};
    }

    const json secondary_results = initial_data->value("contents", json::object())
                                       .value("twoColumnWatchNextResults", json::object())
                                       .value("secondaryResults", json::object())
                                       .value("secondaryResults", json::object())
                                       .value("results", json::array());

    std::vector<StreamItem> items;
    std::unordered_set<std::string> seen_ids;
    if (!current_video_id.empty()) {
        seen_ids.insert(current_video_id);
    }
    collect_stream_items(secondary_results, allow_short_videos, 16, seen_ids, items);
    return items;
}

std::optional<CommentPage> fetch_comments_page(
    HttpClient* client,
    const std::string& continuation_token,
    size_t limit) {
    if (continuation_token.empty()) {
        return std::nullopt;
    }

    json payload = {
        {"continuation", continuation_token},
        {"context",
         {{"client",
           {{"clientName", "WEB"},
            {"clientVersion", kWebClientVersion},
            {"hl", "ko"},
            {"gl", "KR"},
            {"platform", "DESKTOP"}}}}},
    };

    const auto response = client->post(
        "https://www.youtube.com/youtubei/v1/next?prettyPrint=false",
        payload.dump(),
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kWebUserAgent},
            {"X-Youtube-Client-Name", "1"},
            {"X-Youtube-Client-Version", kWebClientVersion},
            {"Origin", "https://www.youtube.com"},
        });
    if (!response.has_value() || response->empty()) {
        return std::nullopt;
    }

    const json root = json::parse(*response, nullptr, false);
    if (root.is_discarded()) {
        return std::nullopt;
    }

    CommentPage page;
    json continuation_items = json::array();
    const json endpoints = root.value("onResponseReceivedEndpoints", json::array());
    if (endpoints.is_array() && !endpoints.empty() && endpoints.front().is_object()) {
        continuation_items = endpoints.front()
                                 .value("reloadContinuationItemsCommand", json::object())
                                 .value("continuationItems", json::array());
    }
    if (!continuation_items.empty()) {
        page.title = get_text(continuation_items.at(0).value("commentsHeaderRenderer", json::object()).value(
            "countText", json::object()));
    }

    const json mutations = root.value("frameworkUpdates", json::object())
                               .value("entityBatchUpdate", json::object())
                               .value("mutations", json::array());
    for (const auto& mutation : mutations) {
        if (!mutation.is_object() || !mutation.contains("payload") || !mutation.at("payload").is_object()) {
            continue;
        }

        const json payload_node = mutation.at("payload");
        if (!payload_node.contains("commentEntityPayload")
            || !payload_node.at("commentEntityPayload").is_object()) {
            continue;
        }

        const json comment = payload_node.at("commentEntityPayload");
        CommentItem item;
        item.body = get_text(comment.value("properties", json::object()).value("content", json::object()));
        item.published_text = get_string(comment.value("properties", json::object()), "publishedTime");
        item.author_name = get_string(comment.value("author", json::object()), "displayName");
        item.author_url = get_command_url(
            comment.value("author", json::object())
                .value("channelCommand", json::object())
                .value("innertubeCommand", json::object()));
        item.author_thumbnail_url = get_string(comment.value("author", json::object()), "avatarThumbnailUrl");
        item.like_count_text = get_string(comment.value("toolbar", json::object()), "likeCountNotliked");
        item.reply_count_text = get_string(comment.value("toolbar", json::object()), "replyCount");
        item.is_verified = comment.value("author", json::object()).value("isVerified", false);

        if (item.body.empty()) {
            continue;
        }

        page.items.push_back(std::move(item));
        if (page.items.size() >= limit) {
            break;
        }
    }

    if (page.items.empty()) {
        return std::nullopt;
    }

    if (page.title.empty()) {
        page.title = "댓글";
    }
    return page;
}

std::string extract_playlist_title(const json& root) {
    std::string title;
    const auto walk = [&](const auto& self, const json& node) -> void {
        if (!title.empty() || node.is_null()) {
            return;
        }

        if (node.is_object()) {
            if (node.contains("playlistSidebarPrimaryInfoRenderer")
                && node.at("playlistSidebarPrimaryInfoRenderer").is_object()) {
                title = get_text(
                    node.at("playlistSidebarPrimaryInfoRenderer").value("title", json::object()));
                if (!title.empty()) {
                    return;
                }
            }

            for (const auto& entry : node.items()) {
                self(self, entry.value());
                if (!title.empty()) {
                    return;
                }
            }
            return;
        }

        if (node.is_array()) {
            for (const auto& child : node) {
                self(self, child);
                if (!title.empty()) {
                    return;
                }
            }
        }
    };

    walk(walk, root);
    return title;
}

}  // namespace

YouTubeCatalogService::YouTubeCatalogService(HttpClient* client, AuthStore* auth_store)
    : client_(client ? client : &owned_client_)
    , auth_store_(auth_store ? auth_store : &AuthStore::instance()) {
    std::string ignored_error;
    this->auth_store_->load(&ignored_error);
}

bool YouTubeCatalogService::load_auth_session(std::string* error_message) {
    return this->auth_store_->load(error_message);
}

bool YouTubeCatalogService::reload_auth_session(std::string* error_message) {
    if (!this->auth_store_->reload(error_message)) {
        return false;
    }
    this->invalidate_auth_caches();
    return this->auth_store_->load(error_message);
}

bool YouTubeCatalogService::has_auth_session() const {
    return this->auth_store_->has_session();
}

AuthSession YouTubeCatalogService::auth_session() const {
    return this->auth_store_->session();
}

bool YouTubeCatalogService::import_auth_session_from_file(
    const std::string& file_path,
    std::string* error_message) {
    if (!this->auth_store_->import_from_file(file_path, error_message)) {
        return false;
    }
    this->invalidate_auth_caches();
    return true;
}

bool YouTubeCatalogService::update_auth_session_from_cookie(
    const std::string& cookie_header,
    const std::string& source_label,
    std::string* error_message) {
    if (!this->auth_store_->update_from_cookie_header(cookie_header, source_label, error_message)) {
        return false;
    }
    this->invalidate_auth_caches();
    return true;
}

bool YouTubeCatalogService::clear_auth_session(std::string* error_message) {
    if (!this->auth_store_->clear(error_message)) {
        return false;
    }
    this->invalidate_auth_caches();
    return true;
}

std::vector<Kiosk> YouTubeCatalogService::list_kiosks() const {
    std::vector<Kiosk> kiosks;
    kiosks.reserve(kFeedPresets.size());
    for (const auto& preset : kFeedPresets) {
        kiosks.push_back(Kiosk{preset.id, preset.title});
    }
    return kiosks;
}

std::optional<HomeFeed> YouTubeCatalogService::fetch_home_feed(
    const std::string& kiosk_id,
    const std::string& title,
    const std::string& query,
    bool allow_short_videos) const {
    const auto cache_it = home_feed_cache_.find(kiosk_id);
    if (cache_it != home_feed_cache_.end()) {
        error_message_.clear();
        return cache_it->second;
    }

    const auto results = fetch_search_results(query, 16, allow_short_videos);
    if (!error_message_.empty()) {
        logf("youtube: home feed failed id=%s error=%s", kiosk_id.c_str(), error_message_.c_str());
        return std::nullopt;
    }

    HomeFeed feed;
    feed.kiosk = {kiosk_id, title};
    feed.items = results.items;
    home_feed_cache_[kiosk_id] = feed;
    logf("youtube: home feed id=%s query=%s items=%zu",
         kiosk_id.c_str(),
         query.c_str(),
         feed.items.size());
    return feed;
}

std::optional<HomeFeed> YouTubeCatalogService::get_home_feed(const std::string& kiosk_id) const {
    const AppSettings settings = SettingsStore::instance().settings();
    for (const auto& preset : kFeedPresets) {
        if (kiosk_id == preset.id) {
            if (kiosk_id == "recommended" && this->auth_store_->has_session()) {
                const auto personalized_feed = this->fetch_authenticated_browse_feed(
                    "FEwhat_to_watch",
                    preset.title,
                    "https://www.youtube.com/",
                    24,
                    preset.allow_short_videos && !settings.hide_short_videos);
                if (personalized_feed.has_value()) {
                    return personalized_feed;
                }

                logf(
                    "youtube: personalized home fallback error=%s",
                    this->error_message_.c_str());
                this->error_message_.clear();
            }

            return fetch_home_feed(
                preset.id,
                preset.title,
                preset.query,
                preset.allow_short_videos && !settings.hide_short_videos);
        }
    }

    error_message_ = "지원하지 않는 홈 카테고리";
    return std::nullopt;
}

std::optional<HomeFeed> YouTubeCatalogService::fetch_authenticated_browse_feed(
    const std::string& browse_id,
    const std::string& title,
    const std::string& referer,
    size_t limit,
    bool allow_short_videos) const {
    const auto cache_it = this->authenticated_browse_cache_.find(browse_id);
    if (cache_it != this->authenticated_browse_cache_.end()) {
        this->error_message_.clear();
        return cache_it->second;
    }

    std::string auth_error;
    if (!this->auth_store_->load(&auth_error)) {
        this->error_message_ = auth_error.empty() ? "로그인 세션 로드 실패" : auth_error;
        return std::nullopt;
    }
    if (!this->auth_store_->has_session()) {
        this->error_message_ = "구독 피드를 보려면 로그인 세션이 필요합니다";
        return std::nullopt;
    }

    json payload = {
        {"browseId", browse_id},
        {"context",
         {{"client",
           {{"clientName", "WEB"},
            {"clientVersion", kWebClientVersion},
            {"hl", "ko"},
            {"gl", "KR"},
            {"utcOffsetMinutes", 540},
            {"platform", "DESKTOP"},
            {"screenWidthPoints", 1280},
            {"screenHeightPoints", 720},
            {"screenPixelDensity", 1}}}}},
    };

    auto headers =
        this->auth_store_->build_youtube_headers("https://www.youtube.com", referer, &auth_error);
    if (!auth_error.empty()) {
        this->error_message_ = auth_error;
        return std::nullopt;
    }

    headers.push_back({"Content-Type", "application/json"});
    headers.push_back({"User-Agent", kWebUserAgent});
    headers.push_back({"X-Youtube-Client-Name", "1"});
    headers.push_back({"X-Youtube-Client-Version", kWebClientVersion});

    const auto response = this->client_->post(kBrowseApiUrl, payload.dump(), headers);
    if (!response.has_value() || response->empty()) {
        this->error_message_ = "인증된 구독 피드 요청 실패";
        return std::nullopt;
    }

    const json root = json::parse(*response, nullptr, false);
    if (root.is_discarded()) {
        this->error_message_ = "구독 피드 응답 파싱 실패";
        return std::nullopt;
    }

    HomeFeed feed;
    feed.kiosk = {browse_id, title};
    std::unordered_set<std::string> seen_ids;
    collect_stream_items(root, allow_short_videos, limit, seen_ids, feed.items);
    if (feed.items.empty()) {
        this->error_message_ = "구독 피드에서 영상 항목을 찾지 못했습니다";
        return std::nullopt;
    }

    this->cache_stream_details(feed.items);
    this->authenticated_browse_cache_[browse_id] = feed;
    this->error_message_.clear();
    logf("youtube: authenticated browse id=%s items=%zu", browse_id.c_str(), feed.items.size());
    return feed;
}

std::optional<StreamDetail> YouTubeCatalogService::fetch_stream_detail_from_player(
    const std::string& url) const {
    const auto video_id = extract_video_id_from_url(url);
    if (!video_id.has_value()) {
        this->error_message_ = "영상 ID를 추출할 수 없습니다";
        return std::nullopt;
    }

    json payload = {
        {"videoId", *video_id},
        {"contentCheckOk", true},
        {"racyCheckOk", true},
        {"context",
         {{"client",
           {{"clientName", "ANDROID"},
            {"clientVersion", kAndroidClientVersion},
            {"androidSdkVersion", 30},
            {"hl", "ko"},
            {"gl", "KR"}}}}},
    };

    const auto response = this->client_->post(
        kPlayerApiUrl,
        payload.dump(),
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kAndroidUserAgent},
            {"X-Youtube-Client-Name", "3"},
            {"X-Youtube-Client-Version", kAndroidClientVersion},
            {"Origin", "https://www.youtube.com"},
        });

    if (!response.has_value() || response->empty()) {
        this->error_message_ = "영상 상세 요청 실패";
        return std::nullopt;
    }

    const json root = json::parse(*response, nullptr, false);
    if (root.is_discarded()) {
        this->error_message_ = "영상 상세 응답 파싱 실패";
        return std::nullopt;
    }

    const json playability = root.value("playabilityStatus", json::object());
    const std::string playability_status = get_string(playability, "status");
    if (!playability_status.empty() && playability_status != "OK") {
        this->error_message_ = "영상 상세 불러오기 실패: " + playability_status;
        return std::nullopt;
    }

    StreamDetail detail;
    const auto cached_it = this->detail_cache_.find(url);
    if (cached_it != this->detail_cache_.end()) {
        detail = cached_it->second;
    }

    const json video_details = root.value("videoDetails", json::object());
    const json microformat = root.value("microformat", json::object())
                                 .value("playerMicroformatRenderer", json::object());

    detail.item.id = video_id.value_or(detail.item.id);
    detail.item.url = "https://www.youtube.com/watch?v=" + detail.item.id;
    if (detail.item.title.empty()) {
        detail.item.title = get_string(video_details, "title");
    }
    if (detail.item.channel_name.empty()) {
        detail.item.channel_name = get_string(video_details, "author");
    }
    if (detail.item.channel_id.empty()) {
        detail.item.channel_id = get_string(video_details, "channelId");
    }
    if (detail.item.channel_url.empty() && !detail.item.channel_id.empty()) {
        detail.item.channel_url = "https://www.youtube.com/channel/" + detail.item.channel_id;
    }
    if (detail.item.thumbnail_url.empty()) {
        detail.item.thumbnail_url = get_thumbnail_url_from_node(video_details.value("thumbnail", json::object()));
    }
    if (detail.item.duration_text.empty()) {
        detail.item.duration_text = format_duration_text_from_seconds(get_string(video_details, "lengthSeconds"));
    }
    if (detail.item.view_count_text.empty()) {
        detail.item.view_count_text = format_view_count_text(get_string(video_details, "viewCount"));
    }
    if (detail.item.published_text.empty()) {
        detail.item.published_text = get_string(microformat, "publishDate");
        if (detail.item.published_text.empty()) {
            detail.item.published_text = get_string(microformat, "uploadDate");
        }
    }
    detail.item.is_live = video_details.value("isLiveContent", detail.item.is_live);

    if (detail.description.empty()) {
        detail.description = get_string(video_details, "shortDescription");
    }

    if (detail.related_items.empty() && !detail.item.title.empty()) {
        const AppSettings settings = SettingsStore::instance().settings();
        const std::string saved_error = this->error_message_;
        detail.related_items = fetch_watch_related_items(
            this->client_,
            detail.item.url,
            detail.item.id,
            !settings.hide_short_videos);

        if (detail.related_items.empty()) {
            SearchResults related_results = this->fetch_search_results(
                detail.item.title + " " + detail.item.channel_name,
                16,
                !settings.hide_short_videos);
            this->error_message_.clear();

            for (const auto& candidate : related_results.items) {
                if (candidate.id == detail.item.id) {
                    continue;
                }
                detail.related_items.push_back(candidate);
                if (detail.related_items.size() >= 12) {
                    break;
                }
            }
        }

        if (detail.related_items.empty()) {
            this->error_message_ = saved_error;
        }
    }

    this->cache_stream_detail(detail);
    this->error_message_.clear();
    logf("youtube: detail url=%s related=%zu", detail.item.url.c_str(), detail.related_items.size());
    return detail;
}

std::optional<HomeFeed> YouTubeCatalogService::fetch_channel_feed_from_rss(
    const StreamItem& item,
    size_t limit) const {
    std::string channel_id = item.channel_id;
    if (channel_id.empty()) {
        channel_id = extract_channel_id_from_url(item.channel_url);
    }
    if (channel_id.empty() && !item.url.empty()) {
        const auto detail = this->get_stream_detail(item.url);
        if (detail.has_value()) {
            channel_id = detail->item.channel_id;
        }
    }
    if (channel_id.empty()) {
        this->error_message_ = "채널 ID를 찾지 못했습니다";
        return std::nullopt;
    }

    const auto cache_it = this->channel_feed_cache_.find(channel_id);
    if (cache_it != this->channel_feed_cache_.end()) {
        this->error_message_.clear();
        return cache_it->second;
    }

    const auto response = this->client_->get(kChannelFeedUrlPrefix + channel_id);
    if (!response.has_value() || response->empty()) {
        this->error_message_ = "채널 피드 요청 실패";
        return std::nullopt;
    }

    HomeFeed feed;
    feed.kiosk = {channel_id, item.channel_name.empty() ? "채널 업로드" : item.channel_name + " 업로드"};

    size_t search_from = 0;
    while (feed.items.size() < limit) {
        const size_t entry_start = response->find("<entry>", search_from);
        if (entry_start == std::string::npos) {
            break;
        }

        const size_t entry_end = response->find("</entry>", entry_start);
        if (entry_end == std::string::npos) {
            break;
        }

        const std::string entry = response->substr(entry_start, entry_end - entry_start);
        search_from = entry_end + 8;

        StreamItem entry_item;
        entry_item.id = extract_xml_tag(entry, "yt:videoId");
        if (entry_item.id.empty()) {
            continue;
        }

        entry_item.url = "https://www.youtube.com/watch?v=" + entry_item.id;
        entry_item.title = extract_xml_tag(entry, "title");
        entry_item.channel_name = extract_xml_tag(entry, "name");
        entry_item.channel_url = extract_xml_tag(entry, "uri");
        entry_item.channel_id = extract_xml_tag(entry, "yt:channelId");
        entry_item.thumbnail_url = extract_xml_attribute(entry, "<media:thumbnail", "url");
        entry_item.view_count_text =
            format_view_count_text(extract_xml_attribute(entry, "<media:statistics", "views"));
        entry_item.published_text = extract_xml_tag(entry, "published");
        entry_item.is_live = false;

        if (entry_item.title.empty()) {
            continue;
        }

        feed.items.push_back(entry_item);
    }

    if (feed.items.empty()) {
        this->error_message_ = "채널 피드에서 영상을 찾지 못했습니다";
        return std::nullopt;
    }

    this->cache_stream_details(feed.items);
    this->channel_feed_cache_[channel_id] = feed;
    this->error_message_.clear();
    logf("youtube: channel feed id=%s items=%zu", channel_id.c_str(), feed.items.size());
    return feed;
}

std::optional<HomeFeed> YouTubeCatalogService::fetch_playlist_feed_from_browse(
    const std::string& playlist_id,
    size_t limit) const {
    if (playlist_id.empty()) {
        this->error_message_ = "재생목록 ID를 찾지 못했습니다";
        return std::nullopt;
    }

    const auto cache_it = this->playlist_feed_cache_.find(playlist_id);
    if (cache_it != this->playlist_feed_cache_.end()) {
        this->error_message_.clear();
        return cache_it->second;
    }

    json payload = {
        {"browseId", "VL" + playlist_id},
        {"context",
         {{"client",
           {{"clientName", "WEB"},
            {"clientVersion", kWebClientVersion},
            {"hl", "ko"},
            {"gl", "KR"},
            {"platform", "DESKTOP"}}}}},
    };

    const auto response = this->client_->post(
        kBrowseApiUrl,
        payload.dump(),
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kWebUserAgent},
            {"X-Youtube-Client-Name", "1"},
            {"X-Youtube-Client-Version", kWebClientVersion},
            {"Origin", "https://www.youtube.com"},
        });
    if (!response.has_value() || response->empty()) {
        this->error_message_ = "재생목록 요청 실패";
        return std::nullopt;
    }

    const json root = json::parse(*response, nullptr, false);
    if (root.is_discarded()) {
        this->error_message_ = "재생목록 응답 파싱 실패";
        return std::nullopt;
    }

    if (root.contains("alerts") && root.at("alerts").is_array() && !root.at("alerts").empty()) {
        const std::string alert_text = get_text(
            root.at("alerts").front().value("alertRenderer", json::object()).value("text", json::object()));
        if (!alert_text.empty()) {
            this->error_message_ = alert_text;
            return std::nullopt;
        }
    }

    HomeFeed feed;
    const std::string playlist_title = extract_playlist_title(root);
    feed.kiosk = {
        playlist_id,
        playlist_title.empty() ? "재생목록" : playlist_title,
    };

    const AppSettings settings = SettingsStore::instance().settings();
    std::unordered_set<std::string> seen_ids;
    collect_playlist_items(root, !settings.hide_short_videos, limit, seen_ids, feed.items);
    if (feed.items.empty()) {
        this->error_message_ = "재생목록 항목을 찾지 못했습니다";
        return std::nullopt;
    }

    this->cache_stream_details(feed.items);
    this->playlist_feed_cache_[playlist_id] = feed;
    this->error_message_.clear();
    logf("youtube: playlist feed id=%s items=%zu", playlist_id.c_str(), feed.items.size());
    return feed;
}

std::optional<CommentPage> YouTubeCatalogService::fetch_comments_from_watch(
    const StreamItem& item,
    size_t limit) const {
    const std::string cache_key = !item.url.empty() ? item.url : item.id;
    const auto cache_it = this->comments_cache_.find(cache_key);
    if (cache_it != this->comments_cache_.end()) {
        this->error_message_.clear();
        return cache_it->second;
    }

    if (item.url.empty()) {
        this->error_message_ = "댓글을 불러올 URL이 없습니다";
        return std::nullopt;
    }

    const auto initial_data = fetch_watch_page_initial_data(this->client_, item.url);
    if (!initial_data.has_value()) {
        this->error_message_ = "댓글 페이지 초기 데이터 요청 실패";
        return std::nullopt;
    }

    const std::string continuation_token = extract_comments_continuation_token(*initial_data);
    if (continuation_token.empty()) {
        this->error_message_ = "댓글 continuation token을 찾지 못했습니다";
        return std::nullopt;
    }

    const auto page = fetch_comments_page(this->client_, continuation_token, limit);
    if (!page.has_value()) {
        this->error_message_ = "댓글 목록을 파싱하지 못했습니다";
        return std::nullopt;
    }

    this->comments_cache_[cache_key] = *page;
    this->error_message_.clear();
    logf("youtube: comments url=%s items=%zu", item.url.c_str(), page->items.size());
    return page;
}

SearchResults YouTubeCatalogService::fetch_search_results(
    const std::string& query,
    size_t limit,
    bool allow_short_videos) const {
    error_message_.clear();

    SearchResults results;
    results.query = query;
    if (query.empty()) {
        return results;
    }

    json payload = {
        {"query", query},
        {"context",
         {{"client",
           {{"clientName", "ANDROID"},
            {"clientVersion", kAndroidClientVersion},
            {"androidSdkVersion", 30},
            {"hl", "en"},
            {"gl", "US"}}}}},
    };

    const auto response = client_->post(
        kSearchApiUrl,
        payload.dump(),
        {
            {"Content-Type", "application/json"},
            {"User-Agent", kAndroidUserAgent},
            {"X-Youtube-Client-Name", "3"},
            {"X-Youtube-Client-Version", kAndroidClientVersion},
            {"Origin", "https://www.youtube.com"},
        });

    if (!response.has_value() || response->empty()) {
        error_message_ = "유튜브 검색 요청 실패";
        return results;
    }

    const json root = json::parse(*response, nullptr, false);
    if (root.is_discarded()) {
        error_message_ = "유튜브 검색 응답 파싱 실패";
        return results;
    }

    std::unordered_set<std::string> seen_ids;
    collect_stream_items(root, allow_short_videos, limit, seen_ids, results.items);
    if (results.items.empty()) {
        error_message_ = "유튜브 검색 결과가 없습니다";
        return results;
    }

    this->cache_stream_details(results.items);

    logf("youtube: search query=%s items=%zu", query.c_str(), results.items.size());
    return results;
}

std::optional<HomeFeed> YouTubeCatalogService::get_subscriptions_feed() const {
    const AppSettings settings = SettingsStore::instance().settings();
    return this->fetch_authenticated_browse_feed(
        "FEsubscriptions",
        "구독 최신 영상",
        "https://www.youtube.com/feed/subscriptions",
        24,
        !settings.hide_short_videos);
}

std::optional<HomeFeed> YouTubeCatalogService::get_related_feed(const StreamItem& item) const {
    const std::string cache_key = !item.url.empty() ? item.url : item.id;
    const auto cache_it = this->related_feed_cache_.find(cache_key);
    if (cache_it != this->related_feed_cache_.end()) {
        this->error_message_.clear();
        return cache_it->second;
    }

    const auto detail = this->get_stream_detail(item.url);
    if (!detail.has_value() || detail->related_items.empty()) {
        this->error_message_ = "관련 영상을 찾지 못했습니다";
        return std::nullopt;
    }

    HomeFeed feed;
    feed.kiosk = {"related", "연관 추천"};
    feed.items = detail->related_items;
    this->cache_stream_details(feed.items);
    this->related_feed_cache_[cache_key] = feed;
    this->error_message_.clear();
    return feed;
}

std::optional<HomeFeed> YouTubeCatalogService::get_channel_feed(const StreamItem& item) const {
    return this->fetch_channel_feed_from_rss(item, 24);
}

std::optional<HomeFeed> YouTubeCatalogService::get_playlist_feed(const StreamItem& item) const {
    const auto playlist_id = extract_playlist_id_from_url(item.url);
    if (!playlist_id.has_value()) {
        this->error_message_ = "재생목록 ID가 없는 영상입니다";
        return std::nullopt;
    }

    return this->fetch_playlist_feed_from_browse(*playlist_id, 48);
}

std::optional<CommentPage> YouTubeCatalogService::get_comments(const StreamItem& item) const {
    return this->fetch_comments_from_watch(item, 24);
}

SearchResults YouTubeCatalogService::search(const std::string& query) const {
    const AppSettings settings = SettingsStore::instance().settings();
    return fetch_search_results(query, 20, !settings.hide_short_videos);
}

std::optional<StreamDetail> YouTubeCatalogService::get_stream_detail(const std::string& url) const {
    const auto it = detail_cache_.find(url);
    if (it != detail_cache_.end()
        && (!it->second.description.empty() || !it->second.related_items.empty()
            || !it->second.item.channel_id.empty())) {
        error_message_.clear();
        return it->second;
    }

    return this->fetch_stream_detail_from_player(url);
}

void YouTubeCatalogService::cache_stream_details(const std::vector<StreamItem>& items) const {
    for (const auto& item : items) {
        StreamDetail detail;
        const auto it = this->detail_cache_.find(item.url);
        if (it != this->detail_cache_.end()) {
            detail = it->second;
        }
        detail.item = item;
        this->detail_cache_[item.url] = detail;
    }
}

void YouTubeCatalogService::cache_stream_detail(const StreamDetail& detail) const {
    this->detail_cache_[detail.item.url] = detail;
}

void YouTubeCatalogService::invalidate_auth_caches() {
    this->authenticated_browse_cache_.clear();
}

}  // namespace newpipe
