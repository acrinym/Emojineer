#include "emojineer/registry_transport.hpp"

#include "emojineer/hash.hpp"
#include "emojineer/registry.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(EMOJINEER_HAVE_CURL)
#include <curl/curl.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace emojineer {
namespace {

constexpr std::uintmax_t max_descriptor_bytes = 16 * 1024;
constexpr std::uintmax_t max_index_bytes = 4 * 1024 * 1024;
constexpr std::uintmax_t max_artifact_bytes = 128ull * 1024ull * 1024ull;
constexpr std::string_view registry_magic = "EMJREGISTRY1\n";
constexpr std::string_view index_magic = "EMJREGPKG1\n";

// Package-scoped interprocess lock for serializing package-index publication
class PackageLock {
public:
    explicit PackageLock(const std::filesystem::path& lock_path)
        : lock_path_(lock_path) {
        std::filesystem::create_directories(lock_path_.parent_path());
#if defined(_WIN32)
        handle_ = CreateFileW(lock_path_.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("cannot create registry lock file");
        }
        // Initialize OVERLAPPED with zero offset - required by LockFileEx/UnlockFileEx
        overlapped_ = {};
        if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped_)) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            throw std::runtime_error("cannot acquire registry lock");
        }
#else
        fd_ = open(lock_path_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) {
            throw std::runtime_error("cannot create registry lock file");
        }
        if (flock(fd_, LOCK_EX) != 0) {
            close(fd_);
            fd_ = -1;
            throw std::runtime_error("cannot acquire registry lock");
        }
#endif
    }

    ~PackageLock() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            UnlockFileEx(handle_, 0, 1, 0, &overlapped_);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            flock(fd_, LOCK_UN);
            close(fd_);
            fd_ = -1;
        }
#endif
        // Keep lock file persistent to avoid splitting the lock domain:
        // a waiter may hold/open the old inode while a later process
        // recreates a new lock file and acquires a different lock.
    }

    PackageLock(const PackageLock&) = delete;
    PackageLock& operator=(const PackageLock&) = delete;

private:
    std::filesystem::path lock_path_;
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_ = {};
#else
    int fd_ = -1;
#endif
};

bool valid_portable_name(std::string_view value, bool allow_dot = false) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [allow_dot](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_' || (allow_dot && c == '.');
    });
}

void validate_package_name(std::string_view name) {
    if (!valid_portable_name(name)) {
        throw std::runtime_error("registry package name may contain only ASCII letters, digits, '-' and '_'");
    }
}

void validate_registry_id(std::string_view id) {
    if (!valid_portable_name(id, true)) {
        throw std::runtime_error("registry id may contain only ASCII letters, digits, '-', '_' and '.'");
    }
}

bool valid_sha256(std::string_view value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}

void validate_record(const RegistryVersionRecord& record) {
    (void)parse_semantic_version(record.version);
    if (!valid_sha256(record.content_sha256)) {
        throw std::runtime_error("registry version record has malformed content SHA-256");
    }
    if (!valid_sha256(record.artifact_sha256)) {
        throw std::runtime_error("registry version record has malformed artifact SHA-256");
    }
}

std::string read_bounded_file(const std::filesystem::path& path, std::uintmax_t limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw std::runtime_error("cannot inspect registry file '" + path.string() + "'");
    if (size > limit) throw std::runtime_error("registry response exceeds format size limit");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open registry file '" + path.string() + "'");
    // Read at most limit+1 bytes to detect if file grows during read (overflow-safe)
    constexpr std::uintmax_t one = 1;
    const std::uintmax_t max_read = (limit > std::numeric_limits<std::uintmax_t>::max() - one)
        ? std::numeric_limits<std::uintmax_t>::max()
        : limit + one;
    std::string result;
    result.reserve(static_cast<std::size_t>(std::min(size, max_read)));
    std::uintmax_t remaining = max_read;
    char buffer[4096];
    while (remaining > 0 && input) {
        const auto chunk = static_cast<std::streamsize>(std::min(remaining, static_cast<std::uintmax_t>(sizeof(buffer))));
        input.read(buffer, chunk);
        const auto got = input.gcount();
        if (got <= 0) break;
        result.append(buffer, static_cast<std::size_t>(got));
        remaining -= static_cast<std::uintmax_t>(got);
    }
    if (input.fail() && !input.eof()) {
        throw std::runtime_error("error reading registry file '" + path.string() + "'");
    }
    if (result.size() > limit) {
        throw std::runtime_error("registry response exceeds format size limit");
    }
    return result;
}

