#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("capability test failed: " + message);
}

template <class Fn>
std::string expect_error(Fn&& fn, const std::string& needle) {
    try {
        fn();
    } catch (const std::exception& error) {
        const std::string message = error.what();
        require(message.find(needle) != std::string::npos,
                "expected error containing '" + needle + "', got '" + message + "'");
        return message;
    }
    throw std::runtime_error("capability test failed: expected error containing '" + needle + "'");
}

emojineer::Chunk compile_text(const std::string& source) {
    emojineer::Lexer lexer(source);
    emojineer::Parser parser(lexer.tokenize());
    emojineer::Compiler compiler;
    return compiler.compile(parser.parse());
}

struct TempRoot {
    std::filesystem::path path;
    TempRoot() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-capabilities-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }
    ~TempRoot() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write capability test file");
    output << text;
}

void test_intrinsic_compilation_and_mask() {
    const auto chunk = compile_text(
        "📝 🗂️ 🫴 📜x📜 🤲\n"
        "📝 🌐 🫴 📜https://example.com📜 🤲\n"
        "📝 ⚙️ 🫴 📜true📜 🤲\n"
        "📝 🕰️ 🫴 🤲\n"
        "📝 🎲 🫴 10 🤲\n"
        "📝 🖥️ 🫴 📜PATH📜 🤲\n");
    require(chunk.required_capabilities == emojineer::all_capabilities_mask(),
            "all native facilities should produce the full required-capability mask");
    std::size_t host_calls = 0;
    for (const auto& instruction : chunk.code)
        if (instruction.op == emojineer::OpCode::HostCall) ++host_calls;
    require(host_calls == 6, "each native facility should lower to HostCall");
    require(emojineer::capability_mask_string(chunk.required_capabilities) ==
                "filesystem, network, process, clock, random, host",
            "capability rendering should be deterministic");
}

void test_reserved_names_and_arity() {
    expect_error([&] {
        (void)compile_text("🛠️ 🎲 🫴 🤲\n📦 1\n🏁\n");
    }, "reserved native facility");
    expect_error([&] {
        (void)compile_text("📝 🕰️ 🫴 1 🤲\n");
    }, "expects 0 arguments");
}

void test_bytecode_v8_and_verifier_binding() {
    auto chunk = compile_text("📝 🕰️ 🫴 🤲\n");
    std::ostringstream encoded(std::ios::binary);
    emojineer::write_bytecode(chunk, encoded);
    const auto bytes = encoded.str();
    require(bytes.size() > 7, "encoded bytecode should contain a header");
    require(static_cast<unsigned char>(bytes[5]) == 8 && static_cast<unsigned char>(bytes[6]) == 0,
            "Train 20 bytecode should be EMJBC v8");

    std::istringstream input(bytes, std::ios::binary);
    const auto decoded = emojineer::read_bytecode(input);
    require(decoded.required_capabilities == emojineer::capability_mask(emojineer::Capability::Clock),
            "v8 round-trip should preserve required capabilities");

    auto dishonest = chunk;
    dishonest.required_capabilities = 0;
    expect_error([&] { emojineer::verify_bytecode(dishonest); }, "does not match host calls");

    auto invalid = chunk;
    for (auto& instruction : invalid.code) {
        if (instruction.op == emojineer::OpCode::HostCall) {
            instruction.operand = 999;
            break;
        }
    }
    expect_error([&] { emojineer::verify_bytecode(invalid); }, "invalid native facility");
}

void test_preflight_denial_has_zero_program_effects() {
    const auto chunk = compile_text(
        "📝 📜before📜\n"
        "📝 🕰️ 🫴 🤲\n");
    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output);
    expect_error([&] { vm.execute(chunk); }, "missing capability grant(s): clock");
    require(output.str().empty(), "capability denial must happen before earlier bytecode can print");
}

void test_deterministic_clock_and_random() {
    const auto chunk = compile_text(
        "📝 🕰️ 🫴 🤲\n"
        "📝 🕰️ 🫴 🤲\n"
        "📝 🎲 🫴 1000 🤲\n"
        "📝 🎲 🫴 1000 🤲\n");
    emojineer::ExecutionPolicy policy;
    policy.mode = emojineer::ExecutionMode::Deterministic;
    policy.grants = emojineer::capability_mask(emojineer::Capability::Clock) |
                    emojineer::capability_mask(emojineer::Capability::Random);
    policy.deterministic_seed = 123456789;
    policy.deterministic_clock_ms = 1000;

    std::istringstream input_a;
    std::ostringstream output_a;
    emojineer::VM vm_a(input_a, output_a, 1'000'000, policy);
    vm_a.execute(chunk);

    std::istringstream input_b;
    std::ostringstream output_b;
    emojineer::VM vm_b(input_b, output_b, 1'000'000, policy);
    vm_b.execute(chunk);

    require(output_a.str() == output_b.str(), "deterministic mode must reproduce clock/random results");
    require(output_a.str().rfind("1000\n1001\n", 0) == 0,
            "deterministic clock should start at the configured value and advance one millisecond");
}

