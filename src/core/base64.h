// base64.h -- Happy Enginiring
// Standard Base64 encoder (RFC 4648, with '=' padding). Used to embed
// attached images in the JSON request bodies of the vision-capable AI APIs.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace heng {

inline std::string base64Encode(const uint8_t* data, size_t len) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                      static_cast<uint32_t>(data[i + 2]);
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(tbl[(v >> 6) & 0x3F]);
        out.push_back(tbl[v & 0x3F]);
        i += 3;
    }
    size_t rest = len - i;
    if (rest == 1) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out += "==";
    } else if (rest == 2) {
        uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 0x3F]);
        out.push_back(tbl[(v >> 12) & 0x3F]);
        out.push_back(tbl[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

inline std::string base64Encode(const std::vector<uint8_t>& v) {
    return base64Encode(v.empty() ? nullptr : v.data(), v.size());
}

} // namespace heng
