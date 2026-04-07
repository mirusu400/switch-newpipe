#include "activity/comment_feed_activity.hpp"
#include "activity/stream_detail_activity.hpp"

#include "activity/stream_feed_activity.hpp"
#include "newpipe/i18n.hpp"
#include "newpipe/library_store.hpp"
#include "newpipe/log.hpp"
#include "newpipe/playback_helper.hpp"
#include "newpipe/runtime.hpp"

StreamDetailActivity::StreamDetailActivity(newpipe::StreamItem item)
    : item_(std::move(item))
    , service_() {
}

void StreamDetailActivity::onContentAvailable() {
    this->registerAction(newpipe::tr("hints/back"), brls::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    });
    this->registerAction(newpipe::tr("detail/play_action"), brls::BUTTON_A, [this](brls::View*) {
        this->playStream();
        return true;
    });
    this->registerAction(newpipe::tr("detail/channel_action"), brls::BUTTON_X, [this](brls::View*) {
        this->openChannelFeed();
        return true;
    });
    this->registerAction(newpipe::tr("detail/related_action"), brls::BUTTON_Y, [this](brls::View*) {
        this->openRelatedFeed();
        return true;
    });
    this->registerAction(newpipe::tr("detail/more_action"), brls::BUTTON_LB, [this](brls::View*) {
        this->openExtrasMenu();
        return true;
    });
    this->registerAction(newpipe::tr("detail/favorite_action"), brls::BUTTON_RB, [this](brls::View*) {
        this->toggleFavorite();
        return true;
    });

    this->loadDetail();
}

void StreamDetailActivity::loadDetail() {
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::VISIBLE);
    }

    const auto detail = this->service_.get_stream_detail(this->item_.url);
    if (detail.has_value()) {
        this->item_ = detail->item;
    }

    if (this->titleLabel) {
        this->titleLabel->setText(
            this->item_.title.empty() ? newpipe::tr("detail/default_title") : this->item_.title);
    }

    std::string meta;
    if (!this->item_.channel_name.empty()) {
        meta += this->item_.channel_name;
    }
    if (!this->item_.view_count_text.empty()) {
        if (!meta.empty()) {
            meta += " • ";
        }
        meta += this->item_.view_count_text;
    }
    if (!this->item_.published_text.empty()) {
        if (!meta.empty()) {
            meta += " • ";
        }
        meta += this->item_.published_text;
    }
    if (!this->item_.duration_text.empty()) {
        if (!meta.empty()) {
            meta += " • ";
        }
        meta += this->item_.duration_text;
    }
    if (this->metaLabel) {
        this->metaLabel->setText(meta);
    }

    std::string body;
    if (detail.has_value() && !detail->description.empty()) {
        body = detail->description;
    }
    if (body.empty()) {
        body = newpipe::tr("detail/no_description");
    }
    if (!this->item_.url.empty()) {
        body += "\n\n" + this->item_.url;
    }

    if (this->bodyLabel) {
        this->bodyLabel->setText(body);
    }
    if (this->statusLabel) {
        std::string status = newpipe::tr("detail/status_play");
        const bool has_channel = !this->item_.channel_id.empty() || !this->item_.channel_url.empty();
        const bool has_related = detail.has_value() && !detail->related_items.empty();
        const bool has_playlist = this->item_.url.find("list=") != std::string::npos;
        status += has_channel ? newpipe::tr("detail/status_channel") : "";
        status += has_related ? newpipe::tr("detail/status_related") : "";
        status += has_playlist ? newpipe::tr("detail/status_more") : newpipe::tr("detail/status_comments");
        status += newpipe::tr("detail/status_favorite");
        this->statusLabel->setText(status);

        if (this->getContentView()) {
            this->getContentView()->setActionAvailable(brls::BUTTON_X, has_channel);
            this->getContentView()->setActionAvailable(brls::BUTTON_Y, has_related);
            this->getContentView()->setActionAvailable(brls::BUTTON_LB, true);
        }
    }

    this->updateFavoriteAction();
    if (this->spinner) {
        this->spinner->setVisibility(brls::Visibility::GONE);
    }
}

void StreamDetailActivity::updateFavoriteAction() {
    if (this->getContentView()) {
        const bool favorite = newpipe::LibraryStore::instance().is_favorite(this->item_.url);
        this->getContentView()->updateActionHint(
            brls::BUTTON_RB,
            favorite ? newpipe::tr("detail/unfavorite_action")
                     : newpipe::tr("detail/favorite_action"));
    }
}

void StreamDetailActivity::playStream() {
    const auto detail = this->service_.get_stream_detail(this->item_.url);
    const auto request = newpipe::build_playback_request(this->item_, detail);
    if (!request.has_value()) {
        brls::Application::notify(newpipe::tr("detail/playback_url_failed"));
        return;
    }

    const newpipe::StreamItem history_item = detail.has_value() ? detail->item : this->item_;
    std::string ignored_error;
    newpipe::LibraryStore::instance().add_history(history_item, &ignored_error);
    newpipe::queue_playback(*request);
    brls::Application::quit();
}

void StreamDetailActivity::openChannelFeed() {
    const auto feed = this->service_.get_channel_feed(this->item_);
    if (!feed.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/channel_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    brls::Application::pushActivity(new StreamFeedActivity(feed->kiosk.title, feed->items));
}

void StreamDetailActivity::openRelatedFeed() {
    const auto feed = this->service_.get_related_feed(this->item_);
    if (!feed.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/related_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    brls::Application::pushActivity(new StreamFeedActivity(feed->kiosk.title, feed->items));
}

void StreamDetailActivity::openExtrasMenu() {
    auto* dialog = new brls::Dialog(newpipe::tr("detail/extras_title"));

    if (this->item_.url.find("list=") != std::string::npos) {
        dialog->addButton(newpipe::tr("detail/playlist_action"), [this, dialog]() {
            dialog->close();
            this->openPlaylistFeed();
        });
    }

    dialog->addButton(newpipe::tr("detail/comments_action"), [this, dialog]() {
        dialog->close();
        this->openComments();
    });
    dialog->addButton(newpipe::tr("common/close"), [dialog]() { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

void StreamDetailActivity::openPlaylistFeed() {
    const auto feed = this->service_.get_playlist_feed(this->item_);
    if (!feed.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/playlist_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    std::string title = feed->kiosk.title;
    if (title.empty() || title == "재생목록" || title == "Playlist") {
        title = newpipe::tr("detail/playlist_action");
    }

    brls::Application::pushActivity(new StreamFeedActivity(title, feed->items));
}

void StreamDetailActivity::openComments() {
    const auto page = this->service_.get_comments(this->item_);
    if (!page.has_value()) {
        brls::Application::notify(
            this->service_.error_message().empty() ? newpipe::tr("detail/comments_load_failed")
                                                   : this->service_.error_message());
        return;
    }

    std::string title = page->title;
    if (title.empty() || title == "댓글" || title == "Comments") {
        title = newpipe::tr("comments/title");
    }

    brls::Application::pushActivity(new CommentFeedActivity(title, page->items));
}

void StreamDetailActivity::toggleFavorite() {
    bool favorite = false;
    std::string error;
    if (!newpipe::LibraryStore::instance().toggle_favorite(this->item_, &favorite, &error)) {
        brls::Application::notify(error.empty() ? newpipe::tr("detail/favorite_save_failed") : error);
        return;
    }

    this->updateFavoriteAction();
    brls::Application::notify(
        favorite ? newpipe::tr("detail/favorite_added")
                 : newpipe::tr("detail/favorite_removed"));
}
