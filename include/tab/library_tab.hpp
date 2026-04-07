#pragma once

#include <atomic>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class LibraryTab : public brls::Box {
public:
    LibraryTab();

    static brls::View* create() { return new LibraryTab(); }

private:
    void refresh();
    void buildGrid();
    void toggleSection();
    void clearCurrentSection();
    bool allowInitialInput() const;
    void playStream(const newpipe::StreamItem& item);
    void openStream(const newpipe::StreamItem& item);

    BRLS_BIND(brls::Label, statusLabel, "library/status");
    BRLS_BIND(brls::Label, bodyLabel, "library/body");
    BRLS_BIND(brls::ProgressSpinner, spinner, "library/spinner");
    BRLS_BIND(brls::ScrollingFrame, scrollFrame, "library/scroll");
    BRLS_BIND(brls::Box, gridBox, "library/grid");

    newpipe::YouTubeCatalogService service_;
    std::vector<newpipe::StreamItem> items_;
    bool showingFavorites_ = false;
    std::atomic<bool> interactionReady_{false};
};
