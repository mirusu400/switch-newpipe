#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <borealis.hpp>

namespace newpipe {

class ImageLoader {
public:
    static ImageLoader& instance();

    void start();
    void stop();
    void load(const std::string& url, brls::Image* target);

private:
    struct AsyncRequest {
        std::string url;
        std::function<void(const std::string&, size_t)> callback;
    };

    ImageLoader() = default;
    ~ImageLoader();

    void worker();

    std::thread thread_;
    std::mutex mutex_;
    std::queue<AsyncRequest> queue_;
    std::atomic<bool> running_{false};
};

}  // namespace newpipe
