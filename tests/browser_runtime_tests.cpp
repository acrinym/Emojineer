#include "emojineer/browser_runtime.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string direct_sandbox_run(const std::string& source,
                               const std::vector<std::string>& arguments = {}) {
    emojineer::CustomEmojiRegistry registry;
    emojineer::Lexer lexer(source, registry);
    emojineer::Parser parser(lexer.tokenize());
    emojineer::Compiler compiler;
    auto chunk = compiler.compile(parser.parse());
    emojineer::ExecutionPolicy policy;
    policy.mode = emojineer::ExecutionMode::Sandbox;
    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output, 1'000'000, policy, nullptr, arguments);
    vm.execute(chunk);
    return output.str();
}

void test_browser_runtime_matches_production_vm() {
    const std::vector<std::string> programs{
        "📝 📜Hello browser 🚀📜\n",
        "🐍 🧪 🟰 🧬 🫴 📜Hi🙂📜 🤲\n📝 🔡 🫴 🧪 🤲\n",
        "🐍 🧑 🟰 🗃️ 🫴 📜Person📜 📚 🫴 📜name📜 📜Ada📜 🤲 🤲\n"
        "📝 🔎 🫴 🧑 📜name📜 🤲\n",
    };
    for (const auto& source : programs) {
        const auto browser = emojineer::run_browser_source(source);
        require(browser.ok, "browser runtime should execute representative source");
        require(browser.diagnostic.empty(), "successful browser run should have no diagnostic");
        require(browser.stdout_text == direct_sandbox_run(source),
                "browser runtime must match the production sandboxed VM output");
    }
}

void test_browser_runtime_preserves_explicit_arguments() {
    const std::string source = "🐍 🎒 🟰 🧳 🫴 🤲\n📝 🎒\n";
    const std::vector<std::string> arguments{"alpha", "two words"};
    const auto browser = emojineer::run_browser_source(source, arguments);
    require(browser.ok, "browser runtime should support explicit program arguments");
    require(browser.stdout_text == direct_sandbox_run(source, arguments),
            "browser arguments must use the same VM semantics as native execution");
}

void test_browser_runtime_bounds_source_size() {
    std::string source(256 * 1024 + 1, 'x');
    const auto result = emojineer::run_browser_source(source);
    require(!result.ok, "browser runtime must reject oversized source");
    require(result.diagnostic.find("256 KiB") != std::string::npos,
            "oversized browser source should report the source limit");
}

void test_browser_runtime_denies_ambient_authority() {
    const auto result = emojineer::run_browser_source("📝 🌐 🫴 📜https://example.com📜 🤲\n");
    require(!result.ok, "browser runtime must reject native host authority");
    require(result.required_capabilities == emojineer::capability_mask(emojineer::Capability::Network),
            "browser runtime should report the capability that caused rejection");
    require(result.stdout_text.empty(), "capability preflight must happen before program output");
    require(result.diagnostic.find("sandbox") != std::string::npos ||
                result.diagnostic.find("capability") != std::string::npos,
            "browser authority rejection should explain the policy boundary");
}

} // namespace

int main() {
    try {
        test_browser_runtime_matches_production_vm();
        test_browser_runtime_preserves_explicit_arguments();
        test_browser_runtime_bounds_source_size();
        test_browser_runtime_denies_ambient_authority();
        std::cout << "browser runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "browser runtime tests failed: " << error.what() << '\n';
        return 1;
    }
}
