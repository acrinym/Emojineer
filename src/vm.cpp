#include "emojineer/vm.hpp"
#include "emojineer/unicode.hpp"
#include "emojineer/intrinsic.hpp"
#include "emojineer/version.hpp"
#include <unicode/utf8.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <istream>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>
#include <random>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef EMOJINEER_HAVE_CURL
#include <curl/curl.h>
#endif

namespace emojineer { namespace {
constexpr std::size_t MaxCallDepth = 4096;

template<typename Fail>
std::int64_t checked_add(std::int64_t a, std::int64_t b, std::uint32_t line, Fail fail) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        fail(line, "integer overflow: ➕ overflow");
    return a + b;
}

template<typename Fail>
std::int64_t checked_sub(std::int64_t a, std::int64_t b, std::uint32_t line, Fail fail) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b))
        fail(line, "integer overflow: ➖ overflow");
    return a - b;
}

template<typename Fail>
std::int64_t checked_mul(std::int64_t a, std::int64_t b, std::uint32_t line, Fail fail) {
    if (a == 0 || b == 0) return 0;
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN))
        fail(line, "integer overflow: ✖️ overflow");
    if (a > 0) {
        if (b > 0) {
            if (a > INT64_MAX / b) fail(line, "integer overflow: ✖️ overflow");
        } else if (b < INT64_MIN / a) {
            fail(line, "integer overflow: ✖️ overflow");
        }
    } else {
        if (b > 0) {
            if (a < INT64_MIN / b) fail(line, "integer overflow: ✖️ overflow");
        } else if (a > INT64_MAX / b) {
            fail(line, "integer overflow: ✖️ overflow");
        }
    }
    return a * b;
}

constexpr std::size_t MaxFilesystemReadBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxFilesystemWriteBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxNetworkReadBytes = 1024 * 1024;
constexpr std::size_t MaxNetworkRequestBytes = 1024 * 1024;
constexpr std::size_t MaxBytesValue = 16 * 1024 * 1024;
constexpr std::size_t MaxRecordFields = 65'536;

std::string require_native_string(const Value& value, const std::string& facility) {
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    throw std::runtime_error(facility + " requires a text argument");
}

std::uint64_t require_positive_bound(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) {
        if (*integer > 0) return static_cast<std::uint64_t>(*integer);
    } else if (const auto* number = std::get_if<double>(&value)) {
        if (std::isfinite(*number) && std::floor(*number) == *number && *number > 0 &&
            *number <= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return static_cast<std::uint64_t>(*number);
    }
    throw std::runtime_error("random.int requires a positive whole-number bound");
}

std::size_t require_index(const Value& value, std::size_t size, std::string_view operation) {
    double raw = 0;
    if (const auto* d = std::get_if<double>(&value)) raw = *d;
    else if (const auto* i = std::get_if<std::int64_t>(&value)) raw = static_cast<double>(*i);
    else throw std::runtime_error(std::string(operation) + " index must be a whole number");
    if (!std::isfinite(raw) || std::floor(raw) != raw || raw < 0 || raw >= static_cast<double>(size))
        throw std::runtime_error(std::string(operation) + " index out of range");
    return static_cast<std::size_t>(raw);
}

std::uint8_t require_byte(const Value& value, std::string_view operation) {
    std::int64_t raw = -1;
    if (const auto* i = std::get_if<std::int64_t>(&value)) raw = *i;
    else if (const auto* d = std::get_if<double>(&value)) {
        if (!std::isfinite(*d) || std::floor(*d) != *d || *d < 0 || *d > 255)
            throw std::runtime_error(std::string(operation) + " requires a byte value 0..255");
        raw = static_cast<std::int64_t>(*d);
    } else throw std::runtime_error(std::string(operation) + " requires a byte value 0..255");
    if (raw < 0 || raw > 255) throw std::runtime_error(std::string(operation) + " requires a byte value 0..255");
    return static_cast<std::uint8_t>(raw);
}

bool valid_utf8(std::string_view text) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) return false;
    int32_t index = 0;
    const auto length = static_cast<int32_t>(text.size());
    while (index < length) {
        UChar32 codepoint = 0;
        U8_NEXT(text.data(), index, length, codepoint);
        if (codepoint < 0) return false;
    }
    return true;
}

const BytesPtr& require_bytes_ref(const Value& value, std::string_view operation) {
    const auto* bytes = std::get_if<BytesPtr>(&value);
    if (!bytes || !*bytes) throw std::runtime_error(std::string(operation) + " requires bytes");
    return *bytes;
}

const RecordPtr& require_record_ref(const Value& value, std::string_view operation) {
    const auto* record = std::get_if<RecordPtr>(&value);
    if (!record || !*record) throw std::runtime_error(std::string(operation) + " requires a record");
    return *record;
}

const ResultPtr& require_result_ref(const Value& value, std::string_view operation) {
    const auto* result = std::get_if<ResultPtr>(&value);
    if (!result || !*result) throw std::runtime_error(std::string(operation) + " requires a result");
    return *result;
}

std::string hex_encode(const BytesPtr& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(value->bytes.size() * 2);
    for (const auto byte : value->bytes) {
        output.push_back(digits[byte >> 4]);
        output.push_back(digits[byte & 0x0f]);
    }
    return output;
}

int hex_digit(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

BytesPtr hex_decode(std::string_view text) {
    if ((text.size() % 2) != 0) throw std::runtime_error("encoding.hex-decode requires an even number of hex digits");
    if (text.size() / 2 > MaxBytesValue) throw std::runtime_error("decoded bytes exceed 16 MiB limit");
    auto output = std::make_shared<BytesValue>();
    output->bytes.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = hex_digit(static_cast<unsigned char>(text[i]));
        const int low = hex_digit(static_cast<unsigned char>(text[i + 1]));
        if (high < 0 || low < 0) throw std::runtime_error("encoding.hex-decode rejected non-hex input");
        output->bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return output;
}

std::string base64_encode(const BytesPtr& value) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((value->bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < value->bytes.size(); i += 3) {
        const std::uint32_t a = value->bytes[i];
        const std::uint32_t b = i + 1 < value->bytes.size() ? value->bytes[i + 1] : 0;
        const std::uint32_t c = i + 2 < value->bytes.size() ? value->bytes[i + 2] : 0;
        const std::uint32_t block = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(block >> 18) & 63]);
        output.push_back(alphabet[(block >> 12) & 63]);
        output.push_back(i + 1 < value->bytes.size() ? alphabet[(block >> 6) & 63] : '=');
        output.push_back(i + 2 < value->bytes.size() ? alphabet[block & 63] : '=');
    }
    return output;
}