void write_atomic_file(const std::filesystem::path& path, std::string_view data) {
    std::filesystem::create_directories(path.parent_path());

    // Generate unique temp file name using thread-local PRNG to avoid race conditions
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<> dis(0, 15);

    std::filesystem::path temp;
    bool temp_created = false;

#if defined(_WIN32)
    HANDLE hFile = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped = {};

    // Retry loop for exclusive-create name collisions
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix;
        for (int i = 0; i < 16; ++i) {
            suffix += "0123456789abcdef"[dis(gen)];
        }
        temp = path;
        temp += ".tmp.";
        temp += suffix;

        // Use CREATE_NEW for exclusive-create semantics - fails if file exists
        hFile = CreateFileW(temp.c_str(),
            GENERIC_WRITE,
            0,  // No sharing - exclusive access
            nullptr,
            CREATE_NEW,  // Fail if exists, create if not
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (hFile != INVALID_HANDLE_VALUE) {
            temp_created = true;
            break;
        }

        const DWORD err = GetLastError();
        // Retry only on true name collision (ERROR_FILE_EXISTS)
        if (err != ERROR_FILE_EXISTS) {
            throw std::runtime_error("cannot create registry temp file '" + temp.string() + "': error " + std::to_string(err));
        }
        // Otherwise, retry with new suffix
    }

    if (!temp_created) {
        throw std::runtime_error("cannot create registry temp file: too many name collisions");
    }

    // Write data through owned handle - loop to handle short writes
    std::size_t remaining = data.size();
    const char* ptr = data.data();
    while (remaining > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(remaining, static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
        DWORD bytesWritten = 0;
        if (!WriteFile(hFile, ptr, chunk, &bytesWritten, nullptr)) {
            const DWORD err = GetLastError();
            CloseHandle(hFile);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing registry file '" + temp.string() + "': error " + std::to_string(err));
        }
        if (bytesWritten == 0) {
            CloseHandle(hFile);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing registry file '" + temp.string() + "': no progress");
        }
        ptr += bytesWritten;
        remaining -= bytesWritten;
    }

    // Ensure data is flushed to disk
    if (!FlushFileBuffers(hFile)) {
        const DWORD err = GetLastError();
        CloseHandle(hFile);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("failed while flushing registry file '" + temp.string() + "': error " + std::to_string(err));
    }

    // Close the handle before atomic replace
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    // Atomically replace destination using MoveFileEx with WRITE_THROUGH
    const auto temp_str = temp.native();
    const auto path_str = path.native();
    if (!MoveFileExW(temp_str.c_str(), path_str.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto err = GetLastError();
        std::error_code ec(err, std::system_category());
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("cannot commit registry file '" + path.string() + "': " + ec.message());
    }
#else
    int fd = -1;

    // Retry loop for exclusive-create name collisions
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix;
        for (int i = 0; i < 16; ++i) {
            suffix += "0123456789abcdef"[dis(gen)];
        }
        temp = path;
        temp += ".tmp.";
        temp += suffix;

        // Use O_CREAT | O_EXCL for exclusive-create semantics - fails if file exists
        fd = open(temp.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0666);
        if (fd >= 0) {
            temp_created = true;
            break;
        }

        // Retry only on true name collision (EEXIST)
        if (errno != EEXIST) {
            throw std::runtime_error("cannot create registry temp file '" + temp.string() + "': " + std::strerror(errno));
        }
        // Otherwise, retry with new suffix
    }

    if (!temp_created) {
        throw std::runtime_error("cannot create registry temp file: too many name collisions");
    }

    // Write data through owned file descriptor - loop to handle short writes and EINTR
    std::size_t remaining = data.size();
    const char* ptr = data.data();
    while (remaining > 0) {
        ssize_t written = write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;  // Retry interrupted system call
            int saved_errno = errno;
            close(fd);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing registry file '" + temp.string() + "': " + std::strerror(saved_errno));
        }
        if (written == 0) {
            close(fd);
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            throw std::runtime_error("failed while writing registry file '" + temp.string() + "': no progress");
        }
        ptr += written;
        remaining -= static_cast<std::size_t>(written);
    }

    // Ensure data is flushed to disk
    if (fsync(fd) != 0) {
        int saved_errno = errno;
        close(fd);
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("failed while flushing registry file '" + temp.string() + "': " + std::strerror(saved_errno));
    }

    // Close the file descriptor before atomic rename
    close(fd);
    fd = -1;

    // POSIX rename provides atomic replacement semantics
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        throw std::runtime_error("cannot commit registry file '" + path.string() + "': " + error.message());
    }
#endif
}

std::string registry_descriptor(const std::string& id) {
    validate_registry_id(id);
    return std::string(registry_magic) + "id=" + id + "\n";
}

std::string parse_registry_descriptor(std::string_view text) {
    if (!text.starts_with(registry_magic)) throw std::runtime_error("invalid registry descriptor magic");
    text.remove_prefix(registry_magic.size());
    if (!text.ends_with('\n')) throw std::runtime_error("registry descriptor must end with LF");
    text.remove_suffix(1);
    if (!text.starts_with("id=")) throw std::runtime_error("registry descriptor is missing id");
    const std::string id(text.substr(3));
    if (id.find('\n') != std::string::npos || id.find('\r') != std::string::npos) {
        throw std::runtime_error("registry descriptor contains trailing fields");
    }
    validate_registry_id(id);
    return id;
}

