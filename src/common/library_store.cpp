#include "newpipe/library_store.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"

namespace newpipe {
namespace {

using nlohmann::json;

constexpr size_t kHistoryLimit = 120;
constexpr size_t kFavoriteLimit = 200;

json serialize_stream_item(const StreamItem& item) {
    return {
        {"id", item.id},
        {"url", item.url},
        {"title", item.title},
        {"channel_name", item.channel_name},
        {"channel_url", item.channel_url},
        {"channel_id", item.channel_id},
        {"thumbnail_url", item.thumbnail_url},
        {"duration_text", item.duration_text},
        {"view_count_text", item.view_count_text},
        {"published_text", item.published_text},
        {"is_live", item.is_live},
    };
}

std::string get_string(const json& node, const char* key) {
    if (!node.is_object() || !node.contains(key) || !node.at(key).is_string()) {
        return {};
    }
    return node.at(key).get<std::string>();
}

StreamItem deserialize_stream_item(const json& node) {
    StreamItem item;
    item.id = get_string(node, "id");
    item.url = get_string(node, "url");
    item.title = get_string(node, "title");
    item.channel_name = get_string(node, "channel_name");
    item.channel_url = get_string(node, "channel_url");
    item.channel_id = get_string(node, "channel_id");
    item.thumbnail_url = get_string(node, "thumbnail_url");
    item.duration_text = get_string(node, "duration_text");
    item.view_count_text = get_string(node, "view_count_text");
    item.published_text = get_string(node, "published_text");
    item.is_live = node.value("is_live", false);
    return item;
}

bool read_text_file(const std::string& path, std::string& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::string& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        return false;
    }

    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

void upsert_front(std::vector<StreamItem>& items, const StreamItem& item, size_t limit) {
    items.erase(
        std::remove_if(items.begin(), items.end(), [&item](const StreamItem& existing) {
            return !item.url.empty() && existing.url == item.url;
        }),
        items.end());
    items.insert(items.begin(), item);
    if (items.size() > limit) {
        items.resize(limit);
    }
}

}  // namespace

std::string default_library_store_path() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe_library.json";
#else
    return "switch_newpipe_library.json";
#endif
}

LibraryStore& LibraryStore::instance() {
    static LibraryStore store;
    return store;
}

bool LibraryStore::load(std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->loaded_) {
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    std::string raw;
    if (!read_text_file(default_library_store_path(), raw)) {
        this->loaded_ = true;
        this->history_items_.clear();
        this->favorite_items_.clear();
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    const json root = json::parse(raw, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error_message) {
            *error_message = "라이브러리 저장 파일 파싱 실패";
        }
        return false;
    }

    this->history_items_.clear();
    this->favorite_items_.clear();

    for (const auto& node : root.value("history", json::array())) {
        this->history_items_.push_back(deserialize_stream_item(node));
    }
    for (const auto& node : root.value("favorites", json::array())) {
        this->favorite_items_.push_back(deserialize_stream_item(node));
    }

    this->loaded_ = true;
    if (error_message) {
        error_message->clear();
    }
    logf(
        "library: loaded history=%zu favorites=%zu",
        this->history_items_.size(),
        this->favorite_items_.size());
    return true;
}

bool LibraryStore::reload(std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->loaded_ = false;
    this->history_items_.clear();
    this->favorite_items_.clear();
    if (error_message) {
        error_message->clear();
    }
    return true;
}

std::vector<StreamItem> LibraryStore::history_items() {
    std::string error;
    this->ensure_loaded(&error);
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->history_items_;
}

std::vector<StreamItem> LibraryStore::favorite_items() {
    std::string error;
    this->ensure_loaded(&error);
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->favorite_items_;
}

bool LibraryStore::is_favorite(const std::string& url) {
    std::string error;
    this->ensure_loaded(&error);
    std::lock_guard<std::mutex> lock(this->mutex_);
    return std::any_of(this->favorite_items_.begin(), this->favorite_items_.end(), [&url](const StreamItem& item) {
        return item.url == url;
    });
}

bool LibraryStore::add_history(const StreamItem& item, std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        upsert_front(this->history_items_, item, kHistoryLimit);
    }

    logf("library: add history url=%s", item.url.c_str());
    return this->persist(error_message);
}

bool LibraryStore::toggle_favorite(
    const StreamItem& item,
    bool* is_now_favorite,
    std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    bool favorite = false;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        auto it = std::find_if(
            this->favorite_items_.begin(),
            this->favorite_items_.end(),
            [&item](const StreamItem& existing) { return existing.url == item.url; });
        if (it == this->favorite_items_.end()) {
            upsert_front(this->favorite_items_, item, kFavoriteLimit);
            favorite = true;
        } else {
            this->favorite_items_.erase(it);
            favorite = false;
        }
    }

    if (is_now_favorite) {
        *is_now_favorite = favorite;
    }
    logf("library: toggle favorite url=%s now=%d", item.url.c_str(), favorite ? 1 : 0);
    return this->persist(error_message);
}

bool LibraryStore::clear_history(std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->history_items_.clear();
    }
    return this->persist(error_message);
}

bool LibraryStore::clear_favorites(std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->favorite_items_.clear();
    }
    return this->persist(error_message);
}

bool LibraryStore::ensure_loaded(std::string* error_message) {
    if (this->loaded_) {
        if (error_message) {
            error_message->clear();
        }
        return true;
    }
    return this->load(error_message);
}

bool LibraryStore::persist(std::string* error_message) {
    json root;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        root["history"] = json::array();
        for (const auto& item : this->history_items_) {
            root["history"].push_back(serialize_stream_item(item));
        }

        root["favorites"] = json::array();
        for (const auto& item : this->favorite_items_) {
            root["favorites"].push_back(serialize_stream_item(item));
        }
    }

    if (!write_text_file(default_library_store_path(), root.dump(2))) {
        if (error_message) {
            *error_message = "라이브러리 저장 실패";
        }
        return false;
    }

    if (error_message) {
        error_message->clear();
    }
    return true;
}

}  // namespace newpipe
