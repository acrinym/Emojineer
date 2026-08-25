#include "emojineer/package.hpp"
#include "emojineer/package_artifact.hpp"
#include "emojineer/package_report.hpp"
#include "emojineer/project.hpp"
#include "emojineer/registry_transport.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr
        << "emji 0.15\n"
        << "usage:\n"
        << "  emji init <directory> [--name project_name]\n"
        << "  emji check [directory]\n"
        << "  emji lock [directory]\n"
        << "  emji show [directory]\n"
        << "  emji tree [directory] [--hashes] [--json]\n"
        << "  emji sync [directory] [--offline] [--cache directory]\n"
        << "  emji pack [directory] [-o package.emjpkg]\n"
        << "  emji artifact <package.emjpkg>\n"
        << "  emji verify-artifact <package.emjpkg>\n"
        << "  emji registry-init <directory> --id <registry_id>\n"
        << "  emji registry-info --registry <endpoint>\n"
        << "  emji versions <package_name> --registry <endpoint>\n"
        << "  emji publish [directory] --registry <endpoint>\n"
        << "  emji fetch <package_name> <requirement> --registry <endpoint> [--cache directory]\n"
        << "  emji add <package_name> <relative_path> [directory]\n"
        << "  emji add <package_name> <requirement> --registry <endpoint> [--registry-name <alias>] [directory]\n"
        << "  emji remove <package_name> [directory]\n";
}

std::filesystem::path optional_root(int argc, char** argv) {
    if (argc >= 3) return std::filesystem::path(argv[2]);
    return std::filesystem::current_path();
}

struct TreeOptions {
    std::filesystem::path root = std::filesystem::current_path();
    bool include_hashes = false;
    bool json = false;
};

TreeOptions parse_tree_options(int argc, char** argv) {
    TreeOptions options;
    bool have_root = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--hashes") {
            options.include_hashes = true;
            continue;
        }
        if (arg == "--json") {
            options.json = true;
            continue;
        }
        if (arg.rfind("--", 0) == 0) {
            throw std::runtime_error("unknown tree option '" + arg + "'");
        }
        if (have_root) {
            throw std::runtime_error("tree accepts at most one project directory");
        }
        options.root = std::filesystem::path(arg);
        have_root = true;
    }
    return options;
}

struct PackOptions {
    std::filesystem::path root = std::filesystem::current_path();
    std::optional<std::filesystem::path> output;
};

PackOptions parse_pack_options(int argc, char** argv) {
    PackOptions options;
    bool have_root = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o") {
            if (options.output) throw std::runtime_error("pack accepts only one -o output");
            if (++i >= argc) throw std::runtime_error("-o requires an artifact path");
            options.output = std::filesystem::path(argv[i]);
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            throw std::runtime_error("unknown pack option '" + arg + "'");
        }
        if (have_root) throw std::runtime_error("pack accepts at most one project directory");
        options.root = std::filesystem::path(arg);
        have_root = true;
    }
    return options;
}

struct PublishOptions {
    std::filesystem::path root = std::filesystem::current_path();
    std::optional<std::string> registry;
};

PublishOptions parse_publish_options(int argc, char** argv) {
    PublishOptions options;
    bool have_root = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--registry") {
            if (options.registry) throw std::runtime_error("publish accepts only one --registry endpoint");
            if (++i >= argc) throw std::runtime_error("--registry requires an endpoint");
            options.registry = argv[i];
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            throw std::runtime_error("unknown publish option '" + arg + "'");
        }
        if (have_root) throw std::runtime_error("publish accepts at most one project directory");
        options.root = std::filesystem::path(arg);
        have_root = true;
    }
    if (!options.registry) throw std::runtime_error("publish requires --registry <endpoint>");
    return options;
}

std::string registry_option(int argc, char** argv, int start, const std::string& command) {
    std::optional<std::string> registry;
    for (int i = start; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg != "--registry") throw std::runtime_error("unknown " + command + " option '" + arg + "'");
        if (registry) throw std::runtime_error(command + " accepts only one --registry endpoint");
        if (++i >= argc) throw std::runtime_error("--registry requires an endpoint");
        registry = argv[i];
    }
    if (!registry) throw std::runtime_error(command + " requires --registry <endpoint>");
    return *registry;
}