std::filesystem::path relative_resource_path(std::string_view resource) {
    if (resource.empty() || resource.front() == '/' || resource.find('\\') != std::string_view::npos) {
        throw std::runtime_error("invalid registry resource path");
    }
    const std::filesystem::path path{std::string(resource)};
    for (const auto& component : path) {
        if (component == "." || component == "..") {
            throw std::runtime_error("registry resource path may not traverse directories");
        }
    }
    return path;
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
    if (amount > buffer->limit - std::min(buffer->limit, buffer->bytes.size())) {
        buffer->exceeded = true;
        return 0;
    }
    buffer->bytes.append(data, amount);
    return amount;
}

std::string https_get(const std::string& url, std::size_t limit) {
    static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("cannot initialize HTTPS registry transport");

    CURL* handle = curl_easy_init();
    if (!handle) throw std::runtime_error("cannot initialize HTTPS registry request");
    CurlBuffer buffer{{}, limit, false};
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
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
    long response = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response);
    curl_easy_cleanup(handle);
    if (buffer.exceeded) throw std::runtime_error("registry response exceeds format size limit");
    if (result != CURLE_OK) {
        throw std::runtime_error("HTTPS registry request failed: " + std::string(curl_easy_strerror(result)));
    }
    if (response < 200 || response >= 300) {
        throw std::runtime_error("HTTPS registry returned status " + std::to_string(response));
    }
    return buffer.bytes;
}
#endif

std::string read_resource(const RegistryEndpoint& endpoint,
                          std::string_view resource,
                          std::uintmax_t limit) {
    const auto relative = relative_resource_path(resource);
    if (endpoint.kind == RegistryTransportKind::File) {
        return read_bounded_file(endpoint.file_root / relative, limit);
    }
#if defined(EMOJINEER_HAVE_CURL)
    return https_get(endpoint.canonical + "/" + relative.generic_string(),
                     static_cast<std::size_t>(limit));
#else
    (void)limit;
    throw std::runtime_error("HTTPS registry transport is unavailable in this build; rebuild with libcurl");
#endif
}

std::string index_resource(const std::string& package_name) {
    validate_package_name(package_name);
    return "v1/packages/" + package_name + ".index";
}

std::string artifact_resource(const std::string& sha256) {
    if (!valid_sha256(sha256)) throw std::runtime_error("registry artifact SHA-256 is malformed");
    return "v1/artifacts/sha256/" + sha256 + ".emjpkg";
}

RegistryPackageIndex parse_package_index(std::string_view text) {
    if (!text.starts_with(index_magic)) throw std::runtime_error("invalid registry package-index magic");
    text.remove_prefix(index_magic.size());
    if (text.empty() || !text.ends_with('\n')) throw std::runtime_error("registry package index must end with LF");

    RegistryPackageIndex index;
    std::istringstream input{std::string(text)};
    std::string line;
    bool have_registry = false;
    bool have_package = false;
    std::string previous_version;
    while (std::getline(input, line)) {
        if (line.empty()) throw std::runtime_error("registry package index contains a blank record");
        if (line.ends_with('\r')) throw std::runtime_error("registry package index must use LF line endings");
        if (!have_registry) {
            if (!line.starts_with("registry=")) throw std::runtime_error("registry package index is missing registry id");
            index.registry_id = line.substr(9);
            validate_registry_id(index.registry_id);
            have_registry = true;
            continue;
        }
        if (!have_package) {
            if (!line.starts_with("package=")) throw std::runtime_error("registry package index is missing package name");
            index.package_name = line.substr(8);
            validate_package_name(index.package_name);
            have_package = true;
            continue;
        }
        if (!line.starts_with("version=")) throw std::runtime_error("registry package index contains an unknown record");
        const auto first_tab = line.find('\t', 8);
        const auto second_tab = first_tab == std::string::npos ? std::string::npos : line.find('\t', first_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos ||
            line.find('\t', second_tab + 1) != std::string::npos) {
            throw std::runtime_error("registry package version record is malformed");
        }
        RegistryVersionRecord record;
        record.version = line.substr(8, first_tab - 8);
        record.content_sha256 = line.substr(first_tab + 1, second_tab - first_tab - 1);
        record.artifact_sha256 = line.substr(second_tab + 1);
        validate_record(record);
        if (!previous_version.empty() && record.version <= previous_version) {
            throw std::runtime_error("registry package versions are not in canonical textual order");
        }
        previous_version = record.version;
        index.versions.push_back(std::move(record));
    }
    if (!have_registry || !have_package) throw std::runtime_error("registry package index is incomplete");
    return index;
}

void validate_artifact_against_record(const PackageArtifact& artifact,
                                      const std::string& package_name,
                                      const RegistryVersionRecord& record) {
    if (artifact.name != package_name) {
        throw std::runtime_error("registry artifact package name does not match selected package");
    }
    if (artifact.version != record.version) {
        throw std::runtime_error("registry artifact version does not match selected version");
    }
    if (artifact.content_sha256 != record.content_sha256) {
        throw std::runtime_error("registry artifact content SHA-256 does not match package index");
    }
    if (artifact.artifact_sha256 != record.artifact_sha256) {
        throw std::runtime_error("registry artifact SHA-256 does not match package index");
    }
}

