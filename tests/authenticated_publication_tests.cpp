#include "emojineer/project.hpp"
#include "emojineer/registry_transport.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("authenticated publication test failed: " + message);
}

template <typename Function>
std::string require_failure(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    throw std::runtime_error("authenticated publication test failed: expected failure: " + message);
}

struct TempRoot {
    std::filesystem::path path;
    explicit TempRoot(const std::string& suffix) {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-authpub-" + suffix + "-" + std::to_string(nonce));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write authenticated publication test file");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed while writing authenticated publication test file");
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read authenticated publication test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void set_version(const std::filesystem::path& package_root, const std::string& version) {
    auto manifest = emojineer::load_project_manifest(package_root / "emojineer.toml");
    manifest.version = version;
    write_bytes(package_root / "emojineer.toml", emojineer::canonical_manifest_text(manifest));
}

std::optional<std::string> header_value(
    const emojineer::publication_protocol::HttpRequest& request,
    const std::string& name) {
    const auto found = std::find_if(request.headers.begin(), request.headers.end(),
        [&](const auto& header) { return header.first == name; });
    if (found == request.headers.end()) return std::nullopt;
    return found->second;
}

void set_header(emojineer::publication_protocol::HttpRequest& request,
                const std::string& name,
                const std::string& value) {
    for (auto& header : request.headers) {
        if (header.first == name) {
            header.second = value;
            return;
        }
    }
    request.headers.emplace_back(name, value);
}

class PublicationFixture {
public:
    explicit PublicationFixture(const std::filesystem::path& root)
        : root_(root) {
        emojineer::initialize_file_registry(root_, registry_id);
    }

    static constexpr const char* registry_id = "fixture.dev";
    static constexpr const char* good_token = "fixture-secret-token";
    static constexpr const char* owned_namespace = "acme";

    std::size_t request_limit = emojineer::publication_protocol::max_upload_bytes;
    bool tamper_receipt = false;
    bool wrong_media_type = false;

    emojineer::RegistryEndpoint file_endpoint() const {
        return emojineer::parse_registry_endpoint(root_.string());
    }

