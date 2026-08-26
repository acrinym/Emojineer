#include "emojineer/registry_transport.hpp"

#include "emojineer/registry.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(EMOJINEER_HAVE_CURL)
#include <curl/curl.h>
#endif

namespace emojineer {
namespace {

bool valid_portable_name(std::string_view value, bool allow_dot = false) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [allow_dot](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || (allow_dot && c == '.');
    });
}

bool valid_sha256(std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}

bool safe_opaque_text(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= 0x20 && c <= 0x7e;
    });
}

void validate_credential(const RegistryPublishCredential& credential) {
    if (credential.token.empty()) {
        throw std::runtime_error("publication credential token cannot be empty");
    }
    if (credential.token.size() > 8192) {
        throw std::runtime_error("publication credential token exceeds size limit");
    }
    for (unsigned char c : credential.token) {
        if (c <= 0x20 || c == 0x7f) {
            throw std::runtime_error(
                "publication credential contains unsupported whitespace or control characters");
        }
    }
    if (credential.token.find("://") != std::string::npos) {
        throw std::runtime_error("publication credential must not be supplied in URL form");
    }
    if (!valid_portable_name(credential.namespace_id)) {
        throw std::runtime_error(
            "publication namespace may contain only ASCII letters, digits, '-' and '_'");
    }
}

void validate_receipt_shape(const PublicationReceipt& receipt) {
    if (!valid_portable_name(receipt.registry_id, true)) {
        throw std::runtime_error("publication receipt has invalid registry identity");
    }
    if (!valid_portable_name(receipt.package_name)) {
        throw std::runtime_error("publication receipt has invalid package name");
    }
    (void)parse_semantic_version(receipt.version);
    if (!valid_sha256(receipt.content_sha256)) {
        throw std::runtime_error("publication receipt has malformed content SHA-256");
    }
    if (!valid_sha256(receipt.artifact_sha256)) {
        throw std::runtime_error("publication receipt has malformed artifact SHA-256");
    }
    if (receipt.protocol_version != publication_protocol::version) {
        throw std::runtime_error("publication receipt uses unsupported protocol version");
    }
    if (!safe_opaque_text(receipt.receipt_id, 256)) {
        throw std::runtime_error("publication receipt has invalid receipt id");
    }
    if (!safe_opaque_text(receipt.timestamp, 128)) {
        throw std::runtime_error("publication receipt has invalid timestamp");
    }
}

void append_utf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

class JsonStringObjectParser {
public:
    explicit JsonStringObjectParser(std::string_view text) : text_(text) {}

    std::map<std::string, std::string> parse() {
        skip_space();
        expect('{');
        skip_space();
        std::map<std::string, std::string> values;
        if (consume('}')) {
            skip_space();
            require_end();
            return values;
        }
        while (true) {
            const auto key = parse_string();
            skip_space();
            expect(':');
            skip_space();
            const auto value = parse_string();
            if (!values.emplace(key, value).second) {
                throw std::runtime_error("publication receipt contains duplicate JSON field");
            }
            skip_space();
            if (consume('}')) break;
            expect(',');
            skip_space();
        }
        skip_space();
        require_end();
        return values;
    }

private:
    std::string_view text_;
    std::size_t position_ = 0;

    void skip_space() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            throw std::runtime_error("publication receipt is malformed JSON");
        }
    }

    void require_end() const {
        if (position_ != text_.size()) {
            throw std::runtime_error("publication receipt contains trailing JSON data");
        }
    }

    static int hex_value(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return -1;
    }

    std::uint32_t parse_hex4() {
        if (position_ + 4 > text_.size()) {
            throw std::runtime_error("publication receipt has truncated Unicode escape");
        }
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_value(text_[position_++]);
            if (digit < 0) {
                throw std::runtime_error("publication receipt has invalid Unicode escape");
            }
            value = (value << 4) | static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (position_ < text_.size()) {
            const unsigned char raw = static_cast<unsigned char>(text_[position_++]);
            if (raw == '"') return out;
            if (raw < 0x20) {
                throw std::runtime_error(
                    "publication receipt JSON string contains a control character");
            }
            if (raw != '\\') {
                out.push_back(static_cast<char>(raw));
                continue;
            }
            if (position_ >= text_.size()) {
                throw std::runtime_error("publication receipt has truncated JSON escape");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = parse_hex4();
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                        if (position_ + 2 > text_.size() ||
                            text_[position_] != '\\' || text_[position_ + 1] != 'u') {
                            throw std::runtime_error(
                                "publication receipt has unpaired high surrogate");
                        }
                        position_ += 2;
                        const std::uint32_t low = parse_hex4();
                        if (low < 0xdc00 || low > 0xdfff) {
                            throw std::runtime_error(
                                "publication receipt has invalid surrogate pair");
                        }
                        codepoint = 0x10000 +
                                    ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                        throw std::runtime_error(
                            "publication receipt has unpaired low surrogate");
                    }
                    append_utf8(out, codepoint);
                    break;
                }
                default:
                    throw std::runtime_error("publication receipt has invalid JSON escape");
            }
        }
        throw std::runtime_error("publication receipt has unterminated JSON string");
    }
};