std::filesystem::path cache_path_for(const RegistryEndpoint& endpoint,
                                     const std::string& registry_id,
                                     const std::string& package_name,
                                     const RegistryVersionRecord& record,
                                     const std::filesystem::path& requested_root) {
    PackageArtifact expected;
    expected.name = package_name;
    expected.version = record.version;
    expected.artifact_sha256 = record.artifact_sha256;
    const auto root = requested_root.empty() ? default_registry_cache_root() : requested_root;
    const auto registry_key = sha256_hex(endpoint.canonical + "\n" + registry_id).substr(0, 32);
    return package_cache_path(root / "registries" / registry_key, expected);
}

std::string lower_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

} // namespace

RegistryEndpoint parse_registry_endpoint(std::string_view raw) {
    if (raw.empty()) throw std::runtime_error("registry endpoint cannot be empty");
    const std::string text(raw);
    if (text.starts_with("http://")) {
        throw std::runtime_error("registry network endpoints must use HTTPS");
    }
    if (text.starts_with("https://")) {
        if (text.find_first_of("?#@\\\r\n\t ") != std::string::npos) {
            throw std::runtime_error("HTTPS registry endpoint contains unsupported URL syntax");
        }
        std::string rest = text.substr(8);
        const auto slash = rest.find('/');
        std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
        std::string path = slash == std::string::npos ? std::string{} : rest.substr(slash);
        if (authority.empty()) throw std::runtime_error("HTTPS registry endpoint is missing a host");
        authority = lower_ascii(authority);
        while (path.size() > 1 && path.back() == '/') path.pop_back();
        std::istringstream components(path);
        std::string component;
        while (std::getline(components, component, '/')) {
            if (component == "." || component == "..") {
                throw std::runtime_error("HTTPS registry endpoint path may not contain '.' or '..' segments");
            }
        }
        return {RegistryTransportKind::Https, "https://" + authority + path, {}};
    }
    if (text.find("://") != std::string::npos && !text.starts_with("file://")) {
        throw std::runtime_error("unsupported registry endpoint scheme");
    }

    std::string path_text = text;
    if (text.starts_with("file://")) path_text = text.substr(7);
    std::error_code error;
    auto root = std::filesystem::absolute(std::filesystem::path(path_text), error).lexically_normal();
    if (error) throw std::runtime_error("cannot normalize file registry endpoint");
    return {RegistryTransportKind::File, "file://" + root.generic_string(), root};
}

std::filesystem::path default_registry_cache_root() {
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(local) / "Emojineer" / "cache";
    }
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        return std::filesystem::path(xdg) / "emojineer";
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cache" / "emojineer";
    }
#endif
    return std::filesystem::temp_directory_path() / "emojineer-cache";
}

void initialize_file_registry(const std::filesystem::path& raw_root,
                              const std::string& registry_id) {
    validate_registry_id(registry_id);
    const auto endpoint = parse_registry_endpoint(raw_root.string());
    if (endpoint.kind != RegistryTransportKind::File) {
        throw std::runtime_error("initialize_file_registry requires a file endpoint, not " + endpoint.canonical);
    }
    const auto descriptor = endpoint.file_root / "v1/registry.txt";
    if (std::filesystem::exists(descriptor)) {
        const auto existing = parse_registry_descriptor(read_bounded_file(descriptor, max_descriptor_bytes));
        if (existing != registry_id) {
            throw std::runtime_error("registry already exists with id '" + existing + "'");
        }
        return;
    }
    std::filesystem::create_directories(endpoint.file_root / "v1/packages");
    std::filesystem::create_directories(endpoint.file_root / "v1/artifacts/sha256");
    write_atomic_file(descriptor, registry_descriptor(registry_id));
}

std::string registry_identity(const RegistryEndpoint& endpoint) {
    return parse_registry_descriptor(read_resource(endpoint, "v1/registry.txt", max_descriptor_bytes));
}

std::string render_registry_package_index(const RegistryPackageIndex& raw_index) {
    RegistryPackageIndex index = raw_index;
    validate_registry_id(index.registry_id);
    validate_package_name(index.package_name);
    for (const auto& record : index.versions) validate_record(record);
    std::sort(index.versions.begin(), index.versions.end(), [](const auto& left, const auto& right) {
        return left.version < right.version;
    });
    for (std::size_t i = 1; i < index.versions.size(); ++i) {
        if (index.versions[i - 1].version == index.versions[i].version) {
            throw std::runtime_error("registry package index contains duplicate version '" + index.versions[i].version + "'");
        }
    }

    std::ostringstream out;
    out << index_magic
        << "registry=" << index.registry_id << '\n'
        << "package=" << index.package_name << '\n';
    for (const auto& record : index.versions) {
        out << "version=" << record.version << '\t'
            << record.content_sha256 << '\t'
            << record.artifact_sha256 << '\n';
    }
    return out.str();
}

RegistryPackageIndex load_registry_package_index(const RegistryEndpoint& endpoint,
                                                 const std::string& package_name) {
    validate_package_name(package_name);
    const auto identity = registry_identity(endpoint);
    auto index = parse_package_index(read_resource(endpoint, index_resource(package_name), max_index_bytes));
    if (index.registry_id != identity) {
        throw std::runtime_error("registry package index identity does not match registry descriptor");
    }
    if (index.package_name != package_name) {
        throw std::runtime_error("registry package index name does not match requested package");
    }
    return index;
}

