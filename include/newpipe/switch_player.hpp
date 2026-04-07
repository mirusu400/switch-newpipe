#pragma once

#include <string>

namespace newpipe {

struct PlaybackRequest {
    std::string title;
    std::string url;
    std::string referer;
    std::string http_header_fields;
};

bool run_switch_player(const PlaybackRequest& request, std::string& error);

}  // namespace newpipe
