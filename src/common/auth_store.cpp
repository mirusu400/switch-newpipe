#include "newpipe/auth_store.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"
#include "newpipe/log.hpp"

#ifdef __SWITCH__
#include <mbedtls/sha1.h>
#else
#include <openssl/sha.h>
#endif

namespace newpipe {
namespace {

using nlohmann::json;

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool read_text_file(const std::string& path, std::string& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) {
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool write_text_file(const std::string& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.good()) {
        return false;
    }

    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

std::unordered_map<std::string, std::string> parse_cookie_header(const std::string& raw_header) {
    std::unordered_map<std::string, std::string> cookies;
    std::stringstream stream(raw_header);
    std::string token;
    while (std::getline(stream, token, ';')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }

        const size_t sep = token.find('=');
        if (sep == std::string::npos || sep == 0) {
            continue;
        }

        const std::string name = trim(token.substr(0, sep));
        const std::string value = trim(token.substr(sep + 1));
        if (!name.empty() && !value.empty()) {
            cookies[name] = value;
        }
    }

    return cookies;
}

std::string build_cookie_header_from_map(
    const std::unordered_map<std::string, std::string>& cookies,
    const std::vector<std::string>& order) {
    std::string header;
    bool first = true;
    for (const auto& name : order) {
        const auto it = cookies.find(name);
        if (it == cookies.end() || it->second.empty()) {
            continue;
        }

        if (!first) {
            header += "; ";
        }
        header += name + "=" + it->second;
        first = false;
    }
    return header;
}

bool is_google_cookie_domain(const std::string& domain) {
    return domain.find("youtube.com") != std::string::npos
        || domain.find("google.com") != std::string::npos
        || domain.find("googleusercontent.com") != std::string::npos;
}

std::optional<std::string> parse_netscape_cookie_file(const std::string& raw) {
    std::unordered_map<std::string, std::string> cookies;
    std::vector<std::string> order;

    std::stringstream stream(raw);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::vector<std::string> fields;
        std::stringstream line_stream(line);
        std::string field;
        while (std::getline(line_stream, field, '\t')) {
            fields.push_back(field);
        }

        if (fields.size() < 7) {
            continue;
        }

        const std::string& domain = fields[0];
        const std::string& name = fields[5];
        const std::string& value = fields[6];
        if (!is_google_cookie_domain(domain) || name.empty() || value.empty()) {
            continue;
        }

        if (cookies.find(name) == cookies.end()) {
            order.push_back(name);
        }
        cookies[name] = value;
    }

    const std::string header = build_cookie_header_from_map(cookies, order);
    if (header.empty()) {
        return std::nullopt;
    }

