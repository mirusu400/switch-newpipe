#include "newpipe/throttling_decrypter.hpp"

#include <regex>
#include <string>

extern "C" {
#include <quickjs.h>
}

#include "newpipe/log.hpp"

namespace newpipe {
namespace {

std::optional<std::string> find_query_value(const std::string& url, const std::string& key) {
    const std::string pattern = key + "=";
    size_t search_from = 0;
    while (true) {
        const size_t pos = url.find(pattern, search_from);
        if (pos == std::string::npos) {
            return std::nullopt;
        }
        if (pos == 0 || url[pos - 1] == '?' || url[pos - 1] == '&') {
            const size_t value_start = pos + pattern.size();
            const size_t value_end = url.find_first_of("&#", value_start);
            return url.substr(value_start,
                value_end == std::string::npos ? std::string::npos : value_end - value_start);
        }
        search_from = pos + 1;
    }
}

std::string replace_query_value(const std::string& url, const std::string& key, const std::string& new_value) {
    const std::string pattern = key + "=";
    size_t search_from = 0;
    while (true) {
        const size_t pos = url.find(pattern, search_from);
        if (pos == std::string::npos) {
            return url;
        }
        if (pos == 0 || url[pos - 1] == '?' || url[pos - 1] == '&') {
            const size_t value_start = pos + pattern.size();
            const size_t value_end = url.find_first_of("&#", value_start);
            const size_t len = value_end == std::string::npos
                ? std::string::npos
                : value_end - value_start;
            std::string result = url;
            result.replace(value_start, len, new_value);
            return result;
        }
        search_from = pos + 1;
    }
}

}  // namespace

ThrottlingDecrypter::ThrottlingDecrypter(HttpClient* client)
    : client_(client ? client : &owned_client_) {
}

std::string ThrottlingDecrypter::apply(const std::string& url) {
    const auto n_value = find_query_value(url, "n");
    if (!n_value.has_value() || n_value->empty()) {
        return url;
    }

    const auto video_id = find_query_value(url, "id");
    const std::string vid = video_id.value_or("");

    if (!ensure_function_loaded(vid)) {
        logf("throttle: failed to load n transform function for video=%s", vid.c_str());
        return url;
    }

    const auto transformed = transform_n(*n_value);
    if (!transformed.has_value() || transformed->empty()) {
        logf("throttle: n transform failed for n=%s", n_value->c_str());
        return url;
    }

    if (*transformed == *n_value) {
        return url;
    }

    logf("throttle: n transformed %s -> %s", n_value->c_str(), transformed->c_str());
    return replace_query_value(url, "n", *transformed);
}

void ThrottlingDecrypter::invalidate() {
    cached_player_js_url_.clear();
    cached_function_body_.clear();
}

bool ThrottlingDecrypter::ensure_function_loaded(const std::string& video_id) {
    if (!cached_function_body_.empty()) {
        return true;
    }

    const auto player_js_url = fetch_player_js_url(video_id);
    if (!player_js_url.has_value()) {
        log_line("throttle: failed to get player JS URL");
        return false;
    }

    if (*player_js_url == cached_player_js_url_ && !cached_function_body_.empty()) {
        return true;
    }

    const auto player_js = fetch_player_js(*player_js_url);
    if (!player_js.has_value()) {
        log_line("throttle: failed to fetch player JS");
        return false;
    }

    const auto func_body = extract_throttle_function(*player_js);
    if (!func_body.has_value()) {
        log_line("throttle: failed to extract throttle function from player JS");
        return false;
    }

    cached_player_js_url_ = *player_js_url;
    cached_function_body_ = *func_body;
    logf("throttle: extracted n transform function (%zu bytes) from %s",
         cached_function_body_.size(), player_js_url->c_str());
    return true;
}

std::optional<std::string> ThrottlingDecrypter::transform_n(const std::string& n_value) {
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) {
        return std::nullopt;
    }