    emojineer::publication_protocol::HttpResponse handle(
        const emojineer::publication_protocol::HttpRequest& request) {
        using namespace emojineer::publication_protocol;

        if (request.body.size() > request_limit) {
            return {413, std::string(receipt_media_type), "request too large"};
        }
        if (header_value(request, "Authorization") !=
            std::optional<std::string>(std::string("Bearer ") + good_token)) {
            return {401, std::string(receipt_media_type), "invalid credential"};
        }
        if (header_value(request, "X-Emojineer-Namespace") !=
            std::optional<std::string>(owned_namespace)) {
            return {403, std::string(receipt_media_type), "namespace not owned"};
        }
        if (header_value(request, "Content-Type") !=
            std::optional<std::string>(std::string(request_media_type)) ||
            header_value(request, "Accept") !=
            std::optional<std::string>(std::string(receipt_media_type)) ||
            header_value(request, "X-Emojineer-Protocol") !=
            std::optional<std::string>(std::string(version))) {
            return {400, std::string(receipt_media_type), "protocol media type mismatch"};
        }

        emojineer::PackageArtifact artifact;
        try {
            artifact = emojineer::parse_package_artifact(request.body);
        } catch (...) {
            return {400, std::string(receipt_media_type), "invalid package artifact"};
        }

        if (header_value(request, "X-Emojineer-Package") !=
                std::optional<std::string>(artifact.name) ||
            header_value(request, "X-Emojineer-Version") !=
                std::optional<std::string>(artifact.version) ||
            header_value(request, "X-Emojineer-Content-SHA256") !=
                std::optional<std::string>(artifact.content_sha256) ||
            header_value(request, "X-Emojineer-Artifact-SHA256") !=
                std::optional<std::string>(artifact.artifact_sha256)) {
            return {400, std::string(receipt_media_type), "immutable identity mismatch"};
        }

        const std::string key = artifact.name + "@" + artifact.version;
        const auto existing = stored_.find(key);
        if (existing != stored_.end()) {
            if (existing->second.artifact_sha256 != artifact.artifact_sha256 ||
                existing->second.content_sha256 != artifact.content_sha256) {
                return {409, std::string(receipt_media_type), "immutable version conflict"};
            }
            auto receipt = receipts_.at(key);
            if (tamper_receipt) receipt.artifact_sha256 = std::string(64, '0');
            return {200,
                    wrong_media_type ? "application/json" : std::string(receipt_media_type),
                    emojineer::render_publication_receipt(receipt)};
        }

        emojineer::RegistryVersionRecord record{
            artifact.version, artifact.content_sha256, artifact.artifact_sha256};
        stored_[key] = record;
        records_[artifact.name].push_back(record);

        const auto artifact_path = root_ / "v1/artifacts/sha256" /
                                   (artifact.artifact_sha256 + ".emjpkg");
        write_bytes(artifact_path, request.body);
        const emojineer::RegistryPackageIndex index{
            registry_id, artifact.name, records_.at(artifact.name)};
        write_bytes(root_ / "v1/packages" / (artifact.name + ".index"),
                    emojineer::render_registry_package_index(index));

        emojineer::PublicationReceipt receipt;
        receipt.registry_id = registry_id;
        receipt.package_name = artifact.name;
        receipt.version = artifact.version;
        receipt.content_sha256 = artifact.content_sha256;
        receipt.artifact_sha256 = artifact.artifact_sha256;
        receipt.protocol_version = std::string(version);
        receipt.receipt_id = "fixture-" + artifact.artifact_sha256.substr(0, 20);
        receipt.timestamp = "2026-08-25T05:00:00Z";
        receipts_[key] = receipt;

        if (tamper_receipt) receipt.artifact_sha256 = std::string(64, '0');
        return {201,
                wrong_media_type ? "application/json" : std::string(receipt_media_type),
                emojineer::render_publication_receipt(receipt)};
    }

private:
    std::filesystem::path root_;
    std::map<std::string, emojineer::RegistryVersionRecord> stored_;
    std::map<std::string, std::vector<emojineer::RegistryVersionRecord>> records_;
    std::map<std::string, emojineer::PublicationReceipt> receipts_;
};

void test_credentials_reject_header_injection_and_environment_roundtrip() {
#ifdef _WIN32
    _putenv_s("EMOJINEER_TOKEN", "environment-token");
#else
    setenv("EMOJINEER_TOKEN", "environment-token", 1);
#endif
    const auto environment = emojineer::credential_from_environment();
    require(environment && *environment == "environment-token",
            "environment credential should round-trip without serialization");
#ifdef _WIN32
    _putenv_s("EMOJINEER_TOKEN", "");
#else
    unsetenv("EMOJINEER_TOKEN");
#endif

    const auto valid = emojineer::parse_credential("opaque-token_123", "acme");
    require(valid.namespace_id == "acme", "valid namespace should be retained");

    (void)require_failure(
        [] { (void)emojineer::parse_credential("token\r\nX-Evil: yes", "acme"); },
        "CR/LF header injection must be rejected");
    (void)require_failure(
        [] { (void)emojineer::parse_credential("https://user:secret@example.test", "acme"); },
        "credential-in-URL form must be rejected");
}

void test_receipt_json_is_strict_deterministic_and_tamper_checked() {
    emojineer::PublicationReceipt receipt{
        "fixture.dev",
        "spark",
        "1.2.3",
        std::string(64, 'a'),
        std::string(64, 'b'),
        "emjpub1",
        "receipt-123",
        "2026-08-25T05:00:00Z",
    };
    const auto rendered = emojineer::render_publication_receipt(receipt);
    require(rendered.starts_with("{\"artifact_sha256\":"),
            "receipt field ordering should be deterministic");
    const auto parsed = emojineer::parse_publication_receipt(rendered);
    emojineer::verify_publication_receipt(parsed,
                                          receipt.registry_id,
                                          receipt.package_name,
                                          receipt.version,
                                          receipt.content_sha256,
                                          receipt.artifact_sha256);

    (void)require_failure(
        [&] { (void)emojineer::parse_publication_receipt(rendered + " garbage"); },
        "trailing receipt data must be rejected");
    const std::string duplicate =
        "{\"registry_id\":\"fixture.dev\",\"registry_id\":\"other.dev\"}";
    (void)require_failure(
        [&] { (void)emojineer::parse_publication_receipt(duplicate); },
        "duplicate receipt keys must be rejected");

    auto tampered = receipt;
    tampered.artifact_sha256 = std::string(64, 'c');
    (void)require_failure(
        [&] {
            emojineer::verify_publication_receipt(tampered,
                                                  receipt.registry_id,
                                                  receipt.package_name,
                                                  receipt.version,
                                                  receipt.content_sha256,
                                                  receipt.artifact_sha256);
        },
        "tampered receipt identity must be rejected");
}

void test_authenticated_protocol_acceptance_journey() {
    TempRoot root("journey");
    const auto package_root = root.path / "package";
    const auto registry_root = root.path / "registry";
    const auto cache_root = root.path / "cache";
    emojineer::initialize_project(package_root, "spark");
    set_version(package_root, "1.2.0");
    write_bytes(package_root / "src/main.emoji", "📝 📜authenticated spark📜\n");

    PublicationFixture fixture(registry_root);
    const auto endpoint = emojineer::parse_registry_endpoint("https://registry.example.test/api");
    const auto credential = emojineer::parse_credential(PublicationFixture::good_token,
                                                         PublicationFixture::owned_namespace);

    bool identity_checked = false;
    const emojineer::publication_protocol::IdentityLookup identity =
        [&](const emojineer::RegistryEndpoint& target) {
            require(target.canonical == endpoint.canonical,
                    "identity lookup should target the selected registry");
            identity_checked = true;
            return std::string(PublicationFixture::registry_id);
        };
    const emojineer::publication_protocol::Exchange exchange =
        [&](const emojineer::publication_protocol::HttpRequest& request) {
            require(identity_checked,
                    "registry identity must be checked before the request carries authorization");
            require(request.url == endpoint.canonical + "/v1/publish",
                    "publication must use the versioned registry publish resource");
            require(!request.body.empty(),
                    "authenticated request must contain the actual immutable .emjpkg bytes");
            return fixture.handle(request);
        };

    const auto first = emojineer::publication_protocol::publish_with_transport(
        package_root, endpoint, credential, identity, exchange);
    require(first.registry_id == PublicationFixture::registry_id,
            "receipt should bind verified registry identity");

    // The fixture stores the exact uploaded artifact in registry layout. Fetch it
    // through the already-shipped verified read path to prove round-trip identity.
    const auto fetched = emojineer::fetch_registry_package(
        fixture.file_endpoint(), "spark", "1.2.0", cache_root);
    require(fetched.record.artifact_sha256 == first.artifact_sha256,
            "verified fetch should return the exact authenticated upload artifact");
    require(fetched.record.content_sha256 == first.content_sha256,
            "verified fetch should return the exact authenticated upload content identity");

    identity_checked = false;
    const auto second = emojineer::publication_protocol::publish_with_transport(
        package_root, endpoint, credential, identity, exchange);
    require(second.receipt_id == first.receipt_id,
            "identical republish should be idempotent and retain its server receipt identity");

    // Same semantic version with different immutable bytes must conflict.
    write_bytes(package_root / "src/main.emoji", "📝 📜different bytes, same version📜\n");
    identity_checked = false;
    const auto conflict = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, credential, identity, exchange);
        },
        "same-version conflicting content must fail");
    require(conflict.find("409") != std::string::npos,
            "immutable conflict should remain a concrete protocol failure");
}

