#pragma once

#include <borealis.hpp>

#include "view/auto_tab_frame.hpp"

class MainActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/main.xml");

    void onContentAvailable() override;

private:
    BRLS_BIND(AutoTabFrame, tabsFrame, "main/tabs");
};
