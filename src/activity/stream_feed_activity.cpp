#include "activity/stream_feed_activity.hpp"

#include "activity/stream_detail_activity.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"
#include "view/stream_card.hpp"

namespace {
constexpr size_t kGridColumns = 4;
}

StreamFeedActivity::StreamFeedActivity(std::string title, std::vector<newpipe::StreamItem> items)
    : title_(std::move(title))
    , items_(std::move(items))
    , service_() {
}

void StreamFeedActivity::onContentAvailable() {
    brls::delay(500, [this]() { interactionReady_.store(true); });
    this->registerAction(newpipe::tr("hints/back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    if (this->statusLabel) {
        this->statusLabel->setText(this->title_);
    }
    if (this->subtitleLabel) {
        this->subtitleLabel->setText(newpipe::tr("feed/subtitle"));
    }

    this->buildGrid();
}

bool StreamFeedActivity::allowInitialInput() const {
    return interactionReady_.load();
}

void StreamFeedActivity::buildGrid() {
    if (!this->gridBox) {
        return;
    }

    this->gridBox->clearViews();
    for (size_t i = 0; i < this->items_.size(); i += kGridColumns) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(8);

        for (size_t j = i; j < i + kGridColumns && j < this->items_.size(); j++) {
            auto* card = new StreamCard();
            card->setData(this->items_[j]);
            const size_t idx = j;
            card->registerClickAction([this, idx](brls::View*) {
                this->playStream(this->items_[idx]);
                return true;
            });
            card->registerAction(newpipe::tr("common/info"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                this->openStream(this->items_[idx]);
                return true;
            });
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            row->addView(card);
        }

        this->gridBox->addView(row);
    }
}

void StreamFeedActivity::playStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    const auto detail = this->service_.get_stream_detail(item.url);
    const auto request = newpipe::build_playback_request(item, detail);
    if (!request.has_value()) {
        this->openStream(item);
        return;
    }

    const newpipe::StreamItem history_item = detail.has_value() ? detail->item : item;
    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(history_item, &ignored_error);
    newpipe::logf("feed_activity: queue playback url=%s", request->url.c_str());
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void StreamFeedActivity::openStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    brls::Application::pushActivity(new StreamDetailActivity(item));
}
