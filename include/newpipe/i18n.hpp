#pragma once

#include <borealis.hpp>
#include <string>
#include <utility>

namespace newpipe {

inline std::string locale_from_setting(const std::string& language) {
    if (language == "ko") {
        return brls::LOCALE_Ko;
    }
    if (language == "en-US") {
        return brls::LOCALE_EN_US;
    }
    return brls::LOCALE_AUTO;
}

template <typename... Args>
inline std::string tr(const std::string& key, Args&&... args) {
    return brls::getStr(key, std::forward<Args>(args)...);
}

}  // namespace newpipe
