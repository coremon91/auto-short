#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace xdcam {

inline std::string TrimRight(std::string text) {
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

inline std::string Join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out.append(separator);
        }
        out += parts[i];
    }
    return out;
}

inline const char* BoolText(bool value) { return value ? "true" : "false"; }

}  // namespace xdcam