struct FetchOptions {
    std::string registry;
    std::filesystem::path cache;
};

FetchOptions parse_fetch_options(int argc, char** argv) {
    std::optional<std::string> registry;
    std::optional<std::filesystem::path> cache;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--registry") {
            if (registry) throw std::runtime_error("fetch accepts only one --registry endpoint");
            if (++i >= argc) throw std::runtime_error("--registry requires an endpoint");
            registry = argv[i];
            continue;
        }
        if (arg == "--cache") {
            if (cache) throw std::runtime_error("fetch accepts only one --cache directory");
            if (++i >= argc) throw std::runtime_error("--cache requires a directory");
            cache = std::filesystem::path(argv[i]);
            continue;
        }
        throw std::runtime_error("unknown fetch option '" + arg + "'");
    }
    if (!registry) throw std::runtime_error("fetch requires --registry <endpoint>");
    return {*registry, cache.value_or(std::filesystem::path{})};
}

emojineer::PackageGraphReport package_report(const std::filesystem::path& root) {
    try {
        return emojineer::build_package_graph_report(emojineer::resolve_package_graph(root));
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("package graph: ") + error.what());
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage();
            return 2;
        }

        const std::string command = argv[1];
        if (command == "init") {
            if (argc < 3) throw std::runtime_error("init requires a target directory");
            const std::filesystem::path root = argv[2];
            std::optional<std::string> name;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--name") {
                    if (++i >= argc) throw std::runtime_error("--name requires a project name");
                    name = argv[i];
                } else {
                    throw std::runtime_error("unknown init option '" + arg + "'");
                }
            }
            std::string project_name = name.value_or(root.filename().string());
            if (project_name.empty() || project_name == ".") project_name = "emojineer-project";
            emojineer::initialize_project(root, project_name);
            const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
            emojineer::write_project_lock(root, manifest);
            std::cout << "✅ initialized " << manifest.name << " in " << root.string() << '\n';
            return 0;
        }

        if (command == "check") {
            if (argc > 3) throw std::runtime_error("check accepts at most one project directory");
            const auto root = optional_root(argc, argv);
            const auto diagnostics = emojineer::check_project(root);
            if (diagnostics.empty()) {
                const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
                std::cout << "✅ " << manifest.name << " project is valid\n";
                return 0;
            }
            for (const auto& diagnostic : diagnostics) {
                std::cout << root.string() << ": " << diagnostic.message << '\n';
            }
            return 1;
        }

        if (command == "lock") {
            if (argc > 3) throw std::runtime_error("lock accepts at most one project directory");
            const auto root = optional_root(argc, argv);
            const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
            emojineer::write_project_lock(root, manifest);
            std::cout << "✅ wrote " << (root / "emojineer.lock").string() << '\n';
            return 0;
        }

        if (command == "show") {
            if (argc > 3) throw std::runtime_error("show accepts at most one project directory");
            const auto root = optional_root(argc, argv);
            const auto manifest = emojineer::load_project_manifest(root / "emojineer.toml");
            std::cout << "name: " << manifest.name << '\n'
                      << "version: " << manifest.version << '\n'
                      << "entry: " << manifest.entry.generic_string() << '\n'
                      << "manifest-hash: " << emojineer::project_manifest_hash(manifest) << '\n';
            
            // Show registries
            if (!manifest.registries.empty()) {
                std::cout << "registries:\n";
                for (const auto& registry : manifest.registries) {
                    std::cout << "  " << registry.alias << " = " << registry.endpoint << '\n';
                }
            }
            
            // Show dependencies
            if (manifest.dependencies.empty()) {
                std::cout << "dependencies: (none)\n";
            } else {
                std::cout << "dependencies:\n";
                for (const auto& dependency : manifest.dependencies) {
                    if (dependency.kind == emojineer::DependencyKind::Registry) {
                        std::cout << "  " << dependency.name << " -> registry:" 
                                  << dependency.registry_alias << ":" << dependency.requirement << '\n';
                    } else {
                        std::cout << "  " << dependency.name << " -> "
                                  << dependency.path.generic_string() << '\n';
                    }
                }
            }
            return 0;
        }

        if (command == "tree") {
            const auto options = parse_tree_options(argc, argv);
            const auto report = package_report(options.root);
            if (options.json) {
                std::cout << emojineer::render_package_graph_json(report);
            } else {
                std::cout << emojineer::render_package_tree(report, options.include_hashes);
            }
            return 0;
        }

        if (command == "sync") {
            // Parse sync options
            std::filesystem::path root = std::filesystem::current_path();
            std::filesystem::path cache_root;
            bool offline = false;
            
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--offline") {
                    offline = true;
                } else if (arg == "--cache") {
                    if (++i >= argc) throw std::runtime_error("--cache requires a directory");
                    cache_root = std::filesystem::path(argv[i]);
                } else if (arg.rfind("--", 0) == 0) {
                    throw std::runtime_error("unknown sync option '" + arg + "'");
                } else if (i == 2) {
                    root = std::filesystem::path(arg);
                } else {
                    throw std::runtime_error("sync accepts at most one project directory");
                }
            }
            
            if (cache_root.empty()) {
                emojineer::sync_project(root, offline);
            } else {
                emojineer::sync_project(root, cache_root, offline);
            }
            std::cout << "✅ synced project in " << root.string() << '\n';
            return 0;
        }

        if (command == "pack") {
            const auto options = parse_pack_options(argc, argv);
            const auto manifest = emojineer::load_project_manifest(options.root / "emojineer.toml");
            const auto output = options.output.value_or(
                options.root / emojineer::default_package_artifact_filename(manifest.name,
                                                                            manifest.version));
            emojineer::write_package_artifact(options.root, output);
            const auto artifact = emojineer::load_package_artifact(output);
            std::cout << "✅ wrote " << output.string() << '\n'
                      << "artifact-sha256: " << artifact.artifact_sha256 << '\n';
            return 0;
        }

        if (command == "artifact") {
            if (argc != 3) throw std::runtime_error("artifact requires exactly one .emjpkg path");
            const auto artifact = emojineer::load_package_artifact(argv[2]);
            std::cout << emojineer::render_package_artifact_summary(artifact);
            return 0;
        }

        if (command == "verify-artifact") {
            if (argc != 3) throw std::runtime_error("verify-artifact requires exactly one .emjpkg path");
            const auto artifact = emojineer::load_package_artifact(argv[2]);
            std::cout << "✅ " << argv[2] << " is a valid Emojineer package artifact\n"
                      << "content-sha256: " << artifact.content_sha256 << '\n'
                      << "artifact-sha256: " << artifact.artifact_sha256 << '\n';
            return 0;
        }

        if (command == "registry-init") {
            if (argc < 5) throw std::runtime_error("registry-init requires <directory> --id <registry_id>");
            const std::filesystem::path root = argv[2];
            std::optional<std::string> id;
            for (int i = 3; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--id") {
                    if (id) throw std::runtime_error("registry-init accepts only one --id");
                    if (++i >= argc) throw std::runtime_error("--id requires a registry id");
                    id = argv[i];
                } else {
                    throw std::runtime_error("unknown registry-init option '" + arg + "'");
                }
            }
            if (!id) throw std::runtime_error("registry-init requires --id <registry_id>");
            emojineer::initialize_file_registry(root, *id);
            const auto endpoint = emojineer::parse_registry_endpoint(root.string());
            std::cout << "✅ initialized registry " << *id << '\n'
                      << "endpoint: " << endpoint.canonical << '\n';
            return 0;
        }

        if (command == "registry-info") {
            const auto registry = registry_option(argc, argv, 2, "registry-info");
            const auto endpoint = emojineer::parse_registry_endpoint(registry);
            std::cout << "id: " << emojineer::registry_identity(endpoint) << '\n'
                      << "endpoint: " << endpoint.canonical << '\n'
                      << "transport: "
                      << (endpoint.kind == emojineer::RegistryTransportKind::Https ? "https" : "file") << '\n'
                      << "https-support: "
                      << (emojineer::https_registry_transport_available() ? "available" : "not-built") << '\n';
            return 0;
        }

        if (command == "versions") {
            if (argc < 5) throw std::runtime_error("versions requires <package_name> --registry <endpoint>");
            const std::string package = argv[2];
            const auto registry = registry_option(argc, argv, 3, "versions");
            const auto endpoint = emojineer::parse_registry_endpoint(registry);
            std::cout << emojineer::render_registry_versions(
                emojineer::load_registry_package_index(endpoint, package));
            return 0;
        }

        if (command == "publish") {
            const auto options = parse_publish_options(argc, argv);
            const auto endpoint = emojineer::parse_registry_endpoint(*options.registry);
            const auto result = emojineer::publish_package_to_registry(options.root, endpoint);
            std::cout << (result.already_present ? "✅ already published " : "✅ published ")
                      << emojineer::load_project_manifest(options.root / "emojineer.toml").name
                      << '@' << result.record.version << '\n'
                      << "artifact-sha256: " << result.record.artifact_sha256 << '\n';
            return 0;
        }

        if (command == "fetch") {
            if (argc < 6) {
                throw std::runtime_error("fetch requires <package_name> <requirement> --registry <endpoint>");
            }
            const std::string package = argv[2];
            const std::string requirement = argv[3];
            const auto options = parse_fetch_options(argc, argv);
            const auto endpoint = emojineer::parse_registry_endpoint(options.registry);
            const auto result = emojineer::fetch_registry_package(endpoint,
                                                                   package,
                                                                   requirement,
                                                                   options.cache);
            std::cout << "✅ fetched " << package << '@' << result.record.version << '\n'
                      << "cache: " << result.cache_path.string() << '\n'
                      << "cache-hit: " << (result.cache_hit ? "yes" : "no") << '\n'
                      << "artifact-sha256: " << result.record.artifact_sha256 << '\n';
            return 0;
        }

        if (command == "add") {
            // Parse options to check for --registry
            std::optional<std::string> registry;
            std::optional<std::string> registry_name;
            std::vector<std::string> positional_args;
            
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--registry") {
                    if (++i >= argc) throw std::runtime_error("--registry requires an endpoint");
                    registry = argv[i];
                } else if (arg == "--registry-name") {
                    if (++i >= argc) throw std::runtime_error("--registry-name requires an alias");
                    registry_name = argv[i];
                } else if (arg.rfind("--", 0) == 0) {
                    throw std::runtime_error("unknown add option '" + arg + "'");
                } else {
                    positional_args.push_back(arg);
                }
            }
            
            if (registry) {
                // Registry dependency add - transactional
                // Format: emji add <name> <requirement> --registry <endpoint> [--registry-name <alias>] [directory]
                if (positional_args.size() < 2 || positional_args.size() > 3) {
                    throw std::runtime_error("add requires <package_name> <requirement> --registry <endpoint> and optional project directory");
                }
                const std::string name = positional_args[0];
                const std::string requirement = positional_args[1];
                std::filesystem::path root = positional_args.size() == 3
                    ? std::filesystem::path(positional_args[2])
                    : std::filesystem::current_path();
                
                emojineer::add_project_registry_dependency_transactional(root, name, requirement, *registry,
                    registry_name.value_or("origin"));
                std::cout << "✅ added " << name << " [" << requirement << "] from registry " << *registry << '\n';
                return 0;
            } else {
                // Path dependency add (backward compatible)
                if (positional_args.size() < 2 || positional_args.size() > 3) {
                    throw std::runtime_error("add requires <package_name> <relative_path> and optional project directory");
                }
                const std::string name = positional_args[0];
                const std::filesystem::path path = positional_args[1];
                const std::filesystem::path root = positional_args.size() == 3
                    ? std::filesystem::path(positional_args[2])
                    : std::filesystem::current_path();
                emojineer::add_project_dependency(root, name, path);
                std::cout << "✅ added " << name << " -> " << path.generic_string() << '\n';
                return 0;
            }
        }

        if (command == "remove") {
            if (argc < 3 || argc > 4) {
                throw std::runtime_error("remove requires <package_name> and optional project directory");
            }
            const std::string name = argv[2];
            const std::filesystem::path root = argc == 4
                                                   ? std::filesystem::path(argv[3])
                                                   : std::filesystem::current_path();
            emojineer::remove_project_dependency(root, name);
            std::cout << "✅ removed " << name << '\n';
            return 0;
        }

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "emji: " << error.what() << '\n';
        return 1;
    }
}
