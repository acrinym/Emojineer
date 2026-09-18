#include "emojineer/browser_runtime.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/version.hpp"

#include <emscripten/emscripten.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

std::string last_response;

std::string json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char escaped[7]{};
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
                    out += escaped;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* emojineer_browser_version() {
    last_response = emojineer::version;
    return last_response.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* emojineer_browser_run(const char* source) {
    const auto result = emojineer::run_browser_source(source ? source : "");
    last_response = "{\"ok\":";
    last_response += result.ok ? "true" : "false";
    last_response += ",\"stdout\":\"" + json_escape(result.stdout_text) + "\"";
    last_response += ",\"diagnostic\":\"" + json_escape(result.diagnostic) + "\"";
    last_response += ",\"requiredCapabilities\":\"";
    last_response += json_escape(emojineer::capability_mask_string(result.required_capabilities));
    last_response += "\"}";
    return last_response.c_str();
}

} // extern "C"
