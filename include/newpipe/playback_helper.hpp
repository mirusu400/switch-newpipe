#pragma once

#include <optional>
#include <string>

#include "newpipe/models.hpp"
#include "newpipe/switch_player.hpp"

namespace newpipe {

inline std::optional<PlaybackRequest> build_playback_request(
    const StreamItem& item,
    const std::optional<StreamDetail>& detail) {
    std::string url = item.url;
    if (url.empty() && detail.has_value() && detail->playback_url.has_value()) {
        url = *detail->playback_url;
    }

    if (url.empty()) {
        return std::nullopt;
    }

    PlaybackRequest request;
    request.title = item.title;
    request.url = url;
    request.referer = item.url;
    if (item.url.empty()) {
        request.http_header_fields =
            "Accept: */*,Accept-Encoding: identity,Connection: close,Cache-Control: no-cache";
    }
    return request;
}

}  // namespace newpipe
