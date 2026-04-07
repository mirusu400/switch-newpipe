#include "tab/settings_tab.hpp"

#include "newpipe/auth_store.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/settings_store.hpp"

namespace {

int language_selection(const std::string& value) {
    if (value == "ko") {
        return 1;
    }
    if (value == "en-US") {
        return 2;
    }
    return 0;
}

std::string language_from_selection(int selection) {
    switch (selection) {
        case 1:
            return "ko";
        case 2:
            return "en-US";
        default:
            return "auto";
    }
}

int playback_quality_selection(newpipe::PlaybackQualityMode mode) {
    switch (mode) {
        case newpipe::PlaybackQualityMode::STANDARD_720:
            return 0;
        case newpipe::PlaybackQualityMode::COMPATIBILITY:
            return 1;
        case newpipe::PlaybackQualityMode::DATA_SAVER:
            return 2;
        default:
            return 0;
    }
}

newpipe::PlaybackQualityMode playback_quality_from_selection(int selection) {
    switch (selection) {
        case 1:
            return newpipe::PlaybackQualityMode::COMPATIBILITY;
        case 2:
            return newpipe::PlaybackQualityMode::DATA_SAVER;
        default:
            return newpipe::PlaybackQualityMode::STANDARD_720;
    }
}

int startup_tab_selection(const std::string& value) {
    if (value == "search") {
        return 1;
    }
    if (value == "subscriptions") {
        return 2;
    }
    if (value == "library") {
        return 3;
    }
    if (value == "settings") {
        return 4;
    }
    return 0;
}

std::string startup_tab_from_selection(int selection) {
    switch (selection) {
        case 1:
            return "search";
        case 2:
            return "subscriptions";
        case 3:
            return "library";
        case 4:
            return "settings";
        default:
            return "home";
    }
}

int home_kiosk_selection(const std::string& value) {
    if (value == "live") {
        return 1;
    }
    if (value == "music") {
        return 2;
    }
    if (value == "gaming") {
        return 3;
    }
    return 0;
}

std::string home_kiosk_from_selection(int selection) {
    switch (selection) {
        case 1:
            return "live";
        case 2:
            return "music";
        case 3:
            return "gaming";
        default:
            return "recommended";
    }
}

}  // namespace

SettingsTab::SettingsTab() {
    this->inflateFromXMLRes("xml/tabs/settings.xml");

    this->registerAction(newpipe::tr("settings/reset_action"), brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        this->resetToDefaults();
        return true;
    });

    this->syncLocalizedText();

    this->languageCell->init(
        newpipe::tr("settings/language/title"),
        {
            newpipe::tr("settings/language/options/auto"),
            newpipe::tr("settings/language/options/korean"),
            newpipe::tr("settings/language/options/english"),
        },
        0,
        [this](int selection) {
            std::string error;
            if (!newpipe::SettingsStore::instance().update_language(
                    language_from_selection(selection), &error)) {
                brls::Application::notify(
                    error.empty() ? newpipe::tr("settings/language/save_failed") : error);
                this->syncFromStore();
                return;
            }
            brls::Application::notify(newpipe::tr("settings/language/saved"));
        });

    this->playbackQualityCell->init(
        newpipe::tr("settings/playback_quality/title"),
        {
            newpipe::tr("settings/playback_quality/options/standard"),
            newpipe::tr("settings/playback_quality/options/compatibility"),
            newpipe::tr("settings/playback_quality/options/data_saver"),
        },
        0,
        [this](int selection) {
            std::string error;
            if (!newpipe::SettingsStore::instance().update_playback_quality(
                    playback_quality_from_selection(selection), &error)) {
                brls::Application::notify(
                    error.empty() ? newpipe::tr("settings/playback_quality/save_failed") : error);
                this->syncFromStore();
                return;
            }
            brls::Application::notify(newpipe::tr("settings/playback_quality/saved"));
        });

    this->startupTabCell->init(
        newpipe::tr("settings/startup_tab/title"),
        {
            newpipe::tr("app/home"),
            newpipe::tr("app/search"),
            newpipe::tr("app/subscriptions"),
            newpipe::tr("app/library"),
            newpipe::tr("app/settings"),
        },
        0,
        [this](int selection) {
            std::string error;
            if (!newpipe::SettingsStore::instance().update_startup_tab(
                    startup_tab_from_selection(selection), &error)) {
                brls::Application::notify(
                    error.empty() ? newpipe::tr("settings/startup_tab/save_failed") : error);
                this->syncFromStore();
                return;
            }
            brls::Application::notify(newpipe::tr("settings/startup_tab/saved"));
        });

    this->homeKioskCell->init(
        newpipe::tr("settings/home_kiosk/title"),
        {
            newpipe::tr("settings/home_kiosk/options/recommended"),
            newpipe::tr("settings/home_kiosk/options/live"),
            newpipe::tr("settings/home_kiosk/options/music"),
            newpipe::tr("settings/home_kiosk/options/gaming"),
        },
        0,
        [this](int selection) {
            std::string error;
            if (!newpipe::SettingsStore::instance().update_home_kiosk(
                    home_kiosk_from_selection(selection), &error)) {
                brls::Application::notify(
                    error.empty() ? newpipe::tr("settings/home_kiosk/save_failed") : error);
                this->syncFromStore();
                return;
            }
            brls::Application::notify(newpipe::tr("settings/home_kiosk/saved"));
        });

    this->hideShortsCell->init(newpipe::tr("settings/hide_shorts/title"), false, [this](bool value) {
        std::string error;
        if (!newpipe::SettingsStore::instance().update_hide_short_videos(value, &error)) {
            brls::Application::notify(
                error.empty() ? newpipe::tr("settings/hide_shorts/save_failed") : error);
            this->syncFromStore();
            return;
        }
        brls::Application::notify(
            value ? newpipe::tr("settings/hide_shorts/enabled")
                  : newpipe::tr("settings/hide_shorts/disabled"));
    });

    this->sessionCell->setText(newpipe::tr("settings/session/title"));
    this->sessionCell->registerClickAction([this](brls::View*) {
        this->openSessionInfo();
        return true;
    });

    this->storageCell->setText(newpipe::tr("settings/storage/title"));
    this->storageCell->registerClickAction([this](brls::View*) {
        this->openStorageInfo();
        return true;
    });

    this->syncFromStore();
}