std::string json_escape(std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0x0f]);
                    out.push_back(hex[c & 0x0f]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string required_field(const std::map<std::string, std::string>& fields,
                           std::string_view name) {
    const auto found = fields.find(std::string(name));
    if (found == fields.end()) {
        throw std::runtime_error("publication receipt is missing required field '" +
                                 std::string(name) + "'");
    }
    return found->second;
}

std::string normalized_media_type(std::string_view raw) {
    const auto semicolon = raw.find(';');
    raw = raw.substr(0, semicolon);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    std::string result(raw);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

bool media_type_matches(std::string_view actual, std::string_view expected) {
    return normalized_media_type(actual) == normalized_media_type(expected);
}

void throw_for_publication_status(long status) {
    if (status >= 200 && status < 300) return;
    switch (status) {
        case 400:
            throw std::runtime_error("publication request rejected by registry (HTTP 400)");
        case 401:
            throw std::runtime_error("publication authentication failed (HTTP 401)");
        case 403:
            throw std::runtime_error(
                "publication namespace authorization failed (HTTP 403)");
        case 409:
            throw std::runtime_error("publication immutable version conflict (HTTP 409)");
        case 413:
            throw std::runtime_error(
                "publication upload exceeds registry request limit (HTTP 413)");
        default:
            throw std::runtime_error(
                "HTTPS publication failed with status " + std::to_string(status));
    }
}

void write_atomic_receipt(const std::filesystem::path& path, std::string_view data) {
    if (path.empty()) throw std::runtime_error("publication receipt path cannot be empty");
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<> dis(0, 15);

    std::filesystem::path temp;
    bool temp_created = false;

#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix;
        for (int i = 0; i < 16; ++i) suffix += "0123456789abcdef"[dis(gen)];
        temp = path;
        temp += ".tmp." + suffix;
        handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            temp_created = true;
            break;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) {
            throw std::runtime_error("cannot create publication receipt temporary file");
        }
    }
    if (!temp_created) {
        throw std::runtime_error(
            "cannot create publication receipt temporary file after repeated collisions");
    }

    std::size_t remaining = data.size();
    const char* pointer = data.data();
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (!WriteFile(handle, pointer, chunk, &written, nullptr) || written == 0) {
            CloseHandle(handle);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing publication receipt");
        }
        pointer += written;
        remaining -= written;
    }
    if (!FlushFileBuffers(handle)) {
        CloseHandle(handle);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("failed while flushing publication receipt");
    }
    CloseHandle(handle);

    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("cannot atomically commit publication receipt");
    }
#else
    int descriptor = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix;
        for (int i = 0; i < 16; ++i) suffix += "0123456789abcdef"[dis(gen)];
        temp = path;
        temp += ".tmp." + suffix;
        descriptor = open(temp.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (descriptor >= 0) {
            temp_created = true;
            break;
        }
        if (errno != EEXIST) {
            throw std::runtime_error("cannot create publication receipt temporary file: " +
                                     std::string(std::strerror(errno)));
        }
    }
    if (!temp_created) {
        throw std::runtime_error(
            "cannot create publication receipt temporary file after repeated collisions");
    }

    std::size_t remaining = data.size();
    const char* pointer = data.data();
    while (remaining > 0) {
        const ssize_t written = write(descriptor, pointer, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            const int saved = errno;
            close(descriptor);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing publication receipt: " +
                                     std::string(std::strerror(saved)));
        }
        if (written == 0) {
            close(descriptor);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing publication receipt: no progress");
        }
        pointer += written;
        remaining -= static_cast<std::size_t>(written);
    }
    if (fsync(descriptor) != 0) {
        const int saved = errno;
        close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("failed while flushing publication receipt: " +
                                 std::string(std::strerror(saved)));
    }
    close(descriptor);

    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("cannot atomically commit publication receipt: " +
                                 error.message());
    }
