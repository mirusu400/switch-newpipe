#pragma once

#include <functional>
#include <optional>
#include <string>

#include "newpipe/http_client.hpp"
#include "newpipe/throttling_decrypter.hpp"

namespace newpipe {

enum class PlaybackQualityMode {
    STANDARD_720 = 0,
    COMPATIBILITY = 1,
    DATA_SAVER = 2,
};

struct ResolvedPlayback {
    std::string stream_url;
    std::string referer;
    std::string http_header_fields;
    std::string quality_label;
    int hls_bitrate = 0;
    std::string playlist_body;
    std::string external_audio_url;
    std::string external_audio_playlist_body;
    std::string fallback_stream_url;
    std::string fallback_referer;
    std::string fallback_http_header_fields;
    std::string fallback_quality_label;
    std::string fallback_external_audio_url;
    bool is_live = false;
};

using ResolverStatusCallback = std::function<void(const std::string&, const std::string&)>;

class YouTubeResolver {
public:
    explicit YouTubeResolver(HttpClient* client = nullptr);

    std::optional<ResolvedPlayback> resolve(
        const std::string& url,
        std::string& error_message,
        ResolverStatusCallback on_status = {});

    static bool is_youtube_url(const std::string& url);
    static std::optional<std::string> extract_video_id(const std::string& url);

    ThrottlingDecrypter& throttle_decrypter() { return throttle_decrypter_; }
    void apply_throttle_transform(ResolvedPlayback& playback);

private:
    std::optional<ResolvedPlayback> resolve_internal(
        const std::string& url,
        std::string& error_message,
        ResolverStatusCallback on_status);

    HttpsHttpClient owned_client_;
    HttpClient* client_ = nullptr;
    ThrottlingDecrypter throttle_decrypter_;
};

}  // namespace newpipe
