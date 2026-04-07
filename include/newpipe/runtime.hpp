#pragma once

#include <optional>
#include <string>

#include "newpipe/switch_player.hpp"

namespace newpipe {

void queue_playback(PlaybackRequest request);
std::optional<PlaybackRequest> take_pending_playback();
void clear_pending_playback();

void set_last_playback_error(std::string error_message);
std::string take_last_playback_error();

}  // namespace newpipe