void SettingsTab::syncLocalizedText() {
    if (this->languageCell) {
        this->languageCell->setText(newpipe::tr("settings/language/title"));
    }
    if (this->playbackQualityCell) {
        this->playbackQualityCell->setText(newpipe::tr("settings/playback_quality/title"));
    }
    if (this->startupTabCell) {
        this->startupTabCell->setText(newpipe::tr("settings/startup_tab/title"));
    }
    if (this->homeKioskCell) {
        this->homeKioskCell->setText(newpipe::tr("settings/home_kiosk/title"));
    }
    if (this->hideShortsCell) {
        this->hideShortsCell->setText(newpipe::tr("settings/hide_shorts/title"));
    }
    if (this->sessionCell) {
        this->sessionCell->setText(newpipe::tr("settings/session/title"));
    }
    if (this->storageCell) {
        this->storageCell->setText(newpipe::tr("settings/storage/title"));
    }
}

void SettingsTab::syncFromStore() {
    std::string error;
    newpipe::SettingsStore::instance().load(&error);
    const newpipe::AppSettings settings = newpipe::SettingsStore::instance().settings();

    this->languageCell->setSelection(language_selection(settings.language), true);
    this->playbackQualityCell->setSelection(
        playback_quality_selection(settings.playback_quality), true);
    this->startupTabCell->setSelection(startup_tab_selection(settings.startup_tab), true);
    this->homeKioskCell->setSelection(home_kiosk_selection(settings.home_kiosk), true);
    this->hideShortsCell->setOn(settings.hide_short_videos, false);

    if (!newpipe::AuthStore::instance().load(&error) && !error.empty()) {
        this->sessionCell->setDetailText(newpipe::tr("settings/session/load_failed"));
    } else if (newpipe::AuthStore::instance().has_session()) {
        const auto session = newpipe::AuthStore::instance().session();
        this->sessionCell->setDetailText(
            session.display_name.empty() ? newpipe::tr("settings/session/saved")
                                         : session.display_name);
    } else {
        this->sessionCell->setDetailText(newpipe::tr("settings/session/signed_out"));
    }

    this->storageCell->setDetailText(newpipe::tr("settings/storage/detail"));
}

void SettingsTab::resetToDefaults() {
    std::string error;
    if (!newpipe::SettingsStore::instance().reset(&error)) {
        brls::Application::notify(error.empty() ? newpipe::tr("settings/reset_failed") : error);
        return;
    }

    this->syncFromStore();
    brls::Application::notify(newpipe::tr("settings/reset_done"));
}

void SettingsTab::openSessionInfo() {
    std::string error;
    newpipe::AuthStore::instance().load(&error);
    const auto session = newpipe::AuthStore::instance().session();

    std::string body;
    if (!error.empty()) {
        body = newpipe::tr("settings/session/dialog/load_failed", error);
    } else if (!newpipe::AuthStore::instance().has_session()) {
        body = newpipe::tr("settings/session/dialog/signed_out");
    } else {
        body = newpipe::tr(
            "settings/session/dialog/saved",
            session.display_name.empty() ? newpipe::tr("common/none") : session.display_name,
            newpipe::default_auth_session_path());
        if (!session.source_path.empty()) {
            body += "\n" + newpipe::tr("settings/session/dialog/source", session.source_path);
        }
    }

    auto* dialog = new brls::Dialog(body);
    dialog->addButton(newpipe::tr("common/close"), [dialog]() { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

void SettingsTab::openStorageInfo() {
    const std::string body = newpipe::tr(
        "settings/storage/dialog/body",
        newpipe::default_settings_store_path(),
        newpipe::default_auth_session_path(),
        newpipe::default_auth_import_path(),
        newpipe::default_library_store_path());

    auto* dialog = new brls::Dialog(body);
    dialog->addButton(newpipe::tr("common/close"), [dialog]() { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}
