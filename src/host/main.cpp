#include <iostream>
#include <string>

#include "newpipe/auth_store.hpp"
#include "newpipe/youtube_catalog_service.hpp"
#include "newpipe/youtube_resolver.hpp"

int main(int argc, char* argv[]) {
    newpipe::YouTubeCatalogService service;
    std::string auth_file;
    std::string search_query;
    std::string resolve_url;
    std::string related_url;
    std::string channel_url;
    std::string playlist_url;
    std::string comments_url;
    bool subscriptions_mode = false;
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--auth-file" && i + 1 < argc) {
            auth_file = argv[++i];
        } else if (arg == "--subscriptions") {
            subscriptions_mode = true;
        } else if (arg == "--search" && i + 1 < argc) {
            search_query = argv[++i];
        } else if (arg == "--resolve" && i + 1 < argc) {
            resolve_url = argv[++i];
        } else if (arg == "--related" && i + 1 < argc) {
            related_url = argv[++i];
        } else if (arg == "--channel" && i + 1 < argc) {
            channel_url = argv[++i];
        } else if (arg == "--playlist" && i + 1 < argc) {
            playlist_url = argv[++i];
        } else if (arg == "--comments" && i + 1 < argc) {
            comments_url = argv[++i];
        }
    }

    if (!service.is_loaded()) {
        std::cerr << "service init failed: " << service.error_message() << '\n';
        return 1;
    }

    if (subscriptions_mode) {
        std::string error;
        if (!auth_file.empty()) {
            if (!service.import_auth_session_from_file(auth_file, &error)) {
                std::cerr << "auth import failed: " << error << '\n';
                return 1;
            }
        } else {
            service.load_auth_session(&error);
        }

        const auto feed = service.get_subscriptions_feed();
        if (!feed.has_value()) {
            std::cerr << "subscriptions failed: " << service.error_message() << '\n';
            return 1;
        }

        std::cout << "subscriptions: " << feed->items.size() << '\n';
        for (const auto& item : feed->items) {
            std::cout << "- " << item.title << " | " << item.channel_name << '\n';
        }
        return 0;
    }

    if (!search_query.empty()) {
        const auto results = service.search(search_query);
        std::cout << "search: " << results.query << '\n';
        for (const auto& item : results.items) {
            std::cout << "- " << item.title << " | " << item.channel_name << '\n';
        }
        return 0;
    }

    if (!related_url.empty()) {
        newpipe::StreamItem item;
        item.url = related_url;
        const auto feed = service.get_related_feed(item);
        if (!feed.has_value()) {
            std::cerr << "related failed: " << service.error_message() << '\n';
            return 1;
        }

        std::cout << "related: " << feed->items.size() << '\n';
        for (const auto& entry : feed->items) {
            std::cout << "- " << entry.title << " | " << entry.channel_name << '\n';
        }
        return 0;
    }

    if (!channel_url.empty()) {
        newpipe::StreamItem item;
        item.url = channel_url;
        const auto detail = service.get_stream_detail(channel_url);
        if (detail.has_value()) {
            item = detail->item;
        }

        const auto feed = service.get_channel_feed(item);
        if (!feed.has_value()) {
            std::cerr << "channel failed: " << service.error_message() << '\n';
            return 1;
        }

        std::cout << "channel: " << feed->kiosk.title << " (" << feed->items.size() << ")\n";
        for (const auto& entry : feed->items) {
            std::cout << "- " << entry.title << " | " << entry.published_text << '\n';
        }
        return 0;
    }

    if (!playlist_url.empty()) {
        newpipe::StreamItem item;
        item.url = playlist_url;
        const auto feed = service.get_playlist_feed(item);
        if (!feed.has_value()) {
            std::cerr << "playlist failed: " << service.error_message() << '\n';
            return 1;
        }

        std::cout << "playlist: " << feed->kiosk.title << " (" << feed->items.size() << ")\n";
        for (const auto& entry : feed->items) {
            std::cout << "- " << entry.title << " | " << entry.channel_name << '\n';
        }
        return 0;
    }

    if (!comments_url.empty()) {
        newpipe::StreamItem item;
        item.url = comments_url;
        const auto page = service.get_comments(item);
        if (!page.has_value()) {
            std::cerr << "comments failed: " << service.error_message() << '\n';
            return 1;
        }

        std::cout << "comments: " << page->title << " (" << page->items.size() << ")\n";
        for (const auto& entry : page->items) {
            std::cout << "- " << entry.author_name << " | " << entry.body << '\n';
        }
        return 0;
    }

    if (!resolve_url.empty()) {
        newpipe::YouTubeResolver resolver;
        std::string error;
        const auto resolved = resolver.resolve(
            resolve_url,
            error,
            [](const std::string& title, const std::string& detail) {
                std::cout << title << " :: " << detail << '\n';
            });
        if (!resolved.has_value()) {
            std::cerr << "resolve failed: " << error << '\n';
            return 1;
        }

        std::cout << "stream: " << resolved->stream_url << '\n';
        if (!resolved->external_audio_url.empty()) {
            std::cout << "audio: " << resolved->external_audio_url << '\n';
        }
        std::cout << "referer: " << resolved->referer << '\n';
        std::cout << "quality: " << resolved->quality_label << '\n';
        std::cout << "live: " << (resolved->is_live ? "yes" : "no") << '\n';
        return 0;
    }

    const auto kiosks = service.list_kiosks();
    std::cout << "kiosks: " << kiosks.size() << '\n';
    for (const auto& kiosk : kiosks) {
        const auto feed = service.get_home_feed(kiosk.id);
        std::cout << "* " << kiosk.id;
        if (feed.has_value()) {
            std::cout << " (" << feed->items.size() << " items)";
        }
        std::cout << '\n';
    }

    if (!kiosks.empty()) {
        const auto feed = service.get_home_feed(kiosks.front().id);
        if (feed.has_value() && !feed->items.empty()) {
            const auto detail = service.get_stream_detail(feed->items.front().url);
            if (detail.has_value()) {
                std::cout << "sample: " << detail->item.title << '\n';
            }
        }
    }

    return 0;
}
