#include "newpipe/settings_store.hpp"

#include <fstream>
#include <iterator>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"

namespace newpipe {
namespace {

using nlohmann::json;

std::string get_string(const json& node, const char* key) {
    if (!node.is_object() || !node.contains(key) || !node.at(key).is_string()) {
        return {};
    }

    return node.at(key).get<std::string>();
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

std::string sanitize_startup_tab(std::string value) {
    for (const char* allowed : {"home", "search", "subscriptions", "library", "settings"}) {
        if (value == allowed) {
            return value;
        }
    }

    return "home";
}

std::string sanitize_language(std::string value) {
    for (const char* allowed : {"auto", "ko", "en-US"}) {
        if (value == allowed) {
            return value;
        }
    }

    return "auto";
}

std::string sanitize_home_kiosk(std::string value) {
    for (const char* allowed : {"recommended", "live", "music", "gaming"}) {
        if (value == allowed) {
            return value;
        }
    }

    return "recommended";
}

PlaybackQualityMode sanitize_playback_quality(int value) {
    switch (value) {
        case static_cast<int>(PlaybackQualityMode::STANDARD_720):
            return PlaybackQualityMode::STANDARD_720;
        case static_cast<int>(PlaybackQualityMode::COMPATIBILITY):
            return PlaybackQualityMode::COMPATIBILITY;
        case static_cast<int>(PlaybackQualityMode::DATA_SAVER):
            return PlaybackQualityMode::DATA_SAVER;
        default:
            return PlaybackQualityMode::STANDARD_720;
    }
}

json serialize_settings(const AppSettings& settings) {
    return {
        {"language", settings.language},
        {"startup_tab", settings.startup_tab},
        {"home_kiosk", settings.home_kiosk},
        {"playback_quality", static_cast<int>(settings.playback_quality)},
        {"hide_short_videos", settings.hide_short_videos},
    };
}

AppSettings deserialize_settings(const json& root) {
    AppSettings settings;
    settings.language = sanitize_language(get_string(root, "language"));
    settings.startup_tab = sanitize_startup_tab(get_string(root, "startup_tab"));
    settings.home_kiosk = sanitize_home_kiosk(get_string(root, "home_kiosk"));
    settings.playback_quality =
        sanitize_playback_quality(root.value("playback_quality", 0));
    settings.hide_short_videos = root.value("hide_short_videos", false);
    return settings;
}

}  // namespace

std::string default_settings_store_path() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe_settings.json";
#else
    return "switch_newpipe_settings.json";
#endif
}

SettingsStore& SettingsStore::instance() {
    static SettingsStore store;
    return store;
}

bool SettingsStore::load(std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->loaded_) {
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    std::string raw;
    if (!read_text_file(default_settings_store_path(), raw)) {
        this->settings_ = AppSettings{};
        this->loaded_ = true;
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    const json root = json::parse(raw, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (error_message) {
            *error_message = "설정 파일 파싱 실패";
        }
        return false;
    }

    this->settings_ = deserialize_settings(root);
    this->loaded_ = true;
    if (error_message) {
        error_message->clear();
    }

    logf("settings: loaded language=%s startup=%s kiosk=%s quality=%d hide_shorts=%d",
         this->settings_.language.c_str(),
         this->settings_.startup_tab.c_str(),
         this->settings_.home_kiosk.c_str(),
         static_cast<int>(this->settings_.playback_quality),
         this->settings_.hide_short_videos ? 1 : 0);
    return true;
}

bool SettingsStore::reload(std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->loaded_ = false;
    this->settings_ = AppSettings{};
    if (error_message) {
        error_message->clear();
    }
    return true;
}

AppSettings SettingsStore::settings() {
    std::string error;
    this->ensure_loaded(&error);
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->settings_;
}

bool SettingsStore::update_language(const std::string& language, std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->settings_.language = sanitize_language(language);
    }

    return this->persist(error_message);
}

bool SettingsStore::update_startup_tab(const std::string& startup_tab, std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->settings_.startup_tab = sanitize_startup_tab(startup_tab);
    }

    return this->persist(error_message);
}

bool SettingsStore::update_home_kiosk(const std::string& home_kiosk, std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->settings_.home_kiosk = sanitize_home_kiosk(home_kiosk);
    }

    return this->persist(error_message);
}

bool SettingsStore::update_playback_quality(
    PlaybackQualityMode playback_quality,
    std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->settings_.playback_quality = sanitize_playback_quality(static_cast<int>(playback_quality));
    }

    return this->persist(error_message);
}

bool SettingsStore::update_hide_short_videos(bool hide_short_videos, std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->settings_.hide_short_videos = hide_short_videos;
    }

    return this->persist(error_message);
}

bool SettingsStore::reset(std::string* error_message) {
    if (!this->ensure_loaded(error_message)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->settings_ = AppSettings{};
    }

    return this->persist(error_message);
}

bool SettingsStore::ensure_loaded(std::string* error_message) {
    if (this->loaded_) {
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    return this->load(error_message);
}

bool SettingsStore::persist(std::string* error_message) {
    json root;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        root = serialize_settings(this->settings_);
    }

    if (!write_text_file(default_settings_store_path(), root.dump(2))) {
        if (error_message) {
            *error_message = "설정 저장 실패";
        }
        return false;
    }

    if (error_message) {
        error_message->clear();
    }
    return true;
}

}  // namespace newpipe