    return header;
}

std::optional<std::string> parse_cookie_array(const json& cookies_node) {
    if (!cookies_node.is_array()) {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> cookies;
    std::vector<std::string> order;
    for (const auto& node : cookies_node) {
        if (!node.is_object()) {
            continue;
        }

        const auto name_it = node.find("name");
        const auto value_it = node.find("value");
        if (name_it == node.end() || value_it == node.end()
            || !name_it->is_string() || !value_it->is_string()) {
            continue;
        }

        const std::string name = name_it->get<std::string>();
        const std::string value = value_it->get<std::string>();
        if (name.empty() || value.empty()) {
            continue;
        }

        if (cookies.find(name) == cookies.end()) {
            order.push_back(name);
        }
        cookies[name] = value;
    }

    const std::string header = build_cookie_header_from_map(cookies, order);
    if (header.empty()) {
        return std::nullopt;
    }

    return header;
}

std::optional<AuthSession> parse_auth_source(
    const std::string& raw,
    const std::string& source_label,
    const std::string& source_path,
    std::string* error_message) {
    std::string cookie_header;
    std::string display_name;
    const std::string trimmed = trim(raw);
    if (trimmed.empty()) {
        if (error_message) {
            *error_message = "인증 파일이 비어 있습니다";
        }
        return std::nullopt;
    }

    if (trimmed[0] == '{' || trimmed[0] == '[') {
        const json root = json::parse(trimmed, nullptr, false);
        if (!root.is_discarded() && root.is_object()) {
            if (root.contains("cookie_header") && root.at("cookie_header").is_string()) {
                cookie_header = root.at("cookie_header").get<std::string>();
            } else if (root.contains("cookie") && root.at("cookie").is_string()) {
                cookie_header = root.at("cookie").get<std::string>();
            } else if (root.contains("cookies")) {
                const auto header = parse_cookie_array(root.at("cookies"));
                if (header.has_value()) {
                    cookie_header = *header;
                }
            }

            if (root.contains("display_name") && root.at("display_name").is_string()) {
                display_name = root.at("display_name").get<std::string>();
            } else if (root.contains("account_name") && root.at("account_name").is_string()) {
                display_name = root.at("account_name").get<std::string>();
            }
        }
    }

    if (cookie_header.empty()) {
        const auto netscape_header = parse_netscape_cookie_file(trimmed);
        if (netscape_header.has_value()) {
            cookie_header = *netscape_header;
        }
    }

    if (cookie_header.empty()) {
        cookie_header = trimmed;
        if (cookie_header.rfind("Cookie:", 0) == 0) {
            cookie_header = trim(cookie_header.substr(7));
        }
    }

    const auto cookies = parse_cookie_header(cookie_header);
    std::string sapisid;
    for (const char* name : {"SAPISID", "__Secure-3PAPISID", "APISID", "__Secure-1PAPISID"}) {
        const auto it = cookies.find(name);
        if (it != cookies.end() && !it->second.empty()) {
            sapisid = it->second;
            break;
        }
    }

    if (cookie_header.empty() || sapisid.empty()) {
        if (error_message) {
            *error_message = "쿠키에서 SAPISID 계열 값을 찾지 못했습니다";
        }
        return std::nullopt;
    }

    AuthSession session;
    session.cookie_header = cookie_header;
    session.sapisid = sapisid;
    session.source_label = source_label;
    session.source_path = source_path;
    session.display_name = display_name;
    return session;
}

std::string sha1_hex(const std::string& input) {
    std::array<unsigned char, 20> digest{};
#ifdef __SWITCH__
    mbedtls_sha1_ret(
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size(),
        digest.data());
#else
    SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest.data());
#endif

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        stream << std::setw(2) << static_cast<int>(byte);
    }
    return stream.str();
}

std::string build_sapisid_hash(const std::string& sapisid, const std::string& origin) {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const std::string timestamp = std::to_string(seconds);
    const std::string input = timestamp + " " + sapisid + " " + origin;
    return "SAPISIDHASH " + timestamp + "_" + sha1_hex(input);
}

}  // namespace

std::string default_auth_import_path() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe_auth.txt";
#else
    return "switch_newpipe_auth.txt";
#endif
}

std::string default_auth_session_path() {
#ifdef __SWITCH__
    return "sdmc:/switch/switch_newpipe_session.json";
#else
    return "switch_newpipe_session.json";
#endif
}

AuthStore& AuthStore::instance() {
    static AuthStore store;
    return store;
}

bool AuthStore::load(std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    if (this->loaded_) {
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    std::string raw;
    if (!read_text_file(default_auth_session_path(), raw)) {
        this->loaded_ = true;
        this->session_ = {};
        if (error_message) {
            error_message->clear();
        }
        return true;
    }

    std::string parse_error;
    const auto session = parse_auth_source(raw, "saved session", default_auth_session_path(), &parse_error);
    if (!session.has_value()) {
        if (error_message) {
            *error_message = parse_error.empty() ? "저장된 세션 파일 파싱 실패" : parse_error;
        }
        return false;
    }

    this->session_ = *session;
    this->loaded_ = true;
    if (error_message) {
        error_message->clear();
    }
    logf("auth: loaded session source=%s", this->session_.source_label.c_str());
    return true;
}

bool AuthStore::reload(std::string* error_message) {
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->loaded_ = false;
    this->session_ = {};
    if (error_message) {
        error_message->clear();
    }
    return true;
}

AuthSession AuthStore::session() const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->session_;
}