int base64_digit(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

BytesPtr base64_decode(std::string_view text) {
    if ((text.size() % 4) != 0) throw std::runtime_error("encoding.base64-decode requires canonical length");
    const std::size_t padding = text.empty() ? 0 :
        (text.back() == '=' ? (text.size() > 1 && text[text.size() - 2] == '=' ? 2 : 1) : 0);
    const std::size_t decoded = (text.size() / 4) * 3 - padding;
    if (decoded > MaxBytesValue) throw std::runtime_error("decoded bytes exceed 16 MiB limit");
    auto output = std::make_shared<BytesValue>();
    output->bytes.reserve(decoded);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const int a = base64_digit(static_cast<unsigned char>(text[i]));
        const int b = base64_digit(static_cast<unsigned char>(text[i + 1]));
        const bool pad2 = text[i + 2] == '=';
        const bool pad3 = text[i + 3] == '=';
        const int c = pad2 ? 0 : base64_digit(static_cast<unsigned char>(text[i + 2]));
        const int d = pad3 ? 0 : base64_digit(static_cast<unsigned char>(text[i + 3]));
        if (a < 0 || b < 0 || c < 0 || d < 0 || (!last && (pad2 || pad3)) || (pad2 && !pad3))
            throw std::runtime_error("encoding.base64-decode rejected non-canonical input");
        const std::uint32_t block = (static_cast<std::uint32_t>(a) << 18) |
                                    (static_cast<std::uint32_t>(b) << 12) |
                                    (static_cast<std::uint32_t>(c) << 6) |
                                    static_cast<std::uint32_t>(d);
        output->bytes.push_back(static_cast<std::uint8_t>((block >> 16) & 255));
        if (!pad2) output->bytes.push_back(static_cast<std::uint8_t>((block >> 8) & 255));
        if (!pad3) output->bytes.push_back(static_cast<std::uint8_t>(block & 255));
        if (last) {
            if (pad2 && (b & 0x0f) != 0) throw std::runtime_error("encoding.base64-decode rejected non-canonical padding bits");
            if (!pad2 && pad3 && (c & 0x03) != 0) throw std::runtime_error("encoding.base64-decode rejected non-canonical padding bits");
        }
    }
    return output;
}

std::string read_host_text(const std::string& raw_path) {
    if (raw_path.empty() || raw_path.size() > 32 * 1024 || raw_path.find('\0') != std::string::npos)
        throw std::runtime_error("filesystem.read-text requires a bounded non-empty path");
    const std::filesystem::path path(raw_path);
#ifdef _WIN32
    struct HandleGuard {
        HANDLE handle{INVALID_HANDLE_VALUE};
        ~HandleGuard() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    HandleGuard file{CreateFileW(path.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("filesystem.read-text cannot open requested path");
    if (GetFileType(file.handle) != FILE_TYPE_DISK)
        throw std::runtime_error("filesystem.read-text requires a regular file");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        throw std::runtime_error("filesystem.read-text requires a regular file");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size) || size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) > MaxFilesystemReadBytes)
        throw std::runtime_error("filesystem.read-text exceeds 16 MiB limit");
    std::string result(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto remaining = result.size() - offset;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(remaining, 1u << 20));
        DWORD read = 0;
        if (!ReadFile(file.handle, result.data() + offset, request, &read, nullptr))
            throw std::runtime_error("filesystem.read-text failed while reading path");
        if (read == 0) break;
        offset += read;
    }
    if (offset != result.size())
        throw std::runtime_error("filesystem.read-text file changed while reading");
    return result;
#else
    struct FdGuard {
        int fd{-1};
        ~FdGuard() { if (fd >= 0) ::close(fd); }
    };
    int flags = O_RDONLY | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    FdGuard file{::open(path.c_str(), flags)};
    if (file.fd < 0)
        throw std::runtime_error("filesystem.read-text cannot open requested path");
    struct stat status{};
    if (::fstat(file.fd, &status) != 0)
        throw std::runtime_error("filesystem.read-text cannot inspect requested path");
    if (!S_ISREG(status.st_mode))
        throw std::runtime_error("filesystem.read-text requires a regular file");
    if (status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) > MaxFilesystemReadBytes)
        throw std::runtime_error("filesystem.read-text exceeds 16 MiB limit");
    std::string result(static_cast<std::size_t>(status.st_size), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        const auto count = ::read(file.fd, result.data() + offset, result.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::runtime_error("filesystem.read-text failed while reading path");
        if (count == 0) break;
        offset += static_cast<std::size_t>(count);
    }
    if (offset != result.size())
        throw std::runtime_error("filesystem.read-text file changed while reading");
    return result;
#endif
}

void write_host_text(const std::string& raw_path, const std::string& text) {
    if (raw_path.empty() || raw_path.size() > 32 * 1024 || raw_path.find('\0') != std::string::npos)
        throw std::runtime_error("filesystem.write-text requires a bounded non-empty path");
    if (text.size() > MaxFilesystemWriteBytes)
        throw std::runtime_error("filesystem.write-text exceeds 16 MiB limit");
    const std::filesystem::path path(raw_path);
#ifdef _WIN32
    struct HandleGuard {
        HANDLE handle{INVALID_HANDLE_VALUE};
        ~HandleGuard() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    };
    HandleGuard file{CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                 nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (file.handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("filesystem.write-text cannot open requested path");
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
        throw std::runtime_error("filesystem.write-text requires a regular non-reparse file");
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(text.size() - offset, 1u << 20));
        DWORD written = 0;
        if (!WriteFile(file.handle, text.data() + offset, request, &written, nullptr) || written == 0)
            throw std::runtime_error("filesystem.write-text failed while writing path");
        offset += written;
    }
#else
    struct FdGuard {
        int fd{-1};
        ~FdGuard() { if (fd >= 0) ::close(fd); }
    };
    int flags = O_WRONLY | O_CREAT | O_TRUNC | O_NONBLOCK;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    FdGuard file{::open(path.c_str(), flags, 0666)};
    if (file.fd < 0) throw std::runtime_error("filesystem.write-text cannot open requested path");
    struct stat status{};
    if (::fstat(file.fd, &status) != 0 || !S_ISREG(status.st_mode))
        throw std::runtime_error("filesystem.write-text requires a regular file");
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto count = ::write(file.fd, text.data() + offset, text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("filesystem.write-text failed while writing path");
        offset += static_cast<std::size_t>(count);
    }
#endif
}

void create_host_directory(const std::string& raw_path) {
    if (raw_path.empty() || raw_path.size() > 32 * 1024 || raw_path.find('\0') != std::string::npos)
        throw std::runtime_error("filesystem.create-directory requires a bounded non-empty path");
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(raw_path), error);
    if (error) throw std::runtime_error("filesystem.create-directory failed: " + error.message());
    if (!std::filesystem::is_directory(std::filesystem::path(raw_path), error) || error)
        throw std::runtime_error("filesystem.create-directory did not produce a directory");
}