    JS_SetMemoryLimit(rt, 64 * 1024 * 1024);
    JS_SetMaxStackSize(rt, 1024 * 1024);

    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        return std::nullopt;
    }

    const std::string script =
        "(" + cached_function_body_ + ")(\"" + n_value + "\")";

    JSValue result = JS_Eval(ctx, script.c_str(), script.size(),
                             "<throttle>", JS_EVAL_TYPE_GLOBAL);

    std::optional<std::string> output;
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char* err_str = JS_ToCString(ctx, exception);
        if (err_str) {
            logf("throttle: JS exception: %s", err_str);
            JS_FreeCString(ctx, err_str);
        }
        JS_FreeValue(ctx, exception);
    } else {
        const char* str = JS_ToCString(ctx, result);
        if (str) {
            output = std::string(str);
            JS_FreeCString(ctx, str);
        }
    }

    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return output;
}

std::optional<std::string> ThrottlingDecrypter::fetch_player_js_url(const std::string& video_id) {
    const std::string embed_url = "https://www.youtube.com/embed/" + video_id;
    const auto page = client_->get(embed_url, {
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"},
    });
    if (!page.has_value()) {
        log_line("throttle: failed to fetch embed page");
        return std::nullopt;
    }

    static const std::regex kPlayerJsPattern(R"xxx("jsUrl"\s*:\s*"(/s/player/[^"]+/base\.js)")xxx");
    std::smatch match;
    if (!std::regex_search(*page, match, kPlayerJsPattern)) {
        static const std::regex kPlayerJsPattern2(R"xxx(src="(/s/player/[^"]+/base\.js)")xxx");
        if (!std::regex_search(*page, match, kPlayerJsPattern2)) {
            log_line("throttle: player JS URL not found in embed page");
            return std::nullopt;
        }
    }

    return "https://www.youtube.com" + match[1].str();
}

std::optional<std::string> ThrottlingDecrypter::fetch_player_js(const std::string& player_js_url) {
    return client_->get(player_js_url, {
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"},
    });
}

std::optional<std::string> ThrottlingDecrypter::extract_throttle_function(const std::string& player_js) {
    // Pattern 1: yt-dlp / NewPipe style - function with enhanced_except wrapper
    // Matches: var func=function(a){...};
    // The n-transform function takes a single argument, converts to array,
    // performs operations, and joins back.
    //
    // We look for the function that:
    // 1. Takes a single parameter
    // 2. Splits it into a character array
    // 3. Performs array manipulations
    // 4. Joins and returns

    // Pattern: function(a){var b=a.split(""),c=[...operations...];return b.join("")}
    // Modern pattern from yt-dlp (2024-2026):
    // Look for: function(a){var b=a.split("") followed by array operations and b.join("")

    // Strategy: find the function name first, then extract the full body
    // NewPipe pattern: searches for the assignment of the n-transform function

    // Pattern A: Enhanced format (2024+)
    // &&(b=a.get("n"))&&(b=FUNCNAME(b),a.set("n",b))
    {
        static const std::regex kCallerPattern(
            R"(&&\(b=a\.get\("n"\)\)&&\(b=([a-zA-Z0-9$_]+)(?:\[(\d+)\])?\(b\))");
        std::smatch caller_match;
        if (std::regex_search(player_js, caller_match, kCallerPattern)) {
            std::string func_name = caller_match[1].str();

            if (caller_match[2].matched) {
                // It's an array reference: funcArray[index](b)
                // Find: var funcArray=[funcA,funcB,...];
                const std::string array_pattern_str =
                    "var " + std::regex_replace(func_name, std::regex(R"(\$)"), R"(\$)") +
                    R"(\s*=\s*\[([a-zA-Z0-9$_]+(?:\s*,\s*[a-zA-Z0-9$_]+)*)\])";
                std::regex array_pattern(array_pattern_str);
                std::smatch array_match;
                if (std::regex_search(player_js, array_match, array_pattern)) {
                    const int index = std::stoi(caller_match[2].str());
                    std::string elements = array_match[1].str();
                    // Split by comma and get the nth element
                    int current = 0;
                    size_t pos = 0;
                    while (current < index) {
                        pos = elements.find(',', pos);
                        if (pos == std::string::npos) break;
                        ++pos;
                        ++current;
                    }
                    if (current == index) {
                        size_t end = elements.find(',', pos);
                        func_name = elements.substr(pos,
                            end == std::string::npos ? std::string::npos : end - pos);
                        // Trim whitespace
                        func_name.erase(0, func_name.find_first_not_of(" \t"));
                        func_name.erase(func_name.find_last_not_of(" \t") + 1);
                    }
                }
            }

            logf("throttle: found n-transform function name: %s", func_name.c_str());

            // Now extract the function body
            // Pattern: FUNCNAME=function(a){...}
            const std::string escaped_name = std::regex_replace(func_name, std::regex(R"(\$)"), R"(\$)");
            const std::string body_pattern_str =
                escaped_name + R"(\s*=\s*function\s*\([a-zA-Z0-9_$]\))";
            std::regex body_start_pattern(body_pattern_str);
            std::smatch body_start_match;
            if (std::regex_search(player_js, body_start_match, body_start_pattern)) {
                const size_t func_keyword = player_js.find("function", body_start_match.position());
                if (func_keyword != std::string::npos) {
                    // Find the opening brace
                    const size_t open_brace = player_js.find('{', func_keyword);
                    if (open_brace != std::string::npos) {
                        // Count braces to find matching close
                        int depth = 1;
                        size_t pos = open_brace + 1;
                        while (pos < player_js.size() && depth > 0) {
                            if (player_js[pos] == '{') ++depth;
                            else if (player_js[pos] == '}') --depth;
                            ++pos;
                        }
                        if (depth == 0) {
                            const size_t param_start = player_js.find('(', func_keyword);
                            const std::string full_func = player_js.substr(
                                func_keyword, pos - func_keyword);
                            logf("throttle: extracted function body (%zu bytes)", full_func.size());
                            return full_func;
                        }
                    }
                }
            }
        }
    }

    // Pattern B: Direct function pattern (older format)
    // function FUNCNAME(a){var b=a.split("")...b.join("")}
    {
        static const std::regex kDirectPattern(
            R"(\b([a-zA-Z0-9$_]+)\s*=\s*function\s*\(\s*[a-zA-Z]\s*\)\s*\{)"
            R"(\s*var\s+[a-zA-Z]\s*=\s*[a-zA-Z]\s*\.\s*split\s*\(\s*""\s*\))");
        std::smatch direct_match;
        std::string::const_iterator search_start = player_js.cbegin();
        while (std::regex_search(search_start, player_js.cend(), direct_match, kDirectPattern)) {
            const size_t func_pos = direct_match.position() +
                std::distance(player_js.cbegin(), search_start);
            const size_t func_keyword = player_js.find("function", func_pos);
            if (func_keyword != std::string::npos) {
                const size_t open_brace = player_js.find('{', func_keyword);
                if (open_brace != std::string::npos) {
                    int depth = 1;
                    size_t pos = open_brace + 1;
                    while (pos < player_js.size() && depth > 0) {
                        if (player_js[pos] == '{') ++depth;
                        else if (player_js[pos] == '}') --depth;
                        ++pos;
                    }
                    if (depth == 0) {
                        const std::string body = player_js.substr(func_keyword, pos - func_keyword);
                        // Verify it looks like an n-transform (has join(""))
                        if (body.find("join(\"\")") != std::string::npos
                            || body.find("join('')") != std::string::npos) {
                            logf("throttle: extracted via direct pattern (%zu bytes)", body.size());
                            return body;
                        }
                    }
                }
            }
            search_start = direct_match.suffix().first;
        }
    }

    return std::nullopt;
}

}  // namespace newpipe
