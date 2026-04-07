#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "newpipe/catalog_service.hpp"

namespace newpipe {

class FixtureCatalogService final : public CatalogService {
public:
    explicit FixtureCatalogService(std::string fixture_path = "");

    bool is_loaded() const { return loaded_; }
    const std::string& error_message() const { return error_message_; }

    std::vector<Kiosk> list_kiosks() const override;
    std::optional<HomeFeed> get_home_feed(const std::string& kiosk_id) const override;
    SearchResults search(const std::string& query) const override;
    std::optional<StreamDetail> get_stream_detail(const std::string& url) const override;

private:
    struct CatalogData {
        std::vector<Kiosk> kiosks;
        std::unordered_map<std::string, HomeFeed> feeds_by_id;
        std::unordered_map<std::string, StreamDetail> details_by_url;
        std::vector<StreamItem> searchable_items;
    };

    static std::optional<CatalogData> load_catalog(const std::string& fixture_path,
                                                   std::string& error_message);

    CatalogData data_;
    bool loaded_ = false;
    std::string error_message_;
};

}  // namespace newpipe