bool valid_environment_name(const std::string& name) {
    if (name.empty() || name.size() > 256) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || name.front() == '_')) return false;
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    });
}

#ifdef EMOJINEER_HAVE_CURL
struct CurlBuffer {
    std::string data;
    bool overflow{false};
};

std::size_t curl_write(void* contents, std::size_t size, std::size_t count, void* user) {
    auto* buffer = static_cast<CurlBuffer*>(user);
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count) {
        buffer->overflow = true;
        return 0;
    }
    const auto bytes = size * count;
    if (buffer->data.size() > MaxNetworkReadBytes || bytes > MaxNetworkReadBytes - buffer->data.size()) {
        buffer->overflow = true;
        return 0;
    }
    buffer->data.append(static_cast<const char*>(contents), bytes);
    return bytes;
}
#endif

std::string https_get(const std::string& url) {
    if (url.size() > 8192 || url.rfind("https://", 0) != 0)
        throw std::runtime_error("network.get requires a bounded https:// URL");
    const auto authority_end = url.find('/', 8);
    const auto authority = url.substr(8, authority_end == std::string::npos ? std::string::npos : authority_end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        url.find('\r') != std::string::npos || url.find('\n') != std::string::npos)
        throw std::runtime_error("network.get rejected URL authority");
#ifndef EMOJINEER_HAVE_CURL
    throw std::runtime_error("network.get is unavailable because this build has no libcurl support");
#else
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) throw std::runtime_error("network.get could not initialize libcurl");
    CurlBuffer buffer;
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    const std::string user_agent = "Emojineer/" + std::string(emojineer::version);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, user_agent.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &buffer);
    const auto result = curl_easy_perform(handle.get());
    if (buffer.overflow) throw std::runtime_error("network.get response exceeds 1 MiB limit");
    if (result != CURLE_OK) throw std::runtime_error(std::string("network.get failed: ") + curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw std::runtime_error("network.get received HTTP status " + std::to_string(status));
    return buffer.data;
#endif
}

std::string https_request(const std::string& method, const std::string& url, const std::string& body) {
    if (method != "GET" && method != "POST" && method != "PUT" &&
        method != "PATCH" && method != "DELETE")
        throw std::runtime_error("network.request method must be GET, POST, PUT, PATCH, or DELETE");
    if (body.size() > MaxNetworkRequestBytes)
        throw std::runtime_error("network.request body exceeds 1 MiB limit");
    if (method == "GET" && !body.empty())
        throw std::runtime_error("network.request GET body must be empty");
    if (url.size() > 8192 || url.rfind("https://", 0) != 0)
        throw std::runtime_error("network.request requires a bounded https:// URL");
    const auto authority_end = url.find('/', 8);
    const auto authority = url.substr(8, authority_end == std::string::npos ? std::string::npos : authority_end - 8);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        url.find('\r') != std::string::npos || url.find('\n') != std::string::npos)
        throw std::runtime_error("network.request rejected URL authority");
#ifndef EMOJINEER_HAVE_CURL
    throw std::runtime_error("network.request is unavailable because this build has no libcurl support");
#else
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), &curl_easy_cleanup);
    if (!handle) throw std::runtime_error("network.request could not initialize libcurl");
    CurlBuffer buffer;
    const std::string user_agent = "Emojineer/" + std::string(emojineer::version);
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, user_agent.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    if (!body.empty()) {
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &buffer);
    const auto result = curl_easy_perform(handle.get());
    if (buffer.overflow) throw std::runtime_error("network.request response exceeds 1 MiB limit");
    if (result != CURLE_OK)
        throw std::runtime_error(std::string("network.request failed: ") + curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw std::runtime_error("network.request received HTTP status " + std::to_string(status));
    return buffer.data;
#endif
}
} // anonymous namespace

VM::VM(std::istream& i, std::ostream& o, std::uint64_t fuel, ExecutionPolicy policy,
       const InteropRegistry* interop, std::vector<std::string> program_arguments)
    : input_(i), output_(o), fuel_(fuel), remaining_fuel_(fuel), policy_(std::move(policy)),
      interop_registry_(interop), program_arguments_(std::move(program_arguments)) {
    validate_execution_policy(policy_);
    deterministic_random_state_ = policy_.deterministic_seed ? policy_.deterministic_seed : 0x9E3779B97F4A7C15ULL;
    deterministic_clock_ms_ = policy_.deterministic_clock_ms;
}

std::optional<SourcePosition> VM::get_current_source_position() const {
    if (!current_chunk_ || ip_ >= current_chunk_->source_map.size()) {
        return std::nullopt;
    }
    const auto& src = current_chunk_->source_map[ip_];
    return SourcePosition(src.source_path, src.line, src.column);
}