void test_sandbox_and_deterministic_policy_validation() {
    emojineer::ExecutionPolicy sandbox;
    sandbox.mode = emojineer::ExecutionMode::Sandbox;
    sandbox.grants = emojineer::capability_mask(emojineer::Capability::Clock);
    expect_error([&] { emojineer::validate_execution_policy(sandbox); },
                 "sandbox mode cannot grant");

    emojineer::ExecutionPolicy deterministic;
    deterministic.mode = emojineer::ExecutionMode::Deterministic;
    deterministic.grants = emojineer::capability_mask(emojineer::Capability::Filesystem);
    expect_error([&] { emojineer::validate_execution_policy(deterministic); },
                 "permits only clock and random");
}

void test_filesystem_grant_is_real_and_explicit() {
    TempRoot root;
    const auto file = root.path / "payload.txt";
    write_text(file, "payload");
    const auto chunk = compile_text("📝 🗂️ 🫴 📜" + file.string() + "📜 🤲\n");

    std::istringstream denied_input;
    std::ostringstream denied_output;
    emojineer::VM denied(denied_input, denied_output);
    expect_error([&] { denied.execute(chunk); }, "filesystem");
    require(denied_output.str().empty(), "denied filesystem program must not execute");

    emojineer::ExecutionPolicy policy;
    policy.grants = emojineer::capability_mask(emojineer::Capability::Filesystem);
    std::istringstream granted_input;
    std::ostringstream granted_output;
    emojineer::VM granted(granted_input, granted_output, 1'000'000, policy);
    granted.execute(chunk);
    require(granted_output.str() == "payload\n", "filesystem read intrinsic should return file contents");
}

void test_filesystem_rejects_special_files_without_blocking() {
#ifndef _WIN32
    TempRoot root;
    const auto fifo = root.path / "payload.fifo";
    require(::mkfifo(fifo.c_str(), 0600) == 0, "should create FIFO fixture");

    const auto chunk = compile_text("📝 🗂️ 🫴 📜" + fifo.string() + "📜 🤲\n");
    emojineer::ExecutionPolicy policy;
    policy.grants = emojineer::capability_mask(emojineer::Capability::Filesystem);

    const pid_t child = ::fork();
    require(child >= 0, "should fork bounded FIFO execution child");
    if (child == 0) {
        int code = 0;
        std::istringstream input;
        std::ostringstream output;
        emojineer::VM vm(input, output, 1'000'000, policy);
        try {
            vm.execute(chunk);
            code = 2;
        } catch (const std::exception& error) {
            if (std::string(error.what()).find("regular file") == std::string::npos) code = 3;
        }
        if (!output.str().empty()) code = 4;
        ::_exit(code);
    }

    int status = 0;
    bool exited = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        const pid_t result = ::waitpid(child, &status, WNOHANG);
        if (result == child) {
            exited = true;
            break;
        }
        if (result < 0) {
            ::kill(child, SIGKILL);
            (void)::waitpid(child, &status, 0);
            throw std::runtime_error("capability test failed: waitpid failed for FIFO child");
        }
        ::usleep(10'000);
    }

    if (!exited) {
        ::kill(child, SIGKILL);
        (void)::waitpid(child, &status, 0);
        throw std::runtime_error(
            "capability test failed: filesystem FIFO read blocked past 2-second deadline");
    }
    require(WIFEXITED(status), "FIFO child should exit normally");
    require(WEXITSTATUS(status) == 0,
            "FIFO child should reject the special file without program output");
#endif
}

void test_imported_module_cannot_leak_authority() {
    TempRoot root;
    write_text(root.path / "dep.emoji",
               "🧩 🌲\n"
               "📝 🕰️ 🫴 🤲\n");
    write_text(root.path / "main.emoji",
               "🧩 🚀\n"
               "🔗 📜dep.emoji📜\n"
               "📝 📜main📜\n");

    const auto chunk = emojineer::compile_file(root.path / "main.emoji", {}, root.path);
    require(chunk.required_capabilities == emojineer::capability_mask(emojineer::Capability::Clock),
            "dependency host calls must contribute to the linked chunk capability contract");

    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output);
    expect_error([&] { vm.execute(chunk); }, "clock");
    require(output.str().empty(), "dependency capability denial must happen before any module executes");
}

} // namespace

int main() {
    try {
        test_intrinsic_compilation_and_mask();
        test_reserved_names_and_arity();
        test_bytecode_v8_and_verifier_binding();
        test_preflight_denial_has_zero_program_effects();
        test_deterministic_clock_and_random();
        test_sandbox_and_deterministic_policy_validation();
        test_filesystem_grant_is_real_and_explicit();
        test_filesystem_rejects_special_files_without_blocking();
        test_imported_module_cannot_leak_authority();
    } catch (const std::exception& error) {
        std::cerr << "❌ " << error.what() << '\n';
        return 1;
    }
    return 0;
}
