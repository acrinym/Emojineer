#include "emojineer/bytecode.hpp"
#include "emojineer/easm.hpp"
#include "emojineer/hash.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/package_artifact.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/project.hpp"
#include "emojineer/registry_transport.hpp"
#include "emojineer/vm.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("release hardening: " + message);
}

void append_u8(std::string& out, std::uint8_t value) {
    out.push_back(static_cast<char>(value));
}
void append_u16(std::string& out, std::uint16_t value) {
    append_u8(out, static_cast<std::uint8_t>(value & 0xff));
    append_u8(out, static_cast<std::uint8_t>((value >> 8) & 0xff));
}
void append_u32(std::string& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        append_u8(out, static_cast<std::uint8_t>((value >> shift) & 0xff));
}
void append_string(std::string& out, std::string_view value) {
    append_u32(out, static_cast<std::uint32_t>(value.size()));
    out.append(value);
}
void append_u64(std::string& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        append_u8(out, static_cast<std::uint8_t>((value >> shift) & 0xff));
}
void append_field64(std::string& out, std::string_view value) {
    append_u64(out, static_cast<std::uint64_t>(value.size()));
    out.append(value);
}

std::string legacy_halt_fixture(std::uint16_t version) {
    require(version >= 1 && version <= 9, "legacy fixture version must be 1..9");
    std::string out = "EMJBC";
    append_u16(out, version);
    append_u32(out, 0); // constants
    if (version >= 2) append_u32(out, 0); // functions
    append_u32(out, 1); // instructions
    append_u8(out, version == 1 ? 23 : static_cast<std::uint8_t>(emojineer::OpCode::Halt));
    append_u32(out, 0);
    append_u32(out, 1);
    if (version >= 6) {
        append_u32(out, 1);
        append_string(out, "legacy.emoji");
        append_u32(out, 1); append_u32(out, 1);
        append_u32(out, 1); append_u32(out, 1);
        append_string(out, "");
    } else if (version >= 4) {
        append_u32(out, 1);
        append_string(out, "legacy.emoji");
        append_u32(out, 1); append_u32(out, 1);
    }
    if (version >= 7) append_u32(out, 0); // provenance
    if (version >= 8) append_u32(out, 0); // capabilities
    if (version >= 9) {
        append_u32(out, 0); // imports
        append_u32(out, 0); // exports
    }
    return out;
}

void test_all_supported_legacy_bytecode_versions() {
    for (std::uint16_t version = 1; version <= 9; ++version) {
        const auto bytes = legacy_halt_fixture(version);
        std::istringstream input(bytes, std::ios::binary);
        auto chunk = emojineer::read_bytecode(input);
        require(chunk.code.size() == 1 && chunk.code.front().op == emojineer::OpCode::Halt,
                "EMJBC v" + std::to_string(version) + " fixture must decode as Halt");
        std::istringstream vm_input;
        std::ostringstream vm_output;
        emojineer::VM vm(vm_input, vm_output);
        vm.execute(chunk);
        require(vm_output.str().empty(),
                "EMJBC v" + std::to_string(version) + " fixture changed behavior");
    }
}

std::string mutate(std::string seed, std::mt19937_64& rng) {
    if (seed.empty()) seed = "x";
    const auto choice = static_cast<unsigned>(rng() % 4);
    const auto pos = static_cast<std::size_t>(rng() % seed.size());
    if (choice == 0) {
        seed[pos] = static_cast<char>(rng() & 0xff);
    } else if (choice == 1 && seed.size() > 1) {
        seed.erase(pos, 1);
    } else if (choice == 2) {
        seed.insert(pos, 1, static_cast<char>(rng() & 0xff));
    } else {
        const auto n = std::min<std::size_t>(seed.size() - pos, 8);
        for (std::size_t i = 0; i < n; ++i)
            seed[pos + i] = static_cast<char>(rng() & 0xff);
    }
    return seed;
}

template <typename Fn>
void bounded_mutation_corpus(std::string_view name, const std::string& seed, Fn&& reader) {
    std::mt19937_64 rng(0x454d4f4a494e4545ULL);
    for (int i = 0; i < 256; ++i) {
        auto candidate = mutate(seed, rng);
        try {
            reader(candidate);
        } catch (const std::exception&) {
            // Rejection is expected for most hostile mutations. The contract is bounded,
            // deterministic failure rather than crash, hang, or unchecked allocation.
        }
    }
    std::cout << "  hostile corpus passed: " << name << '\n';
}