std::vector<DebugFrame> VM::get_call_stack() const {
    // Build frames in reverse order so frame 0 is innermost (currently executing)
    // This matches debugger conventions where frame 0 is the current function
    std::vector<DebugFrame> stack;
    
    // Determine which frame is currently executing (innermost)
    // The current instruction pointer tells us which frame we're in
    std::size_t innermost_idx = frames_.empty() ? 0 : frames_.size() - 1;
    
    for (std::size_t i = 0; i < frames_.size(); ++i) {
        const auto& f = frames_[i];
        DebugFrame frame;
        // Frame 0 is innermost (most recent call), frames increase outward
        frame.frame_index = frames_.size() - 1 - i;
        
        if (f.function_index < current_chunk_->functions.size()) {
            frame.function_name = current_chunk_->functions[f.function_index].name;
        }
        
        // For the innermost (currently executing) frame, use current IP
        // For suspended callers, use return IP - 1 (where they will resume)
        if (i == innermost_idx) {
            // Current executing frame - use current IP
            if (ip_ > 0 && ip_ < current_chunk_->source_map.size()) {
                const auto& src = current_chunk_->source_map[ip_];
                frame.source_position = SourcePosition(src.source_path, src.line, src.column);
            }
        } else {
            // Suspended caller - use return IP - 1 (where it will resume)
            if (f.return_ip > 0 && f.return_ip < current_chunk_->source_map.size()) {
                const auto& src = current_chunk_->source_map[f.return_ip - 1];
                frame.source_position = SourcePosition(src.source_path, src.line, src.column);
            }
        }
        
        if (f.function_index < current_chunk_->functions.size()) {
            const auto& fn = current_chunk_->functions[f.function_index];
            frame.parameters.resize(std::min<std::size_t>(fn.arity, f.locals.size()));
            for (std::size_t p = 0; p < frame.parameters.size(); ++p) {
                frame.parameters[p] = f.locals[p];
            }
            // Copy parameter names for debugging
            frame.parameter_names = fn.parameter_names;
            // Trim to actual parameter count
            if (frame.parameter_names.size() > frame.parameters.size()) {
                frame.parameter_names.resize(frame.parameters.size());
            }
            
            // Trim locals to exclude parameters (frame.locals contains only true locals)
            frame.locals.resize(f.locals.size() > fn.arity ? f.locals.size() - fn.arity : 0);
            for (std::size_t l = fn.arity; l < f.locals.size(); ++l) {
                frame.locals[l - fn.arity] = f.locals[l];
            }
            
            // Copy local names for debugging, offset by arity to match trimmed locals
            // fn.local_names includes parameters + locals, but frame.locals only has locals
            for (std::size_t l = fn.arity; l < fn.local_names.size(); ++l) {
                frame.local_names.push_back(fn.local_names[l]);
            }
            
            // Copy parameter names
            frame.parameter_names = fn.parameter_names;
            if (frame.parameter_names.size() > frame.parameters.size()) {
                frame.parameter_names.resize(frame.parameters.size());
            }
            
            // Populate named parameters from FunctionInfo metadata
            for (std::size_t p = 0; p < fn.parameter_names.size() && p < f.locals.size(); ++p) {
                frame.named_parameters[fn.parameter_names[p]] = f.locals[p];
            }
            
            // Populate named locals from FunctionInfo::local_names (includes parameters + local vars)
            for (std::size_t slot = 0; slot < fn.local_names.size() && slot < f.locals.size(); ++slot) {
                frame.named_locals[fn.local_names[slot]] = f.locals[slot];
            }
        }
        
        // Add globals for each frame (the VM's global state)
        frame.globals = globals_;
        
        stack.push_back(std::move(frame));
    }
    
    // Reverse so frame 0 is innermost
    std::reverse(stack.begin(), stack.end());
    // Re-index to ensure 0 = innermost
    for (std::size_t i = 0; i < stack.size(); ++i) {
        stack[i].frame_index = i;
    }
    
    return stack;
}

std::unordered_map<std::string, Value> VM::get_globals() const {
    return globals_;
}

void VM::preflight_interop_bindings(const Chunk& c) const {
    std::vector<bool> checked(c.interop_imports.size(), false);
    for (const auto& instruction : c.code) {
        if (instruction.op != OpCode::InteropCall) continue;
        const auto index = static_cast<std::size_t>(instruction.operand);
        if (index >= c.interop_imports.size() || checked[index]) continue;
        checked[index] = true;
        const auto& import = c.interop_imports[index];
        if (!interop_registry_) throw std::runtime_error("interop adapter registry is required for '" + import.external_name + "'");
        const auto* binding = interop_registry_->find(import.external_name);
        if (!binding) throw std::runtime_error("no interop adapter bound for '" + import.external_name + "'");
        if (binding->required_capabilities != import.required_capabilities)
            throw std::runtime_error("interop adapter capability contract mismatch for '" + import.external_name + "'");
        if (policy_.mode == ExecutionMode::Deterministic && !binding->deterministic)
            throw std::runtime_error("interop adapter is not deterministic: '" + import.external_name + "'");
    }
}

void VM::initialize_execution(const Chunk& c) {
    verify_bytecode(c);
    require_execution_capabilities(c.required_capabilities, policy_);
    preflight_interop_bindings(c);
    deterministic_random_state_ = policy_.deterministic_seed ? policy_.deterministic_seed : 0x9E3779B97F4A7C15ULL;
    deterministic_clock_ms_ = policy_.deterministic_clock_ms;
    stack_.clear();
    frames_.clear();
    globals_.clear();
    ip_ = 0;
    current_chunk_ = &c;
    remaining_fuel_ = fuel_;
    execution_finished_ = false;
    host_invoke_active_ = false;
    host_invoke_result_.reset();
    initial_execution_ = false;
}

void VM::execute(const Chunk& c) {
    if (initial_execution_ || current_chunk_ != &c) {
        initialize_execution(c);
    }
    run_execution_loop();
}

Value VM::invoke_export(const Chunk& c, std::string_view external_name, const std::vector<Value>& arguments) {
    const InteropExportInfo* export_info = nullptr;
    for (const auto& candidate : c.interop_exports) if (candidate.external_name == external_name) { export_info = &candidate; break; }
    if (!export_info) throw std::runtime_error("unknown interop export '" + std::string(external_name) + "'");
    if (arguments.size() != export_info->signature.parameters.size()) throw std::runtime_error("interop export argument count does not match signature");
    for (std::size_t i = 0; i < arguments.size(); ++i) validate_interop_value(arguments[i], export_info->signature.parameters[i]);

    if (initial_execution_ || current_chunk_ != &c) {
        initialize_execution(c);
        run_execution_loop();
        if (!execution_finished_) throw std::runtime_error("cannot invoke interop export while program initialization is paused");
    } else if (!execution_finished_) {
        throw std::runtime_error("cannot invoke interop export while VM execution is active");
    }

    const auto& function = c.functions.at(export_info->function_index);
    stack_.clear(); frames_.clear(); host_invoke_result_.reset();
    CallFrame frame; frame.return_ip = c.code.size(); frame.stack_base = 0; frame.function_index = export_info->function_index;
    frame.locals.resize(function.local_count, false);
    for (std::size_t i = 0; i < arguments.size(); ++i) frame.locals[i] = arguments[i];
    frames_.push_back(std::move(frame)); ip_ = function.entry; remaining_fuel_ = fuel_; execution_finished_ = false; host_invoke_active_ = true;
    run_execution_loop();
    if (!host_invoke_result_) throw std::runtime_error("interop export returned without a host result");
    Value result = std::move(*host_invoke_result_); host_invoke_result_.reset();
    validate_interop_value(result, export_info->signature.result);
    return result;
}

InteropBytes VM::invoke_export_abi(const Chunk& c, std::string_view external_name, std::span<const std::uint8_t> request) {
    try {
        const InteropExportInfo* export_info = nullptr;
        for (const auto& candidate : c.interop_exports) if (candidate.external_name == external_name) { export_info = &candidate; break; }
        if (!export_info) throw std::runtime_error("unknown interop export '" + std::string(external_name) + "'");
        auto arguments = decode_interop_request(export_info->signature, request);
        auto result = invoke_export(c, external_name, arguments);
        return encode_interop_success(export_info->signature.result, result);
    } catch (const std::exception& error) {
        try {
            return encode_interop_failure(error.what());
        } catch (const std::exception&) {
            return encode_interop_failure("interop invocation failed");
        }
    }
}

