#include "newpipe/image_loader.hpp"

#include <chrono>

#include "newpipe/http_client.hpp"
#include "newpipe/log.hpp"

namespace newpipe {

ImageLoader& ImageLoader::instance() {
    static ImageLoader loader;
    return loader;
}

ImageLoader::~ImageLoader() {
    stop();
}

void ImageLoader::start() {
    if (running_) {
        return;
    }

    running_ = true;
    log_line("image: start worker");
    thread_ = std::thread([this]() { worker(); });
}

void ImageLoader::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    log_line("image: stop worker");
}

void ImageLoader::load(const std::string& url, brls::Image* target) {
    if (!target) {
        return;
    }

    logf("image: queue %s", url.c_str());
    target->setImageAsync([url, this](std::function<void(const std::string&, size_t)> cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push({url, cb});
    });
}

void ImageLoader::worker() {
    HttpsHttpClient client;
    log_line("image: worker entered");

    while (running_) {
        AsyncRequest request;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!queue_.empty()) {
                request = std::move(queue_.front());
                queue_.pop();
            }
        }

        if (request.url.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        auto data = client.get(request.url);
        if (data.has_value() && !data->empty()) {
            logf("image: fetched %s bytes=%zu", request.url.c_str(), data->size());
            request.callback(*data, data->size());
        } else {
            logf("image: fetch failed %s", request.url.c_str());
            request.callback("", 0);
        }
    }

    log_line("image: worker exit");
}

}  // namespace newpipe