#endif
}

#if defined(EMOJINEER_HAVE_CURL)
struct CurlBuffer {
    std::string bytes;
    std::size_t limit = 0;
    bool exceeded = false;
};

std::size_t curl_write(char* data, std::size_t size, std::size_t count, void* opaque) {
    auto* buffer = static_cast<CurlBuffer*>(opaque);
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) return 0;
    const std::size_t amount = size * count;
    if (buffer->bytes.size() > buffer->limit ||
        amount > buffer->limit - buffer->bytes.size()) {
        buffer->exceeded = true;
        return 0;
    }
    buffer->bytes.append(data, amount);
    return amount;
}

struct CurlTiming {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> upload_finished;
    std::optional<std::chrono::steady_clock::time_point> first_response_header;
    curl_off_t expected_upload = 0;
    bool upload_timeout = false;
    bool header_timeout = false;
    bool response_body_timeout = false;
};

int curl_progress(void* opaque, curl_off_t, curl_off_t,
                  curl_off_t upload_total, curl_off_t upload_now) {
    auto* timing = static_cast<CurlTiming*>(opaque);
    const auto now = std::chrono::steady_clock::now();
    const auto seconds = [](auto duration) {
        return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    };

    const curl_off_t expected = timing->expected_upload > 0
        ? timing->expected_upload
        : upload_total;
    if (!timing->upload_finished && expected > 0 && upload_now >= expected) {
        timing->upload_finished = now;
    }
    if (!timing->upload_finished &&
        seconds(now - timing->start) > publication_protocol::upload_timeout_seconds) {
        timing->upload_timeout = true;
        return 1;
    }
    if (timing->upload_finished && !timing->first_response_header &&
        seconds(now - *timing->upload_finished) >
            publication_protocol::header_timeout_seconds) {
        timing->header_timeout = true;
        return 1;
    }
    if (timing->first_response_header &&
        seconds(now - *timing->first_response_header) >
            publication_protocol::response_body_timeout_seconds) {
        timing->response_body_timeout = true;
        return 1;
    }
    return 0;
}

std::size_t curl_header(char*, std::size_t size, std::size_t count, void* opaque) {
    if (size != 0 && count > static_cast<std::size_t>(-1) / size) return 0;
    auto* timing = static_cast<CurlTiming*>(opaque);
    const auto now = std::chrono::steady_clock::now();
    if (!timing->upload_finished) timing->upload_finished = now;
    if (!timing->first_response_header) timing->first_response_header = now;
    return size * count;
}

publication_protocol::HttpResponse perform_https_exchange(
    const publication_protocol::HttpRequest& request) {
    static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) {
        throw std::runtime_error("cannot initialize HTTPS publication transport");
    }

    CURL* handle = curl_easy_init();
    if (!handle) throw std::runtime_error("cannot initialize HTTPS publication request");

    struct curl_slist* headers = nullptr;
    auto cleanup = [&]() {
        if (headers) {
            curl_slist_free_all(headers);
            headers = nullptr;
        }
        if (handle) {
            curl_easy_cleanup(handle);
            handle = nullptr;
        }
    };

    try {
        for (const auto& [name, value] : request.headers) {
            if (name.empty() || name.find_first_of("\r\n:") != std::string::npos ||
                value.find_first_of("\r\n") != std::string::npos) {
                throw std::runtime_error("publication request contains an invalid HTTP header");
            }
            const std::string line = name + ": " + value;
            auto* appended = curl_slist_append(headers, line.c_str());
            if (!appended) {
                throw std::runtime_error("cannot allocate HTTPS publication headers");
            }
            headers = appended;
        }
        auto* appended = curl_slist_append(headers, "Expect:");
        if (!appended) {
            throw std::runtime_error("cannot allocate HTTPS publication headers");
        }
        headers = appended;

        CurlBuffer response{{}, publication_protocol::max_receipt_bytes, false};
        CurlTiming timing;
        timing.expected_upload = static_cast<curl_off_t>(request.body.size());

        curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
                         static_cast<curl_off_t>(request.body.size()));
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, curl_header);
        curl_easy_setopt(handle, CURLOPT_HEADERDATA, &timing);
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, curl_progress);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &timing);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT,
                         publication_protocol::connect_timeout_seconds);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT,
                         publication_protocol::connect_timeout_seconds +
                         publication_protocol::upload_timeout_seconds +
                         publication_protocol::header_timeout_seconds +
                         publication_protocol::response_body_timeout_seconds);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Emojineer-emji/0.16");
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif

        const CURLcode result = curl_easy_perform(handle);
        long status = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        char* content_type_raw = nullptr;
        curl_easy_getinfo(handle, CURLINFO_CONTENT_TYPE, &content_type_raw);
        const std::string content_type = content_type_raw ? content_type_raw : "";

        if (response.exceeded) {
            throw std::runtime_error("publication response exceeds 16384-byte size limit");
        }
        if (result != CURLE_OK) {
            if (timing.upload_timeout) {
                throw std::runtime_error("publication upload exceeded 300-second deadline");
            }
            if (timing.header_timeout) {
                throw std::runtime_error(
                    "publication response headers exceeded 30-second deadline");
            }
            if (timing.response_body_timeout) {
                throw std::runtime_error(
                    "publication response body exceeded 300-second deadline");
            }
            throw std::runtime_error("HTTPS publication transport failed: " +
                                     std::string(curl_easy_strerror(result)));
        }

        cleanup();
        return {status, content_type, std::move(response.bytes)};
    } catch (...) {
        cleanup();
        throw;
    }
}
#endif

} // namespace

std::optional<std::string> credential_from_environment() {
    const char* token = std::getenv("EMOJINEER_TOKEN");
    if (!token || token[0] == '\0') return std::nullopt;
    return std::string(token);
}

RegistryPublishCredential parse_credential(std::string_view token,
                                           std::string_view namespace_id) {
    RegistryPublishCredential credential{std::string(token), std::string(namespace_id)};
    validate_credential(credential);
    return credential;
}

PublicationReceipt parse_publication_receipt(std::string_view text) {
    if (text.size() > publication_protocol::max_receipt_bytes) {
        throw std::runtime_error("publication receipt exceeds 16384-byte size limit");
    }
    const auto fields = JsonStringObjectParser(text).parse();
    static const std::vector<std::string> allowed = {
        "artifact_sha256", "content_sha256", "package_name", "protocol_version",
        "receipt_id", "registry_id", "timestamp", "version"
    };
    if (fields.size() != allowed.size()) {
        throw std::runtime_error("publication receipt has unexpected JSON fields");
    }
    for (const auto& [key, value] : fields) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw std::runtime_error("publication receipt has unexpected JSON field");
        }
    }

    PublicationReceipt receipt;
    receipt.registry_id = required_field(fields, "registry_id");
    receipt.package_name = required_field(fields, "package_name");
    receipt.version = required_field(fields, "version");
    receipt.content_sha256 = required_field(fields, "content_sha256");
    receipt.artifact_sha256 = required_field(fields, "artifact_sha256");
    receipt.protocol_version = required_field(fields, "protocol_version");
    receipt.receipt_id = required_field(fields, "receipt_id");
    receipt.timestamp = required_field(fields, "timestamp");
    validate_receipt_shape(receipt);
    return receipt;
}

std::string render_publication_receipt(const PublicationReceipt& receipt) {
    validate_receipt_shape(receipt);
    std::ostringstream out;
    out << '{'
        << "\"artifact_sha256\":\"" << json_escape(receipt.artifact_sha256) << "\","
        << "\"content_sha256\":\"" << json_escape(receipt.content_sha256) << "\","
        << "\"package_name\":\"" << json_escape(receipt.package_name) << "\","
        << "\"protocol_version\":\"" << json_escape(receipt.protocol_version) << "\","
        << "\"receipt_id\":\"" << json_escape(receipt.receipt_id) << "\","
        << "\"registry_id\":\"" << json_escape(receipt.registry_id) << "\","
        << "\"timestamp\":\"" << json_escape(receipt.timestamp) << "\","
        << "\"version\":\"" << json_escape(receipt.version) << "\"}";
    return out.str();
}

