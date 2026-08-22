#include "emojineer/stdlib.hpp"

#include <array>

namespace emojineer {
namespace {

struct StandardModuleDefinition {
    StandardModuleInfo info;
    std::string_view source;
};

constexpr std::array<StandardModuleDefinition, 3> Modules{{
    {
        {"std:math", "small numeric helpers implemented in Emojineer source"},
        R"EMOJI(🧩 🧮
🛠️ 🧭 🫴 🍎 🤲
    🤔 🍎 🔽 0
        📦 ➖ 🍎
    🏁
    📦 🍎
🏁
🛠️ 🤏 🫴 🍎 🍐 🤲
    🤔 🍎 🔽 🍐
        📦 🍎
    🏁
    📦 🍐
🏁
🛠️ 👐 🫴 🍎 🍐 🤲
    🤔 🍎 🔼 🍐
        📦 🍎
    🏁
    📦 🍐
🏁
🛠️ 🎚️ 🫴 🍎 🍐 🍊 🤲
    🤔 🍎 🔽 🍐
        📦 🍐
    🏁
    🤔 🍎 🔼 🍊
        📦 🍊
    🏁
    📦 🍎
🏁
📤 🧭
📤 🤏
📤 👐
📤 🎚️
)EMOJI"
    },
    {
        {"std:arrays", "array search, sum, and reversal implemented in Emojineer source"},
        R"EMOJI(🧩 🗃️
🛠️ 🧲 🫴 🧺 🍎 🤲
    🐍 🧭 🔢 🟰 0
    🔁 🧭 🔽 📏 🧺
        🤔 🔎 🫴 🧺 🧭 🤲 🟰 🍎
            📦 ✅
        🏁
        ✏️ 🧭 🟰 🧭 ➕ 1
    🏁
    📦 ❌
🏁
🛠️ 🧮 🫴 🧺 🤲
    🐍 🧾 🔢 🟰 0
    🐍 🧭 🔢 🟰 0
    🔁 🧭 🔽 📏 🧺
        ✏️ 🧾 🟰 🧾 ➕ 🔎 🫴 🧺 🧭 🤲
        ✏️ 🧭 🟰 🧭 ➕ 1
    🏁
    📦 🧾
🏁
🛠️ 🔃 🫴 🧺 🤲
    🐍 🎒 📚 🟰 📚 🫴 🤲
    🐍 🧭 🔢 🟰 📏 🧺
    🔁 🧭 🔼 0
        ✏️ 🧭 🟰 🧭 ➖ 1
        ✏️ 🎒 🟰 📎 🫴 🎒 🔎 🫴 🧺 🧭 🤲 🤲
    🏁
    📦 🎒
🏁
📤 🧲
📤 🧮
📤 🔃
)EMOJI"
    },
    {
        {"std:text", "basic text predicates and repetition implemented in Emojineer source"},
        R"EMOJI(🧩 🧵
🛠️ 🈳 🫴 🧵 🤲
    📦 📏 🧵 🟰 0
🏁
🛠️ 🪢 🫴 🧵 🧶 🤲
    📦 🧵 ➕ 🧶
🏁
🛠️ 🔂 🫴 🧵 🧮 🤲
    🤔 🧮 🪄 1 🟰 0
        🐍 🧾 🔤 🟰 📜📜
        🐍 🧭 🔢 🟰 🧮
        🔁 🧭 🔼 0
            ✏️ 🧾 🟰 🧾 ➕ 🧵
            ✏️ 🧭 🟰 🧭 ➖ 1
        🏁
        📦 🧾
    🏁
    📦 📜📜
🏁
📤 🈳
📤 🪢
📤 🔂
)EMOJI"
    }
}};

} // namespace

std::optional<std::string_view> standard_module_source(std::string_view specifier) {
    for (const auto& module : Modules) {
        if (module.info.specifier == specifier) return module.source;
    }
    return std::nullopt;
}

std::vector<StandardModuleInfo> standard_modules() {
    std::vector<StandardModuleInfo> result;
    result.reserve(Modules.size());
    for (const auto& module : Modules) result.push_back(module.info);
    return result;
}

} // namespace emojineer
