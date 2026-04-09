#pragma once

#include <optional>
#include <string>

#include "newpipe/http_client.hpp"

namespace newpipe {

class ThrottlingDecrypter {
public:
    explicit ThrottlingDecrypter(HttpClient* client = nullptr);

    std::string apply(const std::string& url);

    void invalidate();

private:
    bool ensure_function_loaded(const std::string& video_id);
    std::optional<std::string> transform_n(const std::string& n_value);

    std::optional<std::string> fetch_player_js_url(const std::string& video_id);
    std::optional<std::string> fetch_player_js(const std::string& player_js_url);
    std::optional<std::string> extract_throttle_function(const std::string& player_js);

    HttpsHttpClient owned_client_;
    HttpClient* client_ = nullptr;
    std::string cached_player_js_url_;
    std::string cached_function_body_;
};

}  // namespace newpipe
