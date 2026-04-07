#pragma once

#include <atomic>
#include <string>
#include <vector>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class StreamFeedActivity : public brls::Activity {
public:
    StreamFeedActivity(std::string title, std::vector<newpipe::StreamItem> items);

    CONTENT_FROM_XML_RES("activity/stream_feed.xml");

    void onContentAvailable() override;

private:
    void buildGrid();
    bool allowInitialInput() const;
    void playStream(const newpipe::StreamItem& item);
    void openStream(const newpipe::StreamItem& item);

    std::string title_;
    std::vector<newpipe::StreamItem> items_;
    newpipe::YouTubeCatalogService service_;
    std::atomic<bool> interactionReady_{false};

    BRLS_BIND(brls::Label, statusLabel, "feed/status");
    BRLS_BIND(brls::Label, subtitleLabel, "feed/subtitle");
    BRLS_BIND(brls::ScrollingFrame, scrollFrame, "feed/scroll");
    BRLS_BIND(brls::Box, gridBox, "feed/grid");
};
