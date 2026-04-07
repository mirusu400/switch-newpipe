#include "newpipe/http_client.hpp"

#include <optional>
#include <regex>
#include <string>

#include "newpipe/log.hpp"
#ifdef __SWITCH__
#include <curl/curl.h>
#else
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib/httplib.h"
#endif

namespace newpipe {
namespace {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path_and_query;
    int port = 443;
};

std::optional<ParsedUrl> parse_url(const std::string& url) {
    static const std::regex kPattern(R"(^(https?)://([^/:?#]+)(?::(\d+))?([^#]*)$)");
    std::smatch match;
    if (!std::regex_match(url, match, kPattern)) {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.scheme = match[1].str();
    parsed.host = match[2].str();
    parsed.port = match[3].matched ? std::stoi(match[3].str())
                                   : (parsed.scheme == "https" ? 443 : 80);
    parsed.path_and_query = match[4].str().empty() ? "/" : match[4].str();
    return parsed;
}

#ifdef __SWITCH__
size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total_size = size * nmemb;
    auto* output = static_cast<std::string*>(userp);
    output->append(static_cast<const char*>(contents), total_size);
    return total_size;
}
#endif

#ifndef __SWITCH__
httplib::Headers to_headers(const std::vector<HttpHeader>& headers) {
    httplib::Headers out;
    for (const auto& header : headers) {
        out.insert({header.name, header.value});
    }
    return out;
}
#endif

}  // namespace

std::optional<std::string> HttpsHttpClient::get(
    const std::string& url,
    const std::vector<HttpHeader>& headers) {
#ifdef __SWITCH__
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_line("http: curl_easy_init failed");
        return std::nullopt;
    }

    std::string response_body;
    struct curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        const std::string line = header.name + ": " + header.value;
        header_list = curl_slist_append(header_list, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Nintendo Switch; Switch-NewPipe)");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    const CURLcode result = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || status_code < 200 || status_code >= 300) {
        logf("http: GET failed url=%s curl=%d status=%ld", url.c_str(), static_cast<int>(result), status_code);
        return std::nullopt;
    }

    logf("http: GET ok url=%s bytes=%zu", url.c_str(), response_body.size());
    return response_body;
#else
    const auto parsed = parse_url(url);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    const auto request_headers = to_headers(headers);
    if (parsed->scheme == "https") {
        httplib::SSLClient client(parsed->host, parsed->port);
        client.set_follow_location(true);
        client.set_connection_timeout(5);
        client.set_read_timeout(5);
        client.enable_server_certificate_verification(true);

        const auto response = client.Get(parsed->path_and_query.c_str(), request_headers);
        if (!response || response->status < 200 || response->status >= 300) {
            return std::nullopt;
        }
        return response->body;
    }

    httplib::Client client(parsed->host, parsed->port);
    client.set_follow_location(true);
    client.set_connection_timeout(5);
    client.set_read_timeout(5);

    const auto response = client.Get(parsed->path_and_query.c_str(), request_headers);
    if (!response || response->status < 200 || response->status >= 300) {
        return std::nullopt;
    }
    return response->body;
#endif
}

std::optional<std::string> HttpsHttpClient::post(
    const std::string& url,
    const std::string& body,
    const std::vector<HttpHeader>& headers) {
#ifdef __SWITCH__
    CURL* curl = curl_easy_init();
    if (!curl) {
        log_line("http: curl_easy_init failed");
        return std::nullopt;
    }

    std::string response_body;
    struct curl_slist* header_list = nullptr;
    for (const auto& header : headers) {
        const std::string line = header.name + ": " + header.value;
        header_list = curl_slist_append(header_list, line.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Nintendo Switch; Switch-NewPipe)");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    const CURLcode result = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || status_code < 200 || status_code >= 300) {
        logf("http: POST failed url=%s curl=%d status=%ld", url.c_str(), static_cast<int>(result), status_code);
        return std::nullopt;
    }

    logf("http: POST ok url=%s bytes=%zu", url.c_str(), response_body.size());
    return response_body;
#else
    const auto parsed = parse_url(url);
    if (!parsed.has_value()) {
        return std::nullopt;
    }

    const auto request_headers = to_headers(headers);
    if (parsed->scheme == "https") {
        httplib::SSLClient client(parsed->host, parsed->port);
        client.set_follow_location(true);
        client.set_connection_timeout(5);
        client.set_read_timeout(30);
        client.enable_server_certificate_verification(true);

        const auto response = client.Post(parsed->path_and_query.c_str(), request_headers, body, "application/json");
        if (!response || response->status < 200 || response->status >= 300) {
            return std::nullopt;
        }
        return response->body;
    }

    httplib::Client client(parsed->host, parsed->port);
    client.set_follow_location(true);
    client.set_connection_timeout(5);
    client.set_read_timeout(30);

    const auto response = client.Post(parsed->path_and_query.c_str(), request_headers, body, "application/json");
    if (!response || response->status < 200 || response->status >= 300) {
        return std::nullopt;
    }
    return response->body;
#endif
}

}  // namespace newpipe