void verify_publication_receipt(const PublicationReceipt& receipt,
                                const std::string& expected_registry_id,
                                const std::string& expected_package_name,
                                const std::string& expected_version,
                                const std::string& expected_content_sha256,
                                const std::string& expected_artifact_sha256) {
    validate_receipt_shape(receipt);
    if (receipt.registry_id != expected_registry_id) {
        throw std::runtime_error("publication receipt registry identity mismatch");
    }
    if (receipt.package_name != expected_package_name) {
        throw std::runtime_error("publication receipt package identity mismatch");
    }
    if (receipt.version != expected_version) {
        throw std::runtime_error("publication receipt version mismatch");
    }
    if (receipt.content_sha256 != expected_content_sha256) {
        throw std::runtime_error("publication receipt content identity mismatch");
    }
    if (receipt.artifact_sha256 != expected_artifact_sha256) {
        throw std::runtime_error("publication receipt artifact identity mismatch");
    }
}

void save_receipt_file(const std::filesystem::path& path,
                       const PublicationReceipt& receipt) {
    write_atomic_receipt(path, render_publication_receipt(receipt));
}

namespace publication_protocol {

PublicationReceipt publish_with_transport(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential,
    const IdentityLookup& identity_lookup,
    const Exchange& exchange) {
    if (endpoint.kind != RegistryTransportKind::Https) {
        throw std::runtime_error(
            "authenticated publication requires an HTTPS registry endpoint");
    }
    if (!identity_lookup || !exchange) {
        throw std::runtime_error("authenticated publication transport is not configured");
    }
    validate_credential(credential);

    // The shipped Train 14 identity resource is fetched and verified before any
    // Authorization-bearing request is constructed or sent.
    const std::string identity = identity_lookup(endpoint);
    if (!valid_portable_name(identity, true)) {
        throw std::runtime_error("registry descriptor returned an invalid identity");
    }

    const std::string bytes = build_package_artifact_bytes(package_root);
    if (bytes.size() > max_upload_bytes) {
        throw std::runtime_error(
            "package artifact exceeds 134217728-byte authenticated publication limit");
    }
    const PackageArtifact artifact = parse_package_artifact(bytes);
    if (!valid_portable_name(artifact.name)) {
        throw std::runtime_error("package artifact has invalid publication name");
    }
    (void)parse_semantic_version(artifact.version);
    if (!valid_sha256(artifact.content_sha256) || !valid_sha256(artifact.artifact_sha256)) {
        throw std::runtime_error("package artifact has invalid publication identity");
    }

    HttpRequest request;
    request.url = endpoint.canonical + "/v1/publish";
    request.headers = {
        {"Authorization", "Bearer " + credential.token},
        {"Content-Type", std::string(request_media_type)},
        {"Accept", std::string(receipt_media_type)},
        {"X-Emojineer-Protocol", std::string(version)},
        {"X-Emojineer-Namespace", credential.namespace_id},
        {"X-Emojineer-Package", artifact.name},
        {"X-Emojineer-Version", artifact.version},
        {"X-Emojineer-Content-SHA256", artifact.content_sha256},
        {"X-Emojineer-Artifact-SHA256", artifact.artifact_sha256},
    };
    request.body = bytes;

    const HttpResponse response = exchange(request);
    if (response.body.size() > max_receipt_bytes) {
        throw std::runtime_error("publication response exceeds 16384-byte size limit");
    }
    throw_for_publication_status(response.status);
    if (!media_type_matches(response.content_type, receipt_media_type)) {
        throw std::runtime_error("publication response has unexpected media type");
    }

    const PublicationReceipt receipt = parse_publication_receipt(response.body);
    verify_publication_receipt(receipt,
                               identity,
                               artifact.name,
                               artifact.version,
                               artifact.content_sha256,
                               artifact.artifact_sha256);
    return receipt;
}

} // namespace publication_protocol

PublicationReceipt publish_package_to_https_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential) {
#if defined(EMOJINEER_HAVE_CURL)
    return publication_protocol::publish_with_transport(
        package_root,
        endpoint,
        credential,
        [](const RegistryEndpoint& target) { return registry_identity(target); },
        [](const publication_protocol::HttpRequest& request) {
            return perform_https_exchange(request);
        });
#else
    (void)package_root;
    (void)endpoint;
    (void)credential;
    throw std::runtime_error(
        "HTTPS publication is unavailable in this build; rebuild with libcurl");
#endif
}

} // namespace emojineer