std::string render_registry_versions(const RegistryPackageIndex& index) {
    std::ostringstream out;
    out << index.package_name << " @ " << index.registry_id << '\n';
    if (index.versions.empty()) {
        out << "  (no versions)\n";
        return out.str();
    }
    for (const auto& record : index.versions) {
        out << "  " << record.version
            << "  content=" << record.content_sha256
            << "  artifact=" << record.artifact_sha256 << '\n';
    }
    return out.str();
}

RegistryPublishResult publish_package_to_registry(const std::filesystem::path& package_root,
                                                  const RegistryEndpoint& endpoint) {
    if (endpoint.kind != RegistryTransportKind::File) {
        throw std::runtime_error("HTTPS publication is not enabled until an authenticated upload contract is defined");
    }
    const auto identity = registry_identity(endpoint);
    const auto bytes = build_package_artifact_bytes(package_root);
    const auto artifact = parse_package_artifact(bytes);
    RegistryVersionRecord record{artifact.version, artifact.content_sha256, artifact.artifact_sha256};
    validate_record(record);

    const auto artifact_path = endpoint.file_root / relative_resource_path(artifact_resource(record.artifact_sha256));
    if (std::filesystem::exists(artifact_path)) {
        const auto existing = load_package_artifact(artifact_path);
        validate_artifact_against_record(existing, artifact.name, record);
    } else {
        write_atomic_file(artifact_path, bytes);
    }

    // Acquire package-scoped interprocess lock for index serialization
    const auto lock_path = endpoint.file_root / "v1/packages" / (artifact.name + ".lock");
    PackageLock lock(lock_path);

    // Reload and revalidate under lock
    const auto index_path = endpoint.file_root / relative_resource_path(index_resource(artifact.name));
    RegistryPackageIndex index{identity, artifact.name, {}};
    if (std::filesystem::exists(index_path)) {
        index = parse_package_index(read_bounded_file(index_path, max_index_bytes));
        if (index.registry_id != identity || index.package_name != artifact.name) {
            throw std::runtime_error("existing registry package index identity is inconsistent");
        }
    }

    auto found = std::find_if(index.versions.begin(), index.versions.end(), [&](const auto& candidate) {
        return candidate.version == record.version;
    });
    if (found != index.versions.end()) {
        if (found->content_sha256 != record.content_sha256 ||
            found->artifact_sha256 != record.artifact_sha256) {
            throw std::runtime_error("immutable registry version conflict for '" + artifact.name + "@" + artifact.version + "'");
        }
        return {record, artifact_path, true};
    }

    index.versions.push_back(record);
    write_atomic_file(index_path, render_registry_package_index(index));
    return {record, artifact_path, false};
}

RegistryFetchResult fetch_registry_package(const RegistryEndpoint& endpoint,
                                           const std::string& package_name,
                                           std::string_view requirement,
                                           const std::filesystem::path& cache_root) {
    validate_package_name(package_name);
    const auto identity = registry_identity(endpoint);
    const auto index = load_registry_package_index(endpoint, package_name);
    std::vector<std::string> versions;
    versions.reserve(index.versions.size());
    for (const auto& record : index.versions) versions.push_back(record.version);
    const auto selected = select_highest_matching_version(versions, requirement);
    if (!selected) {
        throw std::runtime_error("registry has no version of '" + package_name + "' matching '" +
                                 std::string(requirement) + "'");
    }
    const auto found = std::find_if(index.versions.begin(), index.versions.end(), [&](const auto& record) {
        return record.version == *selected;
    });
    if (found == index.versions.end()) throw std::runtime_error("internal registry selection error");
    const RegistryVersionRecord record = *found;
    const auto cache_path = cache_path_for(endpoint, identity, package_name, record, cache_root);

    if (std::filesystem::exists(cache_path)) {
        try {
            auto artifact = load_package_artifact(cache_path);
            validate_artifact_against_record(artifact, package_name, record);
            return {record, std::move(artifact), cache_path, true};
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove(cache_path, ignored);
        }
    }

    const auto bytes = read_resource(endpoint, artifact_resource(record.artifact_sha256), max_artifact_bytes);
    auto artifact = parse_package_artifact(bytes);
    validate_artifact_against_record(artifact, package_name, record);
    write_atomic_file(cache_path, bytes);
    return {record, std::move(artifact), cache_path, false};
}

bool https_registry_transport_available() {
#if defined(EMOJINEER_HAVE_CURL)
    return true;
#else
    return false;
#endif
}

// Protocol version for authenticated publication
constexpr std::string_view authenticated_protocol_version = "emjpub1";

// Get credential from environment variable
std::optional<std::string> credential_from_environment() {
    const char* env_token = std::getenv("EMOJINEER_TOKEN");
    if (env_token && env_token[0] != '\0') {
        return std::string(env_token);
    }
    return std::nullopt;
}

