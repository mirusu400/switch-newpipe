#include "newpipe/fixture_service.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_set>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"

namespace newpipe {
namespace {

using nlohmann::json;

std::string read_text_file(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string get_string(const json& node, const char* key) {
    if (!node.is_object() || !node.contains(key) || node.at(key).is_null()) {
        return {};
    }

    return node.at(key).get<std::string>();
}

bool get_bool(const json& node, const char* key, bool fallback = false) {
    if (!node.is_object() || !node.contains(key) || node.at(key).is_null()) {
        return fallback;
    }

    return node.at(key).get<bool>();
}

StreamItem parse_stream_item(const json& node) {
    StreamItem item;
    item.id = get_string(node, "id");
    item.url = get_string(node, "url");
    item.title = get_string(node, "title");
    item.channel_name = get_string(node, "channel_name");
    item.channel_url = get_string(node, "channel_url");
    item.thumbnail_url = get_string(node, "thumbnail_url");
    item.duration_text = get_string(node, "duration_text");
    item.view_count_text = get_string(node, "view_count_text");
    item.published_text = get_string(node, "published_text");
    item.is_live = get_bool(node, "is_live", false);
    return item;
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

}  // namespace

FixtureCatalogService::FixtureCatalogService(std::string fixture_path) {
    if (fixture_path.empty()) {
#ifdef __SWITCH__
        fixture_path = "romfs:/fixtures/catalog.json";
#else
        fixture_path = "resources/fixtures/catalog.json";
#endif
    }

    auto data = load_catalog(fixture_path, error_message_);
    if (!data.has_value()) {
        logf("fixture: load failed path=%s error=%s", fixture_path.c_str(), error_message_.c_str());
        loaded_ = false;
        return;
    }

    data_ = std::move(*data);
    loaded_ = true;
    logf("fixture: loaded path=%s kiosks=%zu items=%zu",
         fixture_path.c_str(),
         data_.kiosks.size(),
         data_.searchable_items.size());
}

std::vector<Kiosk> FixtureCatalogService::list_kiosks() const {
    return data_.kiosks;
}

std::optional<HomeFeed> FixtureCatalogService::get_home_feed(const std::string& kiosk_id) const {
    const auto it = data_.feeds_by_id.find(kiosk_id);
    if (it == data_.feeds_by_id.end()) {
        return std::nullopt;
    }

    return it->second;
}

SearchResults FixtureCatalogService::search(const std::string& query) const {
    constexpr size_t kFallbackCount = 8;
    SearchResults results;
    results.query = query;

    if (query.empty()) {
        return results;
    }

    for (const auto& item : data_.searchable_items) {
        std::string description;
        const auto detail_it = data_.details_by_url.find(item.url);
        if (detail_it != data_.details_by_url.end()) {
            description = detail_it->second.description;
        }

        if (contains_case_insensitive(item.title, query)
            || contains_case_insensitive(item.channel_name, query)
            || contains_case_insensitive(description, query)
            || contains_case_insensitive(item.id, query)) {
            results.items.push_back(item);
        }
    }

    if (results.items.empty()) {
        results.used_fallback = true;
        for (size_t i = 0; i < data_.searchable_items.size() && i < kFallbackCount; i++) {
            results.items.push_back(data_.searchable_items[i]);
        }
    }

    return results;
}

std::optional<StreamDetail> FixtureCatalogService::get_stream_detail(const std::string& url) const {
    const auto it = data_.details_by_url.find(url);
    if (it == data_.details_by_url.end()) {
        return std::nullopt;
    }

    return it->second;
}

std::optional<FixtureCatalogService::CatalogData> FixtureCatalogService::load_catalog(
    const std::string& fixture_path,
    std::string& error_message) {
    const std::string raw = read_text_file(fixture_path);
    if (raw.empty()) {
        error_message = "fixture file not found: " + fixture_path;
        return std::nullopt;
    }

    const json root = json::parse(raw, nullptr, false);
    if (root.is_discarded()) {
        error_message = "fixture parse failed: " + fixture_path;
        return std::nullopt;
    }

    if (!root.contains("kiosks") || !root.at("kiosks").is_array()) {
        error_message = "fixture missing kiosks array";
        return std::nullopt;
    }

    CatalogData data;
    std::unordered_set<std::string> seen_urls;

    for (const auto& kiosk_node : root.at("kiosks")) {
        Kiosk kiosk;
        kiosk.id = get_string(kiosk_node, "id");
        kiosk.title = get_string(kiosk_node, "title");

        if (kiosk.id.empty()) {
            continue;
        }

        data.kiosks.push_back(kiosk);

        HomeFeed feed;
        feed.kiosk = kiosk;

        if (kiosk_node.contains("items") && kiosk_node.at("items").is_array()) {
            for (const auto& item_node : kiosk_node.at("items")) {
                StreamItem item = parse_stream_item(item_node);
                if (item.url.empty()) {
                    continue;
                }

                feed.items.push_back(item);

                StreamDetail detail;
                detail.item = item;
                detail.description = get_string(item_node, "description");
                const std::string playback_url = get_string(item_node, "playback_url");
                if (!playback_url.empty()) {
                    detail.playback_url = playback_url;
                }
                data.details_by_url[item.url] = detail;

                if (seen_urls.insert(item.url).second) {
                    data.searchable_items.push_back(item);
                }
            }
        }

        data.feeds_by_id[kiosk.id] = std::move(feed);
    }

    if (data.kiosks.empty()) {
        error_message = "fixture contains no kiosks";
        return std::nullopt;
    }

    return data;
}

}  // namespace newpipe
