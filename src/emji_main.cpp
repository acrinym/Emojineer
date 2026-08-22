#include "emojineer/project.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cerr
        << "emji 0.8\n"
        << "usage:\n"
        << "  emji init <directory> [--name project_name]\n"
        << "  emji check [directory]\n"
        << "  emji lock [directory]\n"
        << "  emji show [directory]\n";
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
            return 0;
        }

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "emji: " << error.what() << '\n';
        return 1;
    }
}
