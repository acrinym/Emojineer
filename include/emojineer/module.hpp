#pragma once

#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"

#include <filesystem>

namespace emojineer {

// Compile an entry source file and its module graph into one sovereign EMJBC chunk.
// If module_root is empty, the nearest enclosing emojineer.toml directory is used when
// available; otherwise the entry file's directory is the module root. When that root has
// an emojineer.toml, the resolved local PackageGraph authorizes explicit pkg: imports while
// ordinary relative imports remain confined to the owning package root.
Chunk compile_file(const std::filesystem::path& entry,
                   CustomEmojiRegistry registry = {},
                   std::filesystem::path module_root = {});

} // namespace emojineer