void VM::run_execution_loop() {
    if (!current_chunk_ || execution_finished_) return;
    const Chunk& c = *current_chunk_;
    
    while (ip_ < c.code.size()) {
        // Check if debugger wants to pause BEFORE executing this instruction
        // This ensures we don't execute any instruction when paused, allowing
        // zero-effect debugger preparation and breakpoint setting before execution
        if (debug_control_) {
            if (debug_control_->should_pause_before_execution()) {
                debug_paused_ = true;
                return;
            }
        }
        
        // Check fuel BEFORE executing instruction
        if (remaining_fuel_ == 0)
            runtime_error(c.code[ip_].line, "execution fuel exhausted (possible infinite loop)");
        
        // Execute the instruction first, then consume fuel - this ensures paused
        // programs don't lose fuel for instructions they haven't actually executed
        const Instruction ins = c.code[ip_++];
        const auto line = ins.line;
        
        switch (ins.op) {
            case OpCode::Constant:
                stack_.push_back(c.constants.at(static_cast<std::size_t>(ins.operand)));
                break;
                
            case OpCode::LoadGlobal: {
                auto n = constant_string(c, ins.operand, line);
                auto it = globals_.find(n);
                if (it == globals_.end())
                    runtime_error(line, "undefined emoji variable '" + n + "'");
                stack_.push_back(it->second);
                break;
            }
            
            case OpCode::StoreGlobal: {
                auto n = constant_string(c, ins.operand, line);
                globals_[n] = pop(line);
                break;
            }
            
            case OpCode::LoadLocal: {
                auto& f = frame(line);
                auto s = static_cast<std::size_t>(ins.operand);
                if (s >= f.locals.size()) runtime_error(line, "invalid local slot");
                stack_.push_back(f.locals[s]);
                break;
            }
            
            case OpCode::StoreLocal: {
                auto& f = frame(line);
                auto s = static_cast<std::size_t>(ins.operand);
                if (s >= f.locals.size()) runtime_error(line, "invalid local slot");
                f.locals[s] = pop(line);
                break;
            }
            
            case OpCode::AssertNumber:
                if (!std::holds_alternative<double>(peek(line)) && !std::holds_alternative<std::int64_t>(peek(line)))
                    runtime_error(line, "🔢 variable requires a number");
                break;
                
            case OpCode::AssertString:
                if (!std::holds_alternative<std::string>(peek(line)))
                    runtime_error(line, "🔤 variable requires text");
                break;
                
            case OpCode::AssertBool:
                if (!std::holds_alternative<bool>(peek(line)))
                    runtime_error(line, "🎯 variable requires ✅ or ❌");
                break;
                
            case OpCode::AssertArray:
                if (!std::holds_alternative<ArrayPtr>(peek(line)))
                    runtime_error(line, "📚 variable requires an array");
                break;
                
            case OpCode::Add: {
                Value r = pop(line), l = pop(line);
                if (auto* ln = std::get_if<double>(&l)) {
                    if (auto* rn = std::get_if<double>(&r))
                        stack_.emplace_back(*ln + *rn);
                    else
                        runtime_error(line, "➕ requires two numbers or two text values");
                } else if (auto* li = std::get_if<std::int64_t>(&l)) {
                    if (auto* ri = std::get_if<std::int64_t>(&r))
                        stack_.emplace_back(checked_add(*li, *ri, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                    else
                        runtime_error(line, "➕ requires two numbers or two text values");
                } else if (auto* ls = std::get_if<std::string>(&l)) {
                    if (auto* rs = std::get_if<std::string>(&r))
                        stack_.emplace_back(*ls + *rs);
                    else
                        runtime_error(line, "➕ requires two numbers or two text values");
                } else {
                    runtime_error(line, "➕ requires two numbers or two text values");
                }
                break;
            }
            
            case OpCode::Subtract: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l - r);
                break;
            }
            
            case OpCode::Multiply: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l * r);
                break;
            }
            
            case OpCode::Divide: {
                double r = pop_number(line), l = pop_number(line);
                if (r == 0) runtime_error(line, "division by zero");
                stack_.emplace_back(l / r);
                break;
            }
            
            case OpCode::Modulo: {
                double r = pop_number(line), l = pop_number(line);
                if (r == 0) runtime_error(line, "modulo by zero");
                stack_.emplace_back(std::fmod(l, r));
                break;
            }
            
            case OpCode::AddInt: {
                auto r = pop_int64(line), l = pop_int64(line);
                stack_.emplace_back(checked_add(l, r, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                break;
            }
            
            case OpCode::SubtractInt: {
                auto r = pop_int64(line), l = pop_int64(line);
                stack_.emplace_back(checked_sub(l, r, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                break;
            }
            
            case OpCode::MultiplyInt: {
                auto r = pop_int64(line), l = pop_int64(line);
                stack_.emplace_back(checked_mul(l, r, line, [this](auto q, const auto& m) { runtime_error(q, m); }));
                break;
            }
            
            case OpCode::Equal: {
                Value r = pop(line), l = pop(line);
                stack_.emplace_back(values_equal(l, r));
                break;
            }
            
            case OpCode::Less: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l < r);
                break;
            }
            
            case OpCode::Greater: {
                double r = pop_number(line), l = pop_number(line);
                stack_.emplace_back(l > r);
                break;
            }
            
            case OpCode::Negate:
                stack_.emplace_back(-pop_number(line));
                break;
                
            case OpCode::Not:
                stack_.emplace_back(!pop_bool(line));
                break;
                
            case OpCode::ReadLine: {
                std::string v;
                if (!std::getline(input_, v))
                    runtime_error(line, "📥 could not read input");
                stack_.emplace_back(std::move(v));
                break;
            }
            
            case OpCode::Print:
                output_ << value_to_string(pop(line)) << '\n';
                break;
                
            case OpCode::JumpIfFalse: {
                bool cond = pop_bool(line);
                if (!cond) ip_ = static_cast<std::size_t>(ins.operand);
                break;
            }
            
            case OpCode::Jump:
                ip_ = static_cast<std::size_t>(ins.operand);
                break;
                
            case OpCode::Call: {
                auto fi = static_cast<std::size_t>(ins.operand);
                const auto& fn = c.functions[fi];
                if (stack_.size() < fn.arity)
                    runtime_error(line, "not enough call arguments on VM stack");
                if (frames_.size() >= MaxCallDepth)
                    runtime_error(line, "maximum function call depth exceeded");
                CallFrame f;
                f.return_ip = ip_;
                f.stack_base = stack_.size() - fn.arity;
                f.function_index = fi;
                f.locals.resize(fn.local_count, false);
                for (std::size_t n = fn.arity; n > 0; --n)
                    f.locals[n - 1] = pop(line);
                frames_.push_back(std::move(f));
                ip_ = fn.entry;
                break;
            }
            
            case OpCode::HostCall:
                execute_host_call(ins.operand, line);
                break;

            case OpCode::InteropCall:
                execute_interop_call(ins.operand, line);
                break;

            case OpCode::IntrinsicCall:
                execute_intrinsic_call(ins.operand, line);
                break;

            case OpCode::Return: {
                if (frames_.empty())
                    runtime_error(line, "Return executed outside a function");
                Value result = pop(line);
                CallFrame f = std::move(frames_.back());
                frames_.pop_back();
                if (stack_.size() != f.stack_base)
                    runtime_error(line, "function returned with leaked operand stack values");
                if (host_invoke_active_ && frames_.empty() && f.return_ip == c.code.size()) {
                    host_invoke_result_ = std::move(result);
                    host_invoke_active_ = false;
                    execution_finished_ = true;
                    return;
                }
                ip_ = f.return_ip;
                stack_.push_back(std::move(result));
                break;
            }
            
            case OpCode::MakeArray: {
                auto count = static_cast<std::size_t>(ins.operand);
                if (stack_.size() < count)
                    runtime_error(line, "not enough values for array literal");
                auto a = std::make_shared<ArrayValue>();
                a->elements.resize(count);
                for (std::size_t n = count; n > 0; --n)
                    a->elements[n - 1] = pop(line);
                stack_.emplace_back(std::move(a));
                break;
            }
            
            case OpCode::Index: {
                Value iv = pop(line), cv = pop(line);
                if (auto* ap = std::get_if<ArrayPtr>(&cv)) {
                    if (!*ap) runtime_error(line, "🔎 received a null array");
                    stack_.push_back((*ap)->elements[require_index(iv, (*ap)->elements.size(), "🔎 array")]);
                    break;
                }
                if (auto* bp = std::get_if<BytesPtr>(&cv)) {
                    if (!*bp) runtime_error(line, "🔎 received null bytes");
                    stack_.emplace_back(static_cast<std::int64_t>((*bp)->bytes[require_index(iv, (*bp)->bytes.size(), "🔎 bytes")]));
                    break;
                }
                if (auto* rp = std::get_if<RecordPtr>(&cv)) {
                    if (!*rp) runtime_error(line, "🔎 received a null record");
                    const auto* key = std::get_if<std::string>(&iv);
                    if (!key) runtime_error(line, "🔎 record index must be text");
                    const auto found = (*rp)->fields.find(*key);
                    if (found == (*rp)->fields.end()) runtime_error(line, "🔎 record has no field '" + *key + "'");
                    stack_.push_back(found->second);
                    break;
                }
                runtime_error(line, "🔎 requires an array, bytes, or record");
            }
            
            case OpCode::Length: {
                Value v = pop(line);
                if (auto* ap = std::get_if<ArrayPtr>(&v)) {
                    if (!*ap) runtime_error(line, "📏 received a null array");
                    stack_.emplace_back(static_cast<double>((*ap)->elements.size()));
                } else if (auto* text = std::get_if<std::string>(&v)) {
                    stack_.emplace_back(static_cast<double>(segment_graphemes(*text).size()));
                } else if (auto* bp = std::get_if<BytesPtr>(&v)) {
                    if (!*bp) runtime_error(line, "📏 received null bytes");
                    stack_.emplace_back(static_cast<double>((*bp)->bytes.size()));
                } else if (auto* rp = std::get_if<RecordPtr>(&v)) {
                    if (!*rp) runtime_error(line, "📏 received a null record");
                    stack_.emplace_back(static_cast<double>((*rp)->fields.size()));
                } else {
                    runtime_error(line, "📏 requires an array, text, bytes, or record value");
                }
                break;
            }
            
            case OpCode::Append: {
                Value value = pop(line), cv = pop(line);
                if (auto* ap = std::get_if<ArrayPtr>(&cv)) {
                    if (!*ap) runtime_error(line, "📎 received a null array");
                    auto next = std::make_shared<ArrayValue>(**ap);
                    next->elements.push_back(std::move(value));
                    stack_.emplace_back(std::move(next));
                    break;
                }
                if (auto* bp = std::get_if<BytesPtr>(&cv)) {
                    if (!*bp) runtime_error(line, "📎 received null bytes");
                    if ((*bp)->bytes.size() >= MaxBytesValue) runtime_error(line, "📎 bytes exceed 16 MiB limit");
                    auto next = std::make_shared<BytesValue>(**bp);
                    next->bytes.push_back(require_byte(value, "📎 bytes"));
                    stack_.emplace_back(std::move(next));
                    break;
                }
                runtime_error(line, "📎 requires an array or bytes");
            }
            
            case OpCode::SetIndex: {
                Value value = pop(line), iv = pop(line), cv = pop(line);
                if (auto* ap = std::get_if<ArrayPtr>(&cv)) {
                    if (!*ap) runtime_error(line, "🧷 received a null array");
                    auto next = std::make_shared<ArrayValue>(**ap);
                    next->elements[require_index(iv, next->elements.size(), "🧷 array")] = std::move(value);
                    stack_.emplace_back(std::move(next));
                    break;
                }
                if (auto* bp = std::get_if<BytesPtr>(&cv)) {
                    if (!*bp) runtime_error(line, "🧷 received null bytes");
                    auto next = std::make_shared<BytesValue>(**bp);
                    next->bytes[require_index(iv, next->bytes.size(), "🧷 bytes")] = require_byte(value, "🧷 bytes");
                    stack_.emplace_back(std::move(next));
                    break;
                }
                if (auto* rp = std::get_if<RecordPtr>(&cv)) {
                    if (!*rp) runtime_error(line, "🧷 received a null record");
                    const auto* key = std::get_if<std::string>(&iv);
                    if (!key || key->empty() || key->size() > 256 || !valid_utf8(*key))
                        runtime_error(line, "🧷 record key must be 1..256 bytes of valid UTF-8 text");
                    auto next = std::make_shared<RecordValue>(**rp);
                    next->fields[*key] = std::move(value);
                    if (next->fields.size() > MaxRecordFields) runtime_error(line, "🧷 record exceeds 65536 field limit");
                    stack_.emplace_back(std::move(next));
                    break;
                }
                runtime_error(line, "🧷 requires an array, bytes, or record");
            }
            
            case OpCode::Halt:
                if (!frames_.empty()) runtime_error(line, "VM halted inside a function");
                if (!stack_.empty()) runtime_error(line, "VM halted with a non-empty stack");
                execution_finished_ = true;
                return;
        }
        
        // Consume fuel AFTER successful instruction execution - this ensures
        // debug pauses don't consume extra fuel that normal execution wouldn't
        --remaining_fuel_;
        
        // Check for step mode pause AFTER executing instruction
        if (debug_control_) {
            if (!debug_control_->debug_hook()) {
                debug_paused_ = true;
                return;
            }
        }
    }
    
    runtime_error(0, "bytecode terminated without Halt");
}

