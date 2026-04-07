#pragma once

#include <atomic>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/settings_store.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class HomeTab : public brls::Box {
public:
    HomeTab();

    static brls::View* create() { return new HomeTab(); }

private:
    void loadHome();
    void scheduleLoadHome(long delay_ms);
    void buildGrid();
    void cycleKiosk();
    bool allowInitialInput() const;
    void playStream(const newpipe::StreamItem& item);
    void openStream(const newpipe::StreamItem& item);

    BRLS_BIND(brls::Label, statusLabel, "home/status");
    BRLS_BIND(brls::ProgressSpinner, spinner, "home/spinner");
    BRLS_BIND(brls::ScrollingFrame, scrollFrame, "home/scroll");
    BRLS_BIND(brls::Box, gridBox, "home/grid");

    newpipe::YouTubeCatalogService service_;
    std::vector<newpipe::Kiosk> kiosks_;
    std::vector<newpipe::StreamItem> items_;
    size_t kioskIndex_ = 0;
    bool initialLoadCompleted_ = false;
    int initialLoadAttempts_ = 0;
    std::atomic<bool> interactionReady_{false};
};