// Parse credential from explicit CLI input
RegistryPublishCredential parse_credential(std::string_view token, std::string_view namespace_id) {
    if (token.empty()) {
        throw std::runtime_error("credential token cannot be empty");
    }
    if (namespace_id.empty()) {
        throw std::runtime_error("namespace cannot be empty");
    }
    // Validate no credentials in URL form
    if (token.find("://") != std::string_view::npos) {
        throw std::runtime_error("credential must not be in URL form");
    }
    // Validate portable name for namespace
    if (!valid_portable_name(namespace_id)) {
        throw std::runtime_error("namespace must be a valid portable name (ASCII letters, digits, '-', '_')");
    }
    return {std::string(token), std::string(namespace_id)};
}

// Parse publication receipt from JSON
PublicationReceipt parse_publication_receipt(std::string_view text) {
    PublicationReceipt receipt;
    // Simple JSON parsing - extract required fields
    // Format: {"registry_id":"...","package_name":"...","version":"...","content_sha256":"...","artifact_sha256":"...","protocol_version":"...","receipt_id":"...","timestamp":"..."}
    
    auto extract_string = [](std::string_view json, std::string_view key) -> std::string {
        const auto key_start = json.find("\"" + std::string(key) + "\"");
        if (key_start == std::string_view::npos) {
            throw std::runtime_error("receipt missing required field: " + std::string(key));
        }
        const auto colon = json.find(":", key_start);
        if (colon == std::string_view::npos) {
            throw std::runtime_error("receipt has malformed field: " + std::string(key));
        }
        const auto quote_start = json.find("\"", colon);
        if (quote_start == std::string_view::npos) {
            throw std::runtime_error("receipt has malformed string value: " + std::string(key));
        }
        const auto quote_end = json.find("\"", quote_start + 1);
        if (quote_end == std::string_view::npos) {
            throw std::runtime_error("receipt has unclosed string value: " + std::string(key));
        }
        return std::string(json.substr(quote_start + 1, quote_end - quote_start - 1));
    };
    
    try {
        receipt.registry_id = extract_string(text, "registry_id");
        receipt.package_name = extract_string(text, "package_name");
        receipt.version = extract_string(text, "version");
        receipt.content_sha256 = extract_string(text, "content_sha256");
        receipt.artifact_sha256 = extract_string(text, "artifact_sha256");
        receipt.protocol_version = extract_string(text, "protocol_version");
        receipt.receipt_id = extract_string(text, "receipt_id");
        receipt.timestamp = extract_string(text, "timestamp");
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("failed to parse publication receipt: ") + e.what());
    }
    return receipt;
}

// Render publication receipt as deterministic JSON
std::string render_publication_receipt(const PublicationReceipt& receipt) {
    // Ensure deterministic ordering: keys sorted alphabetically
    std::ostringstream out;
    out << "{";
    out << "\"artifact_sha256\":\"" << receipt.artifact_sha256 << "\",";
    out << "\"content_sha256\":\"" << receipt.content_sha256 << "\",";
    out << "\"package_name\":\"" << receipt.package_name << "\",";
    out << "\"protocol_version\":\"" << receipt.protocol_version << "\",";
    out << "\"receipt_id\":\"" << receipt.receipt_id << "\",";
    out << "\"registry_id\":\"" << receipt.registry_id << "\",";
    out << "\"timestamp\":\"" << receipt.timestamp << "\",";
    out << "\"version\":\"" << receipt.version << "\"";
    out << "}";
    return out.str();
}

// Verify receipt matches expected values before reporting success
// Note: expected_registry_id can be empty to skip the check (registry ID verification
// may be done separately in the client before any credentialed write)
void verify_publication_receipt(const PublicationReceipt& receipt,
                                 const std::string& expected_registry_id,
                                 const std::string& expected_package_name,
                                 const std::string& expected_version,
                                 const std::string& expected_content_sha256,
                                 const std::string& expected_artifact_sha256) {
    // Skip registry_id check if expected value is empty (client verifies separately)
    if (!expected_registry_id.empty() && receipt.registry_id != expected_registry_id) {
        throw std::runtime_error("receipt registry_id mismatch: expected " + expected_registry_id + ", got " + receipt.registry_id);
    }
    if (receipt.package_name != expected_package_name) {
        throw std::runtime_error("receipt package_name mismatch: expected " + expected_package_name + ", got " + receipt.package_name);
    }
    if (receipt.version != expected_version) {
        throw std::runtime_error("receipt version mismatch: expected " + expected_version + ", got " + receipt.version);
    }
    if (receipt.content_sha256 != expected_content_sha256) {
        throw std::runtime_error("receipt content_sha256 mismatch: expected " + expected_content_sha256 + ", got " + receipt.content_sha256);
    }
    if (receipt.artifact_sha256 != expected_artifact_sha256) {
        throw std::runtime_error("receipt artifact_sha256 mismatch: expected " + expected_artifact_sha256 + ", got " + receipt.artifact_sha256);
    }
    // Verify protocol version
    if (receipt.protocol_version != authenticated_protocol_version) {
        throw std::runtime_error("receipt protocol_version mismatch: expected " + std::string(authenticated_protocol_version) + ", got " + receipt.protocol_version);
    }
}