void test_auth_ownership_checksum_bounds_and_secret_redaction() {
    TempRoot root("negative");
    const auto package_root = root.path / "package";
    emojineer::initialize_project(package_root, "guarded");
    set_version(package_root, "2.0.0");
    PublicationFixture fixture(root.path / "registry");
    const auto endpoint = emojineer::parse_registry_endpoint("https://registry.example.test");

    const auto identity = [](const emojineer::RegistryEndpoint&) {
        return std::string(PublicationFixture::registry_id);
    };

    const auto bad_auth = emojineer::parse_credential("wrong-token", "acme");
    const auto auth_error = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, bad_auth, identity,
                [&](const auto& request) { return fixture.handle(request); });
        },
        "invalid token must fail");
    require(auth_error.find("401") != std::string::npos &&
                auth_error.find("wrong-token") == std::string::npos,
            "authentication diagnostic must not disclose credential text");

    const auto wrong_namespace = emojineer::parse_credential(
        PublicationFixture::good_token, "someone_else");
    const auto namespace_error = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, wrong_namespace, identity,
                [&](const auto& request) { return fixture.handle(request); });
        },
        "wrong namespace must fail");
    require(namespace_error.find("403") != std::string::npos,
            "namespace ownership should surface as HTTP 403");

    const auto good = emojineer::parse_credential(
        PublicationFixture::good_token, PublicationFixture::owned_namespace);
    const auto checksum_error = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, good, identity,
                [&](const auto& original) {
                    auto request = original;
                    set_header(request, "X-Emojineer-Artifact-SHA256", std::string(64, '0'));
                    return fixture.handle(request);
                });
        },
        "server fixture must reject a mismatched artifact checksum header");
    require(checksum_error.find("400") != std::string::npos,
            "checksum mismatch should be a concrete bad-request failure");

    fixture.request_limit = 8;
    const auto bounded = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, good, identity,
                [&](const auto& request) { return fixture.handle(request); });
        },
        "fixture request bound must reject oversized uploads");
    require(bounded.find("413") != std::string::npos,
            "bounded request rejection should surface as HTTP 413");
    fixture.request_limit = emojineer::publication_protocol::max_upload_bytes;

    const auto response_bound = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, good, identity,
                [&](const auto&) {
                    return emojineer::publication_protocol::HttpResponse{
                        200,
                        std::string(emojineer::publication_protocol::receipt_media_type),
                        std::string(emojineer::publication_protocol::max_receipt_bytes + 1, 'x')};
                });
        },
        "client response bound must reject oversized receipts");
    require(response_bound.find("size limit") != std::string::npos,
            "oversized response should fail before parsing");

    const auto echoed_secret = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, good, identity,
                [&](const auto&) {
                    return emojineer::publication_protocol::HttpResponse{
                        400, "text/plain",
                        std::string("echo Authorization: Bearer ") + PublicationFixture::good_token};
                });
        },
        "server-controlled error bodies must not leak into diagnostics");
    require(echoed_secret.find(PublicationFixture::good_token) == std::string::npos &&
                echoed_secret.find("Authorization") == std::string::npos,
            "normal diagnostics must not echo server-controlled secret-bearing bodies");
}

