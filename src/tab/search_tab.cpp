#include "tab/search_tab.hpp"

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

SearchTab::SearchTab() : service_() {
    this->inflateFromXMLRes("xml/tabs/search.xml");
    newpipe::log_line("search: construct");
    brls::delay(700, [this]() {
        interactionReady_.store(true);
        newpipe::log_line("search: interaction ready");
    });

    this->registerAction(newpipe::tr("search/action"), brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        brls::Application::getImeManager()->openForText(
            [this](const std::string& text) { doSearch(text); },
            newpipe::tr("search/ime_title"),
            newpipe::tr("search/ime_subtitle"),
            80,
            lastQuery_);
        return true;
    });
}

void SearchTab::doSearch(const std::string& query) {
    lastQuery_ = query;
    newpipe::logf("search: doSearch query=%s", query.c_str());

    if (spinner) {
        spinner->setVisibility(brls::Visibility::VISIBLE);
    }

    if (!service_.is_loaded()) {
        if (statusLabel) {
            statusLabel->setText(newpipe::tr("common/service_init_failed", service_.error_message()));
        }
        if (spinner) {
            spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    if (query.empty()) {
        items_.clear();
        if (gridBox) {
            gridBox->clearViews();
        }
        if (statusLabel) {
            statusLabel->setText(newpipe::tr("search/prompt"));
        }
        if (spinner) {
            spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    const auto results = service_.search(query);
    items_ = results.items;
    newpipe::logf("search: results=%zu", items_.size());
    buildGrid();

    if (statusLabel) {
        if (items_.empty()) {
            if (service_.error_message().empty()) {
                statusLabel->setText(newpipe::tr("search/no_results", query));
            } else {
                statusLabel->setText(service_.error_message());
            }
        } else {
            statusLabel->setText(newpipe::tr("search/results_count", query, items_.size()));
        }
    }
    if (spinner) {
        spinner->setVisibility(brls::Visibility::GONE);
    }
}

void SearchTab::buildGrid() {
    if (!gridBox) {
        newpipe::log_line("search: buildGrid gridBox missing");
        return;
    }

    newpipe::logf("search: buildGrid items=%zu", items_.size());
    gridBox->clearViews();
    for (size_t i = 0; i < items_.size(); i += kGridColumns) {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(8);

        for (size_t j = i; j < i + kGridColumns && j < items_.size(); j++) {
            auto* card = new StreamCard();
            card->setData(items_[j]);
            const size_t idx = j;
            card->registerClickAction([this, idx](brls::View*) {
                playStream(items_[idx]);
                return true;
            });
            card->registerAction(newpipe::tr("common/info"), brls::ControllerButton::BUTTON_Y, [this, idx](brls::View*) {
                openStream(items_[idx]);
                return true;
            });
            card->addGestureRecognizer(new brls::TapGestureRecognizer(card));
            row->addView(card);
        }

        gridBox->addView(row);
    }
}

bool SearchTab::allowInitialInput() const {
    if (interactionReady_.load()) {
        return true;
    }

    newpipe::log_line("search: ignored startup input");
    return false;
}

void SearchTab::playStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    newpipe::logf("search: playStream url=%s", item.url.c_str());
    const auto detail = service_.get_stream_detail(item.url);
    const auto request = newpipe::build_playback_request(item, detail);
    if (!request.has_value()) {
        openStream(item);
        return;
    }

    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(detail.has_value() ? detail->item : item, &ignored_error);
    newpipe::logf("search: queue playback url=%s", request->url.c_str());
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void SearchTab::openStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    newpipe::logf("search: openStream url=%s", item.url.c_str());
    brls::Application::pushActivity(new StreamDetailActivity(item));
}
