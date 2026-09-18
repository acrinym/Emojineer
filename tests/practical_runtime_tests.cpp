#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("practical runtime test failed: " + message);
}

template <class Fn>
void expect_error(Fn&& fn, const std::string& needle) {
    try { fn(); } catch (const std::exception& error) {
        require(std::string(error.what()).find(needle) != std::string::npos,
                "expected '" + needle + "', got '" + error.what() + "'");
        return;
    }
    throw std::runtime_error("expected error containing '" + needle + "'");
}

emojineer::Chunk compile_text(const std::string& source) {
    emojineer::Lexer lexer(source);
    emojineer::Parser parser(lexer.tokenize());
    emojineer::Compiler compiler;
    return compiler.compile(parser.parse());
}

std::string run(const emojineer::Chunk& chunk,
                emojineer::ExecutionPolicy policy = {},
                std::vector<std::string> arguments = {}) {
    std::istringstream input;
    std::ostringstream output;
    emojineer::VM vm(input, output, 1'000'000, policy, nullptr, std::move(arguments));
    vm.execute(chunk);
    return output.str();
}

struct TempRoot {
    std::filesystem::path path;
    TempRoot() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("emojineer-practical-" + std::to_string(nonce));
        std::filesystem::create_directories(path);
    }
    ~TempRoot() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

void test_records_and_results() {
    const auto chunk = compile_text(
        "🐍 🧑 🟰 🗃️ 🫴 📜Person📜 📚 🫴 📜name📜 📜Ada📜 📜age📜 37 🤲 🤲\n"
        "📝 🏷️ 🫴 🧑 🤲\n"
        "📝 🔎 🫴 🧑 📜name📜 🤲\n"
        "🐍 👤 🟰 🧷 🫴 🧑 📜age📜 38 🤲\n"
        "📝 🔎 🫴 👤 📜age📜 🤲\n"
        "📝 📏 👤\n"
        "📝 🗝️ 🫴 👤 🤲\n"
        "🐍 🟩 🟰 🟢 🫴 📜yes📜 🤲\n"
        "📝 👌 🫴 🟩 🤲\n"
        "📝 🎁 🫴 🟩 🤲\n"
        "🐍 🟥 🟰 🔴 🫴 📜no📜 🤲\n"
        "📝 👌 🫴 🟥 🤲\n"
        "📝 🎁 🫴 🟥 🤲\n");
    const auto output = run(chunk);
    require(output == "Person\nAda\n38\n2\n[age, name]\n✅\nyes\n❌\nno\n",
            "record/result source semantics");
}

void test_bytes_and_codecs() {
    const auto chunk = compile_text(
        "🐍 🧪 🟰 🧬 🫴 📜Hi🙂📜 🤲\n"
        "📝 📏 🧪\n"
        "📝 🔎 🫴 🧪 0 🤲\n"
        "📝 🔡 🫴 🧪 🤲\n"
        "📝 📨 🫴 🧪 🤲\n"
        "🐍 🧫 🟰 📎 🫴 🧪 33 🤲\n"
        "📝 🔡 🫴 🧫 🤲\n"
        "🐍 🧊 🟰 🧷 🫴 🧫 0 74 🤲\n"
        "📝 🗣️ 🫴 🧊 🤲\n"
        "📝 🗣️ 🫴 📩 🫴 📜SGnwn5mC📜 🤲 🤲\n");
    const auto output = run(chunk);
    require(output == "6\n72\n4869f09f9982\nSGnwn5mC\n4869f09f998221\nJi🙂!\nHi🙂\n",
            "bytes/index/update/codecs source semantics");

    const auto malformed = compile_text("📝 🗣️ 🫴 🔣 🫴 📜ff📜 🤲 🤲\n");
    expect_error([&] { (void)run(malformed); }, "malformed UTF-8");

    const auto grapheme = compile_text(
        "🐍 🧵 🟰 📜é📜\n"
        "📝 📏 🧵\n"
        "🐍 🧪 🟰 🧬 🫴 🧵 🤲\n"
        "📝 📏 🧪\n"
        "📝 🗣️ 🫴 🧪 🤲\n");
    require(run(grapheme) == "1\n3\né\n",
            "UTF-8 byte length must remain distinct from grapheme length");

    const auto noncanonical64 = compile_text("📝 📩 🫴 📜AB==📜 🤲\n");
    expect_error([&] { (void)run(noncanonical64); }, "non-canonical padding bits");
}

void test_explicit_program_arguments() {
    const auto chunk = compile_text("📝 🧳 🫴 🤲\n");
    require(run(chunk, {}, {"one", "two words"}) == "[one, two words]\n",
            "runtime.arguments should expose exact explicit invocation input");
    require(chunk.required_capabilities == 0,
            "explicit program arguments must not claim ambient host authority");
}

void test_filesystem_write_directory_and_preflight() {
    TempRoot root;
    const auto directory = root.path / "nested" / "dir";
    const auto file = directory / "payload.txt";
    const auto source =
        "📝 📁 🫴 📜" + directory.string() + "📜 🤲\n" +
        "📝 ✍️ 🫴 📜" + file.string() + "📜 📜hello utility📜 🤲\n" +
        "📝 🗂️ 🫴 📜" + file.string() + "📜 🤲\n";
    const auto chunk = compile_text(source);
    require(chunk.required_capabilities ==
                emojineer::capability_mask(emojineer::Capability::Filesystem),
            "write/create/read should infer only filesystem authority");
    expect_error([&] { (void)run(chunk); }, "filesystem");
    require(!std::filesystem::exists(directory), "preflight denial must have zero filesystem effects");

    emojineer::ExecutionPolicy policy;
    policy.grants = emojineer::capability_mask(emojineer::Capability::Filesystem);
    require(run(chunk, policy) == "✅\n✅\nhello utility\n", "filesystem utility journey");
    require(std::filesystem::is_regular_file(file), "write-text should create a regular file");
}

void test_network_request_contract_and_v10_roundtrip() {
    const auto chunk = compile_text(
        "📝 🛰️ 🫴 📜POST📜 📜https://example.test/api📜 📜payload📜 🤲\n");
    require(chunk.required_capabilities ==
                emojineer::capability_mask(emojineer::Capability::Network),
            "network.request should infer network authority");
    expect_error([&] { (void)run(chunk); }, "network");

    emojineer::ExecutionPolicy network_policy;
    network_policy.grants = emojineer::capability_mask(emojineer::Capability::Network);
    const auto invalid_method = compile_text(
        "📝 🛰️ 🫴 📜TRACE📜 📜https://example.test/api📜 📜📜 🤲\n");
    expect_error([&] { (void)run(invalid_method, network_policy); },
                 "method must be GET, POST, PUT, PATCH, or DELETE");

    std::ostringstream encoded(std::ios::binary);
    emojineer::write_bytecode(chunk, encoded);
    const auto bytes = encoded.str();
    require(static_cast<unsigned char>(bytes.at(5)) == 10 &&
            static_cast<unsigned char>(bytes.at(6)) == 0,
            "current writer should emit EMJBC v10");
    std::istringstream input(bytes, std::ios::binary);
    const auto decoded = emojineer::read_bytecode(input);
    require(decoded.required_capabilities == chunk.required_capabilities,
            "v10 round-trip should preserve practical host authority");
}

} // namespace

int main() {
    try {
        test_records_and_results();
        test_bytes_and_codecs();
        test_explicit_program_arguments();
        test_filesystem_write_directory_and_preflight();
        test_network_request_contract_and_v10_roundtrip();
        std::cout << "all practical 1.0 foundation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