void test_identity_preflight_receipt_tamper_media_type_and_receipt_file() {
    TempRoot root("preflight");
    const auto package_root = root.path / "package";
    emojineer::initialize_project(package_root, "signal");
    set_version(package_root, "3.0.0");
    PublicationFixture fixture(root.path / "registry");
    const auto endpoint = emojineer::parse_registry_endpoint("https://registry.example.test");
    const auto credential = emojineer::parse_credential(
        PublicationFixture::good_token, PublicationFixture::owned_namespace);

    bool exchange_called = false;
    (void)require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root,
                endpoint,
                credential,
                [](const auto&) -> std::string {
                    throw std::runtime_error("identity descriptor mismatch");
                },
                [&](const auto&) {
                    exchange_called = true;
                    return emojineer::publication_protocol::HttpResponse{};
                });
        },
        "identity discovery failure must stop publication");
    require(!exchange_called,
            "authorization-bearing exchange must not run when registry identity preflight fails");

    fixture.tamper_receipt = true;
    const auto tamper = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, credential,
                [](const auto&) { return std::string(PublicationFixture::registry_id); },
                [&](const auto& request) { return fixture.handle(request); });
        },
        "tampered receipt must fail client verification");
    require(tamper.find("artifact identity mismatch") != std::string::npos,
            "tampered receipt should fail exact immutable identity binding");

    fixture.tamper_receipt = false;
    set_version(package_root, "3.1.0");
    fixture.wrong_media_type = true;
    const auto media = require_failure(
        [&] {
            (void)emojineer::publication_protocol::publish_with_transport(
                package_root, endpoint, credential,
                [](const auto&) { return std::string(PublicationFixture::registry_id); },
                [&](const auto& request) { return fixture.handle(request); });
        },
        "unexpected response media type must fail");
    require(media.find("media type") != std::string::npos,
            "response media type is part of the protocol contract");

    fixture.wrong_media_type = false;
    set_version(package_root, "3.2.0");
    const auto receipt = emojineer::publication_protocol::publish_with_transport(
        package_root, endpoint, credential,
        [](const auto&) { return std::string(PublicationFixture::registry_id); },
        [&](const auto& request) { return fixture.handle(request); });
    const auto receipt_path = root.path / "receipt.json";
    emojineer::save_receipt_file(receipt_path, receipt);
    const auto saved = read_bytes(receipt_path);
    require(saved == emojineer::render_publication_receipt(receipt),
            "receipt file should contain only deterministic verified receipt JSON");
    require(saved.find(PublicationFixture::good_token) == std::string::npos,
            "receipt files must never serialize credentials");
}

void test_file_registry_publication_remains_unchanged() {
    TempRoot root("file-regression");
    const auto package_root = root.path / "package";
    const auto registry_root = root.path / "registry";
    emojineer::initialize_project(package_root, "localpkg");
    set_version(package_root, "1.0.0");
    emojineer::initialize_file_registry(registry_root, "local.dev");
    const auto endpoint = emojineer::parse_registry_endpoint(registry_root.string());

    const auto first = emojineer::publish_package_to_registry(package_root, endpoint);
    require(!first.already_present, "local publication should still create the immutable version");
    const auto second = emojineer::publish_package_to_registry(package_root, endpoint);
    require(second.already_present, "local publication should remain idempotent without credentials");
}

} // namespace

int main() {
    try {
        test_credentials_reject_header_injection_and_environment_roundtrip();
        test_receipt_json_is_strict_deterministic_and_tamper_checked();
        test_authenticated_protocol_acceptance_journey();
        test_auth_ownership_checksum_bounds_and_secret_redaction();
        test_identity_preflight_receipt_tamper_media_type_and_receipt_file();
        test_file_registry_publication_remains_unchanged();
        std::cout << "authenticated publication tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
