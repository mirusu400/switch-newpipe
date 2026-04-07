#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class SubscriptionsTab : public brls::Box {
public:
    SubscriptionsTab();

    static brls::View* create() { return new SubscriptionsTab(); }

private:
    void refresh();
    void buildGrid();
    void clearGrid();
    void showSignedOutState();
    void openSessionDialog();
    void handleManualCookieInput(const std::string& text);
    bool allowInitialInput() const;
    void playStream(const newpipe::StreamItem& item);
    void openStream(const newpipe::StreamItem& item);

    BRLS_BIND(brls::Label, statusLabel, "subscriptions/status");
    BRLS_BIND(brls::Label, bodyLabel, "subscriptions/body");
    BRLS_BIND(brls::ProgressSpinner, spinner, "subscriptions/spinner");
    BRLS_BIND(brls::ScrollingFrame, scrollFrame, "subscriptions/scroll");
    BRLS_BIND(brls::Box, gridBox, "subscriptions/grid");

    newpipe::YouTubeCatalogService service_;
    std::vector<newpipe::StreamItem> items_;
    std::atomic<bool> interactionReady_{false};
};
