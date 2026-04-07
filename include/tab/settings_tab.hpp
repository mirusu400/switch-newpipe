#pragma once

#include <borealis.hpp>

class SettingsTab : public brls::Box {
public:
    SettingsTab();

    static brls::View* create() { return new SettingsTab(); }

private:
    void syncLocalizedText();
    void syncFromStore();
    void resetToDefaults();
    void openSessionInfo();
    void openStorageInfo();

    BRLS_BIND(brls::SelectorCell, languageCell, "settings/language");
    BRLS_BIND(brls::SelectorCell, playbackQualityCell, "settings/playback_quality");
    BRLS_BIND(brls::SelectorCell, startupTabCell, "settings/startup_tab");
    BRLS_BIND(brls::SelectorCell, homeKioskCell, "settings/home_kiosk");
    BRLS_BIND(brls::BooleanCell, hideShortsCell, "settings/hide_shorts");
    BRLS_BIND(brls::DetailCell, sessionCell, "settings/session");
    BRLS_BIND(brls::DetailCell, storageCell, "settings/storage");
};
