#pragma once

#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"

#include <filesystem>
#include <functional>
#include <optional>

namespace emojineer {

/// Optional source-overlay provider used by editor/LSP callers.
///
/// The callback receives an absolute candidate path and returns an in-memory
/// source replacement when available. nullopt delegates to ordinary disk input;
/// using an overlay does not change module/package authorization semantics.
using SourceProvider = std::function<std::optional<std::string>(const std::filesystem::path&)>;

/// Compile an entry source and its authorized module graph into one EMJBC chunk.
///
/// When module_root is empty, the nearest enclosing emojineer.toml directory is
/// used when available, otherwise the entry directory is the root. A resolved
/// PackageGraph authorizes explicit `pkg:` imports while normal relative imports
/// remain confined to the owning package. Train 20 native facility identifiers
/// are deliberately preserved across symbol rewriting, so host calls originating
/// in dependencies contribute to the final chunk's required capability mask.
Chunk compile_file(const std::filesystem::path& entry,
                   CustomEmojiRegistry registry = {},
                   std::filesystem::path module_root = {},
                   SourceProvider source_provider = {});

} // namespace emojineer