std::string make_package_fixture() {
    // Frozen EMJPKG1 framing from the original Train 13 artifact format.
    // This intentionally does not call build_package_artifact_bytes().
    const std::string name = "legacy_fixture";
    const std::string version = "0.1.0";
    const std::string entry = "src/main.emoji";
    const std::string manifest =
        "[package]\nname = \"legacy_fixture\"\nversion = \"0.1.0\"\n"
        "entry = \"src/main.emoji\"\n";
    const std::string source = "📝 📜legacy package fixture📜\n";
    const std::string source_sha = emojineer::sha256_hex(source);

    std::string identity;
    append_field64(identity, "EMOJINEER-PACKAGE-v1");
    append_field64(identity, manifest);
    append_field64(identity, entry);
    append_field64(identity, source);
    const std::string content_sha = emojineer::sha256_hex(identity);

    std::string bytes = "EMJPKG1\n";
    append_field64(bytes, name);
    append_field64(bytes, version);
    append_field64(bytes, entry);
    append_field64(bytes, content_sha);
    append_field64(bytes, manifest);
    append_u64(bytes, 1);
    append_field64(bytes, entry);
    append_field64(bytes, source_sha);
    append_field64(bytes, source);
    return bytes;
}

void test_hostile_reader_corpora() {
    const std::string source_seed =
        "🐍 🍎 🔢 🟰 2\n📝 🍎 ➕ 3\n";
    bounded_mutation_corpus("lexer/parser", source_seed, [](const std::string& bytes) {
        emojineer::Lexer lexer(bytes);
        emojineer::Parser parser(lexer.tokenize());
        (void)parser.parse();
    });

    const auto bytecode_seed = legacy_halt_fixture(9);
    bounded_mutation_corpus("EMJBC", bytecode_seed, [](const std::string& bytes) {
        std::istringstream input(bytes, std::ios::binary);
        (void)emojineer::read_bytecode(input);
    });

    const std::string easm_seed =
        "EASM1\n"
        "func main () -> i64 regs (i64)\n"
        " i64.const r0 42\n"
        " return r0\n"
        "end\n"
        "export main main\n";
    bounded_mutation_corpus("EASM1", easm_seed, [](const std::string& bytes) {
        (void)emojineer::parse_easm(bytes);
    });

    const auto package_seed = make_package_fixture();
    const auto parsed_package = emojineer::parse_package_artifact(package_seed);
    require(parsed_package.name == "legacy_fixture",
            "frozen EMJPKG1 package fixture must remain readable");
    bounded_mutation_corpus("EMJPKG1", package_seed, [](const std::string& bytes) {
        (void)emojineer::parse_package_artifact(bytes);
    });

    emojineer::PublicationReceipt receipt{
        "fixture.dev", "spark", "1.2.3",
        std::string(64, 'a'), std::string(64, 'b'),
        "emjpub1", "receipt-123", "2026-09-18T12:00:00Z"
    };
    const auto receipt_seed = emojineer::render_publication_receipt(receipt);
    bounded_mutation_corpus("registry receipt", receipt_seed, [](const std::string& bytes) {
        (void)emojineer::parse_publication_receipt(bytes);
    });

    const std::string endpoint_seed = "https://registry.example.test";
    bounded_mutation_corpus("registry endpoint", endpoint_seed, [](const std::string& bytes) {
        (void)emojineer::parse_registry_endpoint(bytes);
    });
}

void test_explicit_truncation_and_unknown_versions() {
    for (std::uint16_t version = 1; version <= 9; ++version) {
        auto bytes = legacy_halt_fixture(version);
        bytes.pop_back();
        std::istringstream truncated(bytes, std::ios::binary);
        try {
            (void)emojineer::read_bytecode(truncated);
            throw std::runtime_error("truncated legacy EMJBC unexpectedly decoded");
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()).find("truncated") != std::string::npos,
                    "truncated EMJBC must fail explicitly");
        }
    }

    auto unknown = legacy_halt_fixture(9);
    unknown[5] = static_cast<char>(99);
    unknown[6] = 0;
    std::istringstream input(unknown, std::ios::binary);
    try {
        (void)emojineer::read_bytecode(input);
        throw std::runtime_error("unknown EMJBC version unexpectedly decoded");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("unsupported Emojineer bytecode version") !=
                    std::string::npos,
                "unknown EMJBC version must report compatibility failure");
    }

    const auto lock_path = std::filesystem::temp_directory_path() /
                           "emojineer-release-hardening-v2.lock";
    {
        std::ofstream lock(lock_path, std::ios::binary | std::ios::trunc);
        lock << "lock_version = 2\nmanifest_hash = \"legacy\"\n";
    }
    try {
        (void)emojineer::load_project_lock(lock_path);
        throw std::runtime_error("legacy lock format unexpectedly decoded as v3");
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find("unsupported project lock format 2") !=
                    std::string::npos,
                "pre-v3 locks must fail with regeneration guidance");
    }
    std::error_code ignored;
    std::filesystem::remove(lock_path, ignored);
}

} // namespace

int main() {
    try {
        test_all_supported_legacy_bytecode_versions();
        test_hostile_reader_corpora();
        test_explicit_truncation_and_unknown_versions();
        std::cout << "release hardening tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "release hardening tests failed: " << error.what() << '\n';
        return 1;
    }
}
