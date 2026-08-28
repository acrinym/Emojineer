#pragma once

#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace emojineer {

// Optional source provider callback for in-memory document overlays.
// If provided, the module system will check the provider before reading from disk.
// Returns std::nullopt if the source is not provided (fall back to disk).
// The callback receives the absolute path to the source file.
using SourceProvider = std::function<std::optional<std::string>(const std::filesystem::path&)>;

// Compile an entry source file and its module graph into one sovereign EMJBC chunk.
// If module_root is empty, the nearest enclosing emojineer.toml directory is used when
// available; otherwise the entry file's directory is the module root. When that root has
// an emojineer.toml, the resolved local PackageGraph authorizes explicit pkg: imports while
// ordinary relative imports remain confined to the owning package root.
// If source_provider is provided, it will be checked before reading from disk.
Chunk compile_file(const std::filesystem::path& entry,
                   CustomEmojiRegistry registry = {},
                   std::filesystem::path module_root = {},
                   SourceProvider source_provider = {});

} // namespace emojineer