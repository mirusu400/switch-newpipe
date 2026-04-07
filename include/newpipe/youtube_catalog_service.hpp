#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "newpipe/auth_store.hpp"
#include "newpipe/catalog_service.hpp"
#include "newpipe/http_client.hpp"
#include "newpipe/settings_store.hpp"

namespace newpipe {

class YouTubeCatalogService final : public CatalogService {
public:
    explicit YouTubeCatalogService(HttpClient* client = nullptr, AuthStore* auth_store = nullptr);

    bool is_loaded() const { return true; }
    const std::string& error_message() const { return error_message_; }

    bool load_auth_session(std::string* error_message = nullptr);
    bool reload_auth_session(std::string* error_message = nullptr);
    bool has_auth_session() const;
    AuthSession auth_session() const;
    bool import_auth_session_from_file(
        const std::string& file_path = {},
        std::string* error_message = nullptr);
    bool update_auth_session_from_cookie(
        const std::string& cookie_header,
        const std::string& source_label,
        std::string* error_message = nullptr);
    bool clear_auth_session(std::string* error_message = nullptr);

    std::vector<Kiosk> list_kiosks() const override;
    std::optional<HomeFeed> get_home_feed(const std::string& kiosk_id) const override;
    std::optional<HomeFeed> get_subscriptions_feed() const;
    std::optional<HomeFeed> get_related_feed(const StreamItem& item) const;
    std::optional<HomeFeed> get_channel_feed(const StreamItem& item) const;
    std::optional<HomeFeed> get_playlist_feed(const StreamItem& item) const;
    std::optional<CommentPage> get_comments(const StreamItem& item) const;
    SearchResults search(const std::string& query) const override;
    std::optional<StreamDetail> get_stream_detail(const std::string& url) const override;

private:
    std::optional<HomeFeed> fetch_home_feed(
        const std::string& kiosk_id,
        const std::string& title,
        const std::string& query,
        bool allow_short_videos) const;
    std::optional<HomeFeed> fetch_authenticated_browse_feed(
        const std::string& browse_id,
        const std::string& title,
        const std::string& referer,
        size_t limit,
        bool allow_short_videos) const;
    std::optional<StreamDetail> fetch_stream_detail_from_player(const std::string& url) const;
    std::optional<HomeFeed> fetch_channel_feed_from_rss(
        const StreamItem& item,
        size_t limit) const;
    std::optional<HomeFeed> fetch_playlist_feed_from_browse(
        const std::string& playlist_id,
        size_t limit) const;
    std::optional<CommentPage> fetch_comments_from_watch(
        const StreamItem& item,
        size_t limit) const;
    SearchResults fetch_search_results(
        const std::string& query,
        size_t limit,
        bool allow_short_videos) const;
    void cache_stream_details(const std::vector<StreamItem>& items) const;
    void cache_stream_detail(const StreamDetail& detail) const;
    void invalidate_auth_caches();

    HttpsHttpClient owned_client_;
    HttpClient* client_ = nullptr;
    AuthStore* auth_store_ = nullptr;
    mutable std::unordered_map<std::string, HomeFeed> home_feed_cache_;
    mutable std::unordered_map<std::string, HomeFeed> related_feed_cache_;
    mutable std::unordered_map<std::string, HomeFeed> channel_feed_cache_;
    mutable std::unordered_map<std::string, HomeFeed> playlist_feed_cache_;
    mutable std::unordered_map<std::string, HomeFeed> authenticated_browse_cache_;
    mutable std::unordered_map<std::string, StreamDetail> detail_cache_;
    mutable std::unordered_map<std::string, CommentPage> comments_cache_;
    mutable std::string error_message_;
};

}  // namespace newpipe
