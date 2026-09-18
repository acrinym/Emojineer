#include "emojineer/browser_runtime.hpp"

#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"

#include <sstream>
#include <string>

namespace emojineer {

namespace {
constexpr std::size_t MaxBrowserSourceBytes = 256 * 1024;
constexpr std::size_t MaxBrowserOutputBytes = 1024 * 1024;
}


BrowserRunResult run_browser_source(std::string_view source,
                                    const std::vector<std::string>& program_arguments) {
    BrowserRunResult result;
    if (source.size() > MaxBrowserSourceBytes) {
        result.diagnostic = "browser source exceeds 256 KiB limit";
        return result;
    }
    std::istringstream input;
    std::ostringstream output;
    try {
        CustomEmojiRegistry registry;
        Lexer lexer(std::string(source), registry);
        Parser parser(lexer.tokenize());
        Compiler compiler;
        auto chunk = compiler.compile(parser.parse());
        result.required_capabilities = chunk.required_capabilities;
        ExecutionPolicy policy;
        policy.mode = ExecutionMode::Sandbox;
        VM vm(input, output, 1'000'000, policy, nullptr, program_arguments);
        vm.execute(chunk);
        result.stdout_text = output.str();
        if (result.stdout_text.size() > MaxBrowserOutputBytes) {
            result.stdout_text.clear();
            result.diagnostic = "browser output exceeds 1 MiB limit";
            return result;
        }
        result.ok = true;
    } catch (const std::exception& error) {
        result.diagnostic = error.what();
    }
    if (result.stdout_text.empty()) result.stdout_text = output.str();
    if (result.stdout_text.size() > MaxBrowserOutputBytes) {
        result.ok = false;
        result.stdout_text.clear();
        result.diagnostic = "browser output exceeds 1 MiB limit";
    }
    return result;
}

} // namespace emojineer
