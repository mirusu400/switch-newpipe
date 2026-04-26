#include "tab/subscriptions_tab.hpp"

#include "activity/stream_detail_activity.hpp"
#include "newpipe/auth_store.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"
#include "view/stream_card.hpp"

namespace {
constexpr size_t kGridColumns = 4;
}

SubscriptionsTab::SubscriptionsTab() : service_() {
    this->inflateFromXMLRes("xml/tabs/subscriptions.xml");
    newpipe::log_line("subscriptions: construct");
    brls::delay(700, [this]() { interactionReady_.store(true); });

    this->registerAction(newpipe::tr("common/refresh"), brls::ControllerButton::BUTTON_X, [this](brls::View*) {
        this->refresh();
        return true;
    });
    this->registerAction(newpipe::tr("subscriptions/session_action"), brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        this->openSessionDialog();
        return true;
    });

    this->refresh();
}

bool SubscriptionsTab::allowInitialInput() const {
    return interactionReady_.load();
}

void SubscriptionsTab::refresh() {
    newpipe::log_line("subscriptions: refresh");
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::VISIBLE);
    }

    std::string auth_error;
    if (!this->service_.load_auth_session(&auth_error)) {
        if (this->statusLabel) {
            this->statusLabel->setText(
                auth_error.empty() ? newpipe::tr("subscriptions/session_load_failed") : auth_error);
        }
        if (this->bodyLabel) {
            this->bodyLabel->setText(newpipe::tr("subscriptions/session_load_failed_body"));
        }
        this->clearGrid();
        if (this->spinner) {
            this->spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    if (!this->service_.has_auth_session()) {
        this->showSignedOutState();
        if (this->spinner) {
            this->spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    const auto feed = this->service_.get_subscriptions_feed();
    if (!feed.has_value()) {
        if (this->statusLabel) {
            this->statusLabel->setText(this->service_.error_message().empty()
                    ? newpipe::tr("subscriptions/feed_load_failed")
                    : this->service_.error_message());
        }
        const auto session = this->service_.auth_session();
        if (this->bodyLabel) {
            std::string body = newpipe::tr("subscriptions/feed_load_failed_body");
            if (!session.source_path.empty()) {
                body += "\n\n" + newpipe::tr("subscriptions/current_source", session.source_path);
            }
            this->bodyLabel->setText(body);
        }
        this->clearGrid();
        if (this->spinner) {
            this->spinner->setVisibility(brls::Visibility::GONE);
        }
        return;
    }

    this->items_ = feed->items;
    this->buildGrid();

    if (this->statusLabel) {
        this->statusLabel->setText(newpipe::tr("common/count_with_title", feed->kiosk.title, this->items_.size()));
    }
    if (this->bodyLabel) {
        const auto session = this->service_.auth_session();
        std::string session_name;
        if (!session.display_name.empty()) {
            session_name = session.display_name;
        } else if (!session.source_path.empty()) {
            session_name = session.source_path;
        } else {
            session_name = newpipe::tr("settings/session/saved");
        }
        std::string body = newpipe::tr("subscriptions/session_prefix", session_name);
        body += "\n" + newpipe::tr("subscriptions/controls");
        this->bodyLabel->setText(body);
    }
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::GONE);
    }
}

void SubscriptionsTab::buildGrid() {
    if (!this->gridBox) {
        return;
    }

    // See HomeTab::buildGrid — work around borealis giveFocus(nullptr) no-op
    // that would leave a dangling currentFocus after clearing the focused card.
    if (this->scrollFrame) {
        for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent()) {
            if (v == this->gridBox) {
                brls::Application::giveFocus(this->scrollFrame);
                break;
            }
        }
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

void SubscriptionsTab::clearGrid() {
    this->items_.clear();
    if (this->gridBox) {
        if (this->scrollFrame) {
            for (brls::View* v = brls::Application::getCurrentFocus(); v; v = v->getParent()) {
                if (v == this->gridBox) {
                    brls::Application::giveFocus(this->scrollFrame);
                    break;
                }
            }
        }
        this->gridBox->clearViews();
    }
}

void SubscriptionsTab::showSignedOutState() {
    this->clearGrid();
    if (this->statusLabel) {
        this->statusLabel->setText(newpipe::tr("subscriptions/signed_out_title"));
    }
    if (this->bodyLabel) {
        const std::string body = newpipe::tr(
            "subscriptions/signed_out_body", newpipe::default_auth_import_path());
        this->bodyLabel->setText(body);
    }
}

void SubscriptionsTab::openSessionDialog() {
    const auto session = this->service_.auth_session();

    std::string body;
    if (this->service_.has_auth_session()) {
        if (!session.source_path.empty()) {
            body = newpipe::tr("subscriptions/session_dialog/saved", session.source_path);
        } else if (!session.source_label.empty()) {
            body = newpipe::tr("subscriptions/session_dialog/saved", session.source_label);
        } else {
            body = newpipe::tr("subscriptions/session_dialog/saved", "manual");
        }
    } else {
        body = newpipe::tr("subscriptions/session_dialog/signed_out", newpipe::default_auth_import_path());
    }

    auto* dialog = new brls::Dialog(body);
    dialog->addButton(newpipe::tr("subscriptions/session_dialog/load_file"), [this, dialog]() {
        dialog->close();
        std::string error;
        if (this->service_.import_auth_session_from_file({}, &error)) {
            brls::Application::notify(newpipe::tr("subscriptions/session_dialog/load_file_done"));
        } else {
            brls::Application::notify(
                error.empty() ? newpipe::tr("subscriptions/session_dialog/load_file_failed") : error);
        }
        this->refresh();
    });
    dialog->addButton(newpipe::tr("subscriptions/session_dialog/input_cookie"), [this, dialog]() {
        dialog->close();
        brls::Application::getImeManager()->openForText(
            [this](const std::string& text) { this->handleManualCookieInput(text); },
            newpipe::tr("subscriptions/session_dialog/ime_title"),
            newpipe::tr("subscriptions/session_dialog/ime_subtitle"),
            4096,
            "");
    });
    if (this->service_.has_auth_session()) {
        dialog->addButton(newpipe::tr("subscriptions/session_dialog/logout"), [this, dialog]() {
            dialog->close();
            std::string error;
            if (this->service_.clear_auth_session(&error)) {
                brls::Application::notify(newpipe::tr("subscriptions/session_dialog/logout_done"));
            } else {
                brls::Application::notify(
                    error.empty() ? newpipe::tr("subscriptions/session_dialog/logout_failed") : error);
            }
            this->refresh();
        });
    }
    dialog->addButton(newpipe::tr("common/close"), [dialog]() { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

void SubscriptionsTab::handleManualCookieInput(const std::string& text) {
    if (text.empty()) {
        brls::Application::notify(newpipe::tr("subscriptions/session_dialog/empty_input"));
        return;
    }

    std::string error;
    if (this->service_.update_auth_session_from_cookie(text, "manual input", &error)) {
        brls::Application::notify(newpipe::tr("subscriptions/session_dialog/save_cookie_done"));
    } else {
        brls::Application::notify(
            error.empty() ? newpipe::tr("subscriptions/session_dialog/save_cookie_failed") : error);
    }
    this->refresh();
}

void SubscriptionsTab::playStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    newpipe::logf("subscriptions: playStream url=%s", item.url.c_str());
    const auto detail = this->service_.get_stream_detail(item.url);
    const auto request = newpipe::build_playback_request(item, detail);
    if (!request.has_value()) {
        this->openStream(item);
        return;
    }

    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(detail.has_value() ? detail->item : item, &ignored_error);
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void SubscriptionsTab::openStream(const newpipe::StreamItem& item) {
    if (!allowInitialInput()) {
        return;
    }
    brls::Application::pushActivity(new StreamDetailActivity(item));
}