// Save receipt to file
void save_receipt_file(const std::filesystem::path& path, const PublicationReceipt& receipt) {
    std::filesystem::create_directories(path.parent_path());
    write_atomic_file(path, render_publication_receipt(receipt));
}

#if defined(EMOJINEER_HAVE_CURL)

// Helper to sanitize error messages - redact potential secrets from server responses
// Never include untrusted server response bodies verbatim in auth/protocol error diagnostics
std::string sanitize_error_response(std::string_view response, bool is_auth_error) {
    if (is_auth_error) {
        // For auth errors, don't include server response at all - it could reflect bearer tokens
        return "[redacted for security]";
    }
    // For non-auth errors, truncate to first 200 chars to avoid dumping large responses
    if (response.size() > 200) {
        return std::string(response.substr(0, 200)) + "... [truncated]";
    }
    return std::string(response);
}

// Versioned protocol media types
constexpr std::string_view metadata_media_type = "application/vnd.emojineer.publication.metadata.v1+json";
constexpr std::string_view artifact_media_type = "application/vnd.emojineer.package.v1+octet-stream";
constexpr std::string_view receipt_media_type = "application/vnd.emojineer.publication.receipt.v1+json";
constexpr std::string_view identity_media_type = "application/vnd.emojineer.registry.identity.v1+json";

// Authenticated HTTPS publication client using single bounded multipart request
// Protocol: POST /v1/publish with multipart/form-data containing metadata + artifact
class HttpsPublishClientV2 {
public:
    HttpsPublishClientV2(const std::string& base_url,
                          const std::string& auth_token,
                          const std::string& namespace_id,
                          const std::string& package_name,
                          const std::string& version,
                          const std::string& content_sha256,
                          const std::string& artifact_sha256,
                          const std::string& artifact_body,
                          std::size_t response_limit)
        : base_url_(base_url)
        , auth_token_(auth_token)
        , namespace_id_(namespace_id)
        , package_name_(package_name)
        , version_(version)
        , content_sha256_(content_sha256)
        , artifact_sha256_(artifact_sha256)
        , artifact_body_(artifact_body)
        , response_limit_(response_limit) {}

    // First verify registry identity before any credentialed write
    std::string verify_registry_identity() {
        std::string identity_url = base_url_ + "/v1/identity";
        
        CURL* handle = curl_easy_init();
        if (!handle) {
            throw std::runtime_error("cannot initialize HTTPS registry request");
        }
        
        CurlBuffer response{{}, 4096, false};  // Small limit for identity response
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Accept: " + std::string(identity_media_type)).c_str());
        
        curl_easy_setopt(handle, CURLOPT_URL, identity_url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Emojineer-emji/0.16");
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
        
        const CURLcode result = curl_easy_perform(handle);
        long response_code = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(handle);
        
        if (result != CURLE_OK) {
            throw std::runtime_error("registry identity verification failed: " + std::string(curl_easy_strerror(result)));
        }
        if (response_code != 200) {
            throw std::runtime_error("registry identity endpoint returned status " + std::to_string(response_code));
        }
        if (response.exceeded) {
            throw std::runtime_error("registry identity response exceeds size limit");
        }
        
        // Parse {"registry_id":"..."} from response
        return extract_string(response.bytes, "registry_id");
    }

    PublicationReceipt execute() {
        static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (initialized != CURLE_OK) {
            throw std::runtime_error("cannot initialize HTTPS registry transport");
        }

        // Step 1: Verify registry identity BEFORE any credentialed write
        std::string verified_registry_id = verify_registry_identity();
        
        // Bound the request body - artifact size + metadata overhead
        constexpr std::uintmax_t max_artifact_size = 128ULL * 1024ULL * 1024ULL;  // 128MB
        if (artifact_body_.size() > max_artifact_size) {
            throw std::runtime_error("artifact size exceeds maximum allowed (" + std::to_string(max_artifact_size) + " bytes)");
        }

        // Step 2: Build multipart request with metadata + artifact
        // Generate unique boundary
        std::string boundary = "----EmjPubBoundary";
        boundary += std::to_string(static_cast<std::uint64_t>(std::time(nullptr)));
        boundary += ".";
        boundary += std::to_string(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)));
        
        // Build multipart body
        std::string multipart_body;
        multipart_body.reserve(1024 + artifact_body_.size());
        
        // Part 1: metadata JSON
        multipart_body += "--" + boundary + "\r\n";
        multipart_body += "Content-Type: " + std::string(metadata_media_type) + "\r\n";
        multipart_body += "Content-Disposition: form-data; name=\"metadata\"\r\n\r\n";
        multipart_body += "{";
        multipart_body += "\"namespace\":\"" + namespace_id_ + "\",";
        multipart_body += "\"package\":\"" + package_name_ + "\",";
        multipart_body += "\"version\":\"" + version_ + "\",";
        multipart_body += "\"content_sha256\":\"" + content_sha256_ + "\",";
        multipart_body += "\"artifact_sha256\":\"" + artifact_sha256_ + "\"";
        multipart_body += "}\r\n";
        
