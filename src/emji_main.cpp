#include "emojineer/project.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr
        << "emji 0.11\n"
        << "usage:\n"
        << "  emji init <directory> [--name project_name]\n"
        << "  emji check [directory]\n"
        << "  emji lock [directory]\n"
        << "  emji show [directory]\n"
        << "  emji add <package_name> <relative_path> [directory]\n"
        << "  emji remove <package_name> [directory]\n";
}

std::filesystem::path optional_root(int argc, char** argv) {
    if (argc >= 3) return std::filesystem::path(argv[2]);
    return std::filesystem::current_path();
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
            if (manifest.dependencies.empty()) {
                std::cout << "dependencies: (none)\n";
            } else {
                std::cout << "dependencies:\n";
                for (const auto& dependency : manifest.dependencies) {
                    std::cout << "  " << dependency.name << " -> "
                              << dependency.path.generic_string() << '\n';
                }
            }
            return 0;
        }

        if (command == "add") {
            if (argc < 4 || argc > 5) {
                throw std::runtime_error("add requires <package_name> <relative_path> and optional project directory");
            }
            const std::string name = argv[2];
            const std::filesystem::path path = argv[3];
            const std::filesystem::path root = argc == 5
                                                   ? std::filesystem::path(argv[4])
                                                   : std::filesystem::current_path();
            emojineer::add_project_dependency(root, name, path);
            std::cout << "✅ added " << name << " -> " << path.generic_string() << '\n';
            return 0;
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
