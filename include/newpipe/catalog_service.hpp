#pragma once

#include <optional>
#include <string>
#include <vector>

#include "newpipe/models.hpp"

namespace newpipe {

class CatalogService {
public:
    virtual ~CatalogService() = default;

    virtual std::vector<Kiosk> list_kiosks() const = 0;
    virtual std::optional<HomeFeed> get_home_feed(const std::string& kiosk_id) const = 0;
    virtual SearchResults search(const std::string& query) const = 0;
    virtual std::optional<StreamDetail> get_stream_detail(const std::string& url) const = 0;
};

}  // namespace newpipe
