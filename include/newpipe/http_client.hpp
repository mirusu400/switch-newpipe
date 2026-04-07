#pragma once

#include <optional>
#include <string>
#include <vector>

namespace newpipe {

struct HttpHeader {
    std::string name;
    std::string value;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;

    virtual std::optional<std::string> get(
        const std::string& url,
        const std::vector<HttpHeader>& headers = {}) = 0;

    virtual std::optional<std::string> post(
        const std::string& url,
        const std::string& body,
        const std::vector<HttpHeader>& headers = {}) = 0;
};

class HttpsHttpClient final : public HttpClient {
public:
    std::optional<std::string> get(
        const std::string& url,
        const std::vector<HttpHeader>& headers = {}) override;

    std::optional<std::string> post(
        const std::string& url,
        const std::string& body,
        const std::vector<HttpHeader>& headers = {}) override;
};

}  // namespace newpipe