std::uint64_t VM::next_deterministic_random() {
    std::uint64_t x = deterministic_random_state_;
    if (x == 0) x = 0x9E3779B97F4A7C15ULL;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    deterministic_random_state_ = x;
    return x * 2685821657736338717ULL;
}

void VM::execute_host_call(std::int32_t operand, std::uint32_t line) {
    const auto facility = native_facility_from_operand(operand);
    if (!facility) runtime_error(line, "invalid native facility operand");
    const auto capability = native_facility_capability(*facility);
    const auto bit = capability_mask(capability);
    if ((policy_.grants & bit) == 0)
        runtime_error(line, "native facility requires capability grant: " + capability_name(capability));
    if (policy_.mode == ExecutionMode::Sandbox)
        runtime_error(line, "native facilities are unavailable in sandbox mode");
    if (policy_.mode == ExecutionMode::Deterministic &&
        capability != Capability::Clock && capability != Capability::Random)
        runtime_error(line, "real host facilities are unavailable in deterministic mode");

    const auto arity = native_facility_arity(*facility);
    if (stack_.size() < arity) runtime_error(line, "not enough native call arguments on VM stack");
    std::vector<Value> args(arity);
    for (std::size_t n = arity; n > 0; --n) args[n - 1] = pop(line);

    try {
        switch (*facility) {
            case NativeFacility::FilesystemReadText:
                stack_.emplace_back(read_host_text(require_native_string(args[0], native_facility_name(*facility))));
                return;
            case NativeFacility::NetworkGet:
                stack_.emplace_back(https_get(require_native_string(args[0], native_facility_name(*facility))));
                return;
            case NativeFacility::ProcessRun: {
                const auto command = require_native_string(args[0], native_facility_name(*facility));
                if (command.empty() || command.size() > 32768 || command.find('\0') != std::string::npos)
                    throw std::runtime_error("process.run requires a non-empty command up to 32 KiB");
                const int status = std::system(command.c_str());
                if (status == -1) throw std::runtime_error("process.run could not invoke the host command processor");
                stack_.emplace_back(static_cast<std::int64_t>(status));
                return;
            }
            case NativeFacility::ClockMillis:
                if (policy_.mode == ExecutionMode::Deterministic) {
                    stack_.emplace_back(deterministic_clock_ms_++);
                } else {
                    const auto now = std::chrono::system_clock::now().time_since_epoch();
                    stack_.emplace_back(static_cast<std::int64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
                }
                return;
            case NativeFacility::RandomInt: {
                const auto bound = require_positive_bound(args[0]);
                std::uint64_t value = 0;
                if (policy_.mode == ExecutionMode::Deterministic) {
                    const auto limit = std::numeric_limits<std::uint64_t>::max() -
                                       (std::numeric_limits<std::uint64_t>::max() % bound);
                    do { value = next_deterministic_random(); } while (value >= limit);
                    value %= bound;
                } else {
                    std::random_device source;
                    const auto seed = (static_cast<std::uint64_t>(source()) << 32) ^
                                      static_cast<std::uint64_t>(source());
                    std::mt19937_64 engine(seed);
                    std::uniform_int_distribution<std::uint64_t> distribution(0, bound - 1);
                    value = distribution(engine);
                }
                stack_.emplace_back(static_cast<std::int64_t>(value));
                return;
            }
            case NativeFacility::HostEnvironment: {
                const auto name = require_native_string(args[0], native_facility_name(*facility));
                if (!valid_environment_name(name))
                    throw std::runtime_error("host.environment requires a portable environment variable name");
                const char* value = std::getenv(name.c_str());
                stack_.emplace_back(value ? std::string(value) : std::string{});
                return;
            }
            case NativeFacility::FilesystemWriteText:
                write_host_text(require_native_string(args[0], native_facility_name(*facility)),
                                require_native_string(args[1], native_facility_name(*facility)));
                stack_.emplace_back(true);
                return;
            case NativeFacility::FilesystemCreateDirectory:
                create_host_directory(require_native_string(args[0], native_facility_name(*facility)));
                stack_.emplace_back(true);
                return;
            case NativeFacility::NetworkRequest:
                stack_.emplace_back(https_request(
                    require_native_string(args[0], native_facility_name(*facility)),
                    require_native_string(args[1], native_facility_name(*facility)),
                    require_native_string(args[2], native_facility_name(*facility))));
                return;
        }
    } catch (const std::exception& error) {
        runtime_error(line, native_facility_name(*facility) + ": " + error.what());
    }
}

void VM::execute_intrinsic_call(std::int32_t operand, std::uint32_t line) {
    const auto intrinsic = intrinsic_from_operand(operand);
    if (!intrinsic) runtime_error(line, "invalid intrinsic operand");
    const auto arity = intrinsic_arity(*intrinsic);
    if (stack_.size() < arity) runtime_error(line, "not enough intrinsic arguments on VM stack");
    std::vector<Value> args(arity);
    for (std::size_t n = arity; n > 0; --n) args[n - 1] = pop(line);

    try {
        switch (*intrinsic) {
            case Intrinsic::RecordCreate: {
                const auto type = require_native_string(args[0], intrinsic_name(*intrinsic));
                if (type.empty() || type.size() > 256 || !valid_utf8(type))
                    throw std::runtime_error("record type name must be 1..256 bytes of valid UTF-8");
                const auto* pairs = std::get_if<ArrayPtr>(&args[1]);
                if (!pairs || !*pairs) throw std::runtime_error("record.create requires a key/value array");
                if (((*pairs)->elements.size() % 2) != 0)
                    throw std::runtime_error("record.create key/value array must contain pairs");
                if ((*pairs)->elements.size() / 2 > MaxRecordFields)
                    throw std::runtime_error("record exceeds 65536 field limit");
                auto record = std::make_shared<RecordValue>();
                record->type_name = type;
                for (std::size_t i = 0; i < (*pairs)->elements.size(); i += 2) {
                    const auto* key = std::get_if<std::string>(&(*pairs)->elements[i]);
                    if (!key || key->empty() || key->size() > 256 || !valid_utf8(*key))
                        throw std::runtime_error("record keys must be 1..256 bytes of valid UTF-8");
                    if (!record->fields.emplace(*key, (*pairs)->elements[i + 1]).second)
                        throw std::runtime_error("record.create rejects duplicate field '" + *key + "'");
                }
                stack_.emplace_back(std::move(record));
                return;
            }
            case Intrinsic::RecordType:
                stack_.emplace_back(require_record_ref(args[0], intrinsic_name(*intrinsic))->type_name);
                return;
            case Intrinsic::RecordKeys: {
                const auto& record = require_record_ref(args[0], intrinsic_name(*intrinsic));
                auto keys = std::make_shared<ArrayValue>();
                keys->elements.reserve(record->fields.size());
                for (const auto& [key, ignored] : record->fields) {
                    (void)ignored;
                    keys->elements.emplace_back(key);
                }
                stack_.emplace_back(std::move(keys));
                return;
            }
            case Intrinsic::ResultOk:
            case Intrinsic::ResultError: {
                auto result = std::make_shared<ResultValue>();
                result->ok = *intrinsic == Intrinsic::ResultOk;
                result->payload = std::move(args[0]);
                stack_.emplace_back(std::move(result));
                return;
            }
            case Intrinsic::ResultIsOk:
                stack_.emplace_back(require_result_ref(args[0], intrinsic_name(*intrinsic))->ok);
                return;
            case Intrinsic::ResultPayload:
                stack_.push_back(require_result_ref(args[0], intrinsic_name(*intrinsic))->payload);
                return;
            case Intrinsic::Utf8Encode: {
                const auto text = require_native_string(args[0], intrinsic_name(*intrinsic));
                if (!valid_utf8(text)) throw std::runtime_error("encoding.utf8-encode requires valid UTF-8 text");
                if (text.size() > MaxBytesValue) throw std::runtime_error("encoded bytes exceed 16 MiB limit");
                auto bytes = std::make_shared<BytesValue>();
                bytes->bytes.assign(text.begin(), text.end());
                stack_.emplace_back(std::move(bytes));
                return;
            }
            case Intrinsic::Utf8Decode: {
                const auto& bytes = require_bytes_ref(args[0], intrinsic_name(*intrinsic));
                std::string text(bytes->bytes.begin(), bytes->bytes.end());
                if (!valid_utf8(text)) throw std::runtime_error("encoding.utf8-decode rejected malformed UTF-8");
                stack_.emplace_back(std::move(text));
                return;
            }
            case Intrinsic::HexEncode:
                stack_.emplace_back(hex_encode(require_bytes_ref(args[0], intrinsic_name(*intrinsic))));
                return;
            case Intrinsic::HexDecode:
                stack_.emplace_back(hex_decode(require_native_string(args[0], intrinsic_name(*intrinsic))));
                return;
            case Intrinsic::Base64Encode:
                stack_.emplace_back(base64_encode(require_bytes_ref(args[0], intrinsic_name(*intrinsic))));
                return;
            case Intrinsic::Base64Decode:
                stack_.emplace_back(base64_decode(require_native_string(args[0], intrinsic_name(*intrinsic))));
                return;
            case Intrinsic::ProgramArguments: {
                auto values = std::make_shared<ArrayValue>();
                values->elements.reserve(program_arguments_.size());
                for (const auto& argument : program_arguments_) values->elements.emplace_back(argument);
                stack_.emplace_back(std::move(values));
                return;
            }
        }
    } catch (const std::exception& error) {
        runtime_error(line, intrinsic_name(*intrinsic) + ": " + error.what());
    }
}

void VM::execute_interop_call(std::int32_t operand, std::uint32_t line) {
    if (!current_chunk_ || operand < 0 || static_cast<std::size_t>(operand) >= current_chunk_->interop_imports.size())
        runtime_error(line, "invalid interop import operand");
    const auto& import = current_chunk_->interop_imports[static_cast<std::size_t>(operand)];
    const auto required = import.required_capabilities;
    if ((policy_.grants & required) != required)
        runtime_error(line, "interop adapter requires capability grant(s): " + capability_mask_string(required));
    if (!interop_registry_) runtime_error(line, "interop adapter registry is not bound");
    const auto* binding = interop_registry_->find(import.external_name);
    if (!binding) runtime_error(line, "no interop adapter bound for '" + import.external_name + "'");
    if (binding->required_capabilities != required)
        runtime_error(line, "interop adapter capability contract mismatch for '" + import.external_name + "'");
    if (policy_.mode == ExecutionMode::Deterministic && !binding->deterministic)
        runtime_error(line, "interop adapter is not deterministic: '" + import.external_name + "'");
    const auto arity = import.signature.parameters.size();
    if (stack_.size() < arity) runtime_error(line, "not enough interop call arguments on VM stack");
    std::vector<Value> arguments(arity);
    for (std::size_t n = arity; n > 0; --n) arguments[n - 1] = pop(line);
    try {
        const auto request = encode_interop_request(import.signature, arguments);
        const auto response = binding->invoke(request);
        stack_.push_back(decode_interop_response(import.signature.result, response));
    } catch (const std::exception& error) {
        runtime_error(line, "interop '" + import.external_name + "': " + error.what());
    }
}

Value VM::pop(std::uint32_t line) {
    if (stack_.empty()) runtime_error(line, "VM stack underflow");
    Value v = std::move(stack_.back());
    stack_.pop_back();
    return v;
}

const Value& VM::peek(std::uint32_t line) const {
    if (stack_.empty()) runtime_error(line, "VM stack underflow");
    return stack_.back();
}

bool VM::pop_bool(std::uint32_t line) {
    Value v = pop(line);
    if (auto* b = std::get_if<bool>(&v)) return *b;
    runtime_error(line, "condition requires ✅ or ❌");
}

double VM::pop_number(std::uint32_t line) {
    Value v = pop(line);
    if (auto* n = std::get_if<double>(&v)) return *n;
    if (auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    runtime_error(line, "numeric operation requires numbers");
}

std::int64_t VM::pop_int64(std::uint32_t line) {
    Value v = pop(line);
    if (auto* i = std::get_if<std::int64_t>(&v)) return *i;
    runtime_error(line, "integer operation requires integers");
}

std::string VM::constant_string(const Chunk& c, std::int32_t idx, std::uint32_t line) const {
    if (idx < 0 || static_cast<std::size_t>(idx) >= c.constants.size())
        runtime_error(line, "invalid constant index");
    auto* s = std::get_if<std::string>(&c.constants[static_cast<std::size_t>(idx)]);
    if (!s) runtime_error(line, "bytecode expected string constant");
    return *s;
}

VM::CallFrame& VM::frame(std::uint32_t line) {
    if (frames_.empty()) runtime_error(line, "local variable access outside function");
    return frames_.back();
}

const VM::CallFrame& VM::frame(std::uint32_t line) const {
    if (frames_.empty()) runtime_error(line, "local variable access outside function");
    return frames_.back();
}

[[noreturn]] void VM::runtime_error(std::uint32_t line, const std::string& m) const {
    if (line) throw std::runtime_error("runtime line " + std::to_string(line) + ": " + m);
    throw std::runtime_error("runtime: " + m);
}

} // namespace emojineer