        // Part 2: artifact binary
        multipart_body += "--" + boundary + "\r\n";
        multipart_body += "Content-Type: " + std::string(artifact_media_type) + "\r\n";
        multipart_body += "Content-Disposition: form-data; name=\"artifact\"; filename=\"" + package_name_ + ".emjpkg\"\r\n\r\n";
        multipart_body += artifact_body_;
        multipart_body += "\r\n";
        
        // Final boundary
        multipart_body += "--" + boundary + "--\r\n";
        
        std::string content_type = "multipart/form-data; boundary=" + boundary;
        std::string publish_url = base_url_ + "/v1/publish";
        
        CURL* handle = curl_easy_init();
        if (!handle) {
            throw std::runtime_error("cannot initialize HTTPS registry request");
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
        headers = curl_slist_append(headers, ("Authorization: Bearer " + auth_token_).c_str());
        headers = curl_slist_append(headers, ("Accept: " + std::string(receipt_media_type)).c_str());
        
        CurlBuffer response{{}, response_limit_, false};
        
        curl_easy_setopt(handle, CURLOPT_URL, publish_url.c_str());
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, multipart_body.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, multipart_body.size());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, curl_write);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        
        // Security settings: no redirects, TLS verification, timeouts, bounded upload
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 300L);  // 5 min for large uploads
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Emojineer-emji/0.16");
        
        // Bound request body size
        curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(max_artifact_size + 8192));
        
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif

        const CURLcode result = curl_easy_perform(handle);
        long response_code = 0;
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
        curl_slist_free_all(headers);
        
        if (response.exceeded) {
            curl_easy_cleanup(handle);
            throw std::runtime_error("publication response exceeds size limit");
        }
        if (result != CURLE_OK) {
            curl_easy_cleanup(handle);
            throw std::runtime_error("HTTPS publication request failed: " + std::string(curl_easy_strerror(result)));
        }
        
        std::string response_bytes = response.bytes;
        curl_easy_cleanup(handle);
        
        // Check response code - sanitize error messages to prevent secret leakage
        if (response_code == 401) {
            throw std::runtime_error("publication authentication failed: invalid or missing credential");
        }
        if (response_code == 403) {
            throw std::runtime_error("publication authorization failed: namespace '" + namespace_id_ + "' not owned by credential");
        }
        if (response_code == 409) {
            throw std::runtime_error("publication conflict: version " + version_ + " already exists with different content");
        }
        if (response_code == 400) {
            throw std::runtime_error("publication request rejected: " + sanitize_error_response(response_bytes, false));
        }
        if (response_code < 200 || response_code >= 300) {
            throw std::runtime_error("HTTPS publication returned status " + std::to_string(response_code) + ": " + sanitize_error_response(response_bytes, false));
        }
        
        // Parse receipt from response
        PublicationReceipt receipt = parse_publication_receipt(response_bytes);
        
        // Verify receipt matches verified registry identity
        if (receipt.registry_id != verified_registry_id) {
            throw std::runtime_error("receipt registry_id mismatch: expected " + verified_registry_id + ", got " + receipt.registry_id);
        }
        
        return receipt;
    }

private:
    std::string base_url_;
    std::string auth_token_;
    std::string namespace_id_;
    std::string package_name_;
    std::string version_;
    std::string content_sha256_;
    std::string artifact_sha256_;
    std::string artifact_body_;
    std::size_t response_limit_;
};

#endif // EMOJINEER_HAVE_CURL

// Authenticated HTTPS publication
PublicationReceipt publish_package_to_https_registry(
    const std::filesystem::path& package_root,
    const RegistryEndpoint& endpoint,
    const RegistryPublishCredential& credential) {
    
    if (endpoint.kind != RegistryTransportKind::Https) {
        throw std::runtime_error("authenticated publication requires HTTPS endpoint");
    }
    
#if defined(EMOJINEER_HAVE_CURL)
    // Build the package artifact
    const auto bytes = build_package_artifact_bytes(package_root);
    const auto artifact = parse_package_artifact(bytes);
    
    // Validate artifact
    validate_record({artifact.version, artifact.content_sha256, artifact.artifact_sha256});
    
    // Use the new client to publish
    constexpr std::size_t max_response_size = 16 * 1024;
    HttpsPublishClientV2 client(
        endpoint.canonical,
        credential.token,
        credential.namespace_id,
        artifact.name,
        artifact.version,
        artifact.content_sha256,
        artifact.artifact_sha256,
        bytes,
        max_response_size
    );
    
    PublicationReceipt receipt = client.execute();
    
    // Verify receipt matches our expectations
    verify_publication_receipt(
        receipt,
        "",  // Registry ID will be verified by the server identity
        artifact.name,
        artifact.version,
        artifact.content_sha256,
        artifact.artifact_sha256
    );
    
    return receipt;
#else
    (void)package_root;
    (void)endpoint;
    (void)credential;
    throw std::runtime_error("HTTPS publication requires libcurl support (not built)");
#endif
}

} // namespace emojineer
