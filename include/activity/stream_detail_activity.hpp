#pragma once

#include <string>

#include <borealis.hpp>

#include "newpipe/models.hpp"
#include "newpipe/youtube_catalog_service.hpp"

class StreamDetailActivity : public brls::Activity {
public:
    explicit StreamDetailActivity(newpipe::StreamItem item);

    CONTENT_FROM_XML_RES("activity/stream_detail.xml");

    void onContentAvailable() override;

private:
    void loadDetail();
    void updateFavoriteAction();
    void playStream();
    void openChannelFeed();
    void openRelatedFeed();
    void openExtrasMenu();
    void openPlaylistFeed();
    void openComments();
    void toggleFavorite();

    newpipe::StreamItem item_;
    newpipe::YouTubeCatalogService service_;

    BRLS_BIND(brls::Label, titleLabel, "detail/title");
    BRLS_BIND(brls::Label, metaLabel, "detail/meta");
    BRLS_BIND(brls::Label, statusLabel, "detail/status");
    BRLS_BIND(brls::Label, bodyLabel, "detail/body");
    BRLS_BIND(brls::ProgressSpinner, spinner, "detail/spinner");
};
