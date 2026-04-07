#pragma once

#include <borealis.hpp>

#include "newpipe/models.hpp"

class StreamCard : public brls::Box {
public:
    StreamCard();

    void setData(const newpipe::StreamItem& item);

private:
    BRLS_BIND(brls::Image, thumbnail, "stream/thumbnail");
    BRLS_BIND(brls::Label, titleLabel, "stream/title");
    BRLS_BIND(brls::Label, channelLabel, "stream/channel");
    BRLS_BIND(brls::Label, leftMetaLabel, "stream/left_meta");
    BRLS_BIND(brls::Label, rightMetaLabel, "stream/right_meta");
};
