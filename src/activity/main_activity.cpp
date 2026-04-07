#include "activity/main_activity.hpp"

#include "newpipe/i18n.hpp"
#include "newpipe/settings_store.hpp"

namespace {

size_t startup_tab_index(const std::string& tab_id) {
    if (tab_id == "search") {
        return 1;
    }
    if (tab_id == "subscriptions") {
        return 2;
    }
    if (tab_id == "library") {
        return 3;
    }
    if (tab_id == "settings") {
        return 4;
    }
    return 0;
}

}  // namespace

void MainActivity::onContentAvailable() {
    this->registerAction(newpipe::tr("common/info"), brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        auto* dialog = new brls::Dialog(newpipe::tr("app/info_body"));
        dialog->addButton(newpipe::tr("hints/ok"), [dialog]() { dialog->close(); });
        dialog->setCancelable(true);
        dialog->open();
        return true;
    });

    if (this->tabsFrame) {
        const size_t index = startup_tab_index(newpipe::SettingsStore::instance().settings().startup_tab);
        this->tabsFrame->setDefaultTabIndex(index);
        this->tabsFrame->focusTab(static_cast<int>(index));
    }
}