bool AuthStore::has_session() const {
    std::lock_guard<std::mutex> lock(this->mutex_);
    return this->session_.authenticated();
}

bool AuthStore::update_from_cookie_header(
    const std::string& cookie_header,
    const std::string& source_label,
    std::string* error_message) {
    std::string parse_error;
    const auto session = parse_auth_source(cookie_header, source_label, {}, &parse_error);
    if (!session.has_value()) {
        if (error_message) {
            *error_message = parse_error.empty() ? "쿠키 파싱 실패" : parse_error;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->session_ = *session;
        this->loaded_ = true;
    }

    if (!this->persist_session(error_message)) {
        return false;
    }

    logf("auth: updated session source=%s", source_label.c_str());
    if (error_message) {
        error_message->clear();
    }
    return true;
}

bool AuthStore::import_from_file(const std::string& file_path, std::string* error_message) {
    const std::string resolved_path = file_path.empty() ? default_auth_import_path() : file_path;

    std::string raw;
    if (!read_text_file(resolved_path, raw)) {
        if (error_message) {
            *error_message = "인증 파일을 열 수 없습니다: " + resolved_path;
        }
        return false;
    }

    std::string parse_error;
    const auto session = parse_auth_source(raw, "imported file", resolved_path, &parse_error);
    if (!session.has_value()) {
        if (error_message) {
            *error_message = parse_error.empty() ? "인증 파일 파싱 실패" : parse_error;
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->session_ = *session;
        this->loaded_ = true;
    }

    if (!this->persist_session(error_message)) {
        return false;
    }

    logf("auth: imported session path=%s", resolved_path.c_str());
    if (error_message) {
        error_message->clear();
    }
    return true;
}

bool AuthStore::clear(std::string* error_message) {
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->session_ = {};
        this->loaded_ = true;
    }

    const int rc = std::remove(default_auth_session_path().c_str());
    if (rc != 0) {
        std::ifstream existing(default_auth_session_path());
        if (existing.good()) {
            if (error_message) {
                *error_message = "세션 파일 삭제 실패";
            }
            return false;
        }
    }

    if (error_message) {
        error_message->clear();
    }
    log_line("auth: cleared session");
    return true;
}

std::vector<HttpHeader> AuthStore::build_youtube_headers(
    const std::string& origin,
    const std::string& referer,
    std::string* error_message) const {
    AuthSession session_copy;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        session_copy = this->session_;
    }

    if (!session_copy.authenticated()) {
        if (error_message) {
            *error_message = "로그인 세션이 없습니다";
        }
        return {};
    }

    if (error_message) {
        error_message->clear();
    }

    return {
        {"Cookie", session_copy.cookie_header},
        {"Authorization", build_sapisid_hash(session_copy.sapisid, origin)},
        {"Origin", origin},
        {"X-Origin", origin},
        {"Referer", referer},
        {"X-Goog-AuthUser", "0"},
        {"X-Youtube-Bootstrap-Logged-In", "true"},
    };
}

bool AuthStore::persist_session(std::string* error_message) {
    AuthSession session_copy;
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        session_copy = this->session_;
    }

    json root = {
        {"cookie_header", session_copy.cookie_header},
        {"source_label", session_copy.source_label},
        {"source_path", session_copy.source_path},
        {"display_name", session_copy.display_name},
    };

    if (!write_text_file(default_auth_session_path(), root.dump(2))) {
        if (error_message) {
            *error_message = "세션 파일 저장 실패";
        }
        return false;
    }

    if (error_message) {
        error_message->clear();
    }
    return true;
}

}  // namespace newpipe
