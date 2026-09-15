#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/cer.hpp"
#include "emojineer/debugger.hpp"
#include "emojineer/disassembler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/repl.hpp"
#include "emojineer/source_tools.hpp"
#include "emojineer/stdlib.hpp"
#include "emojineer/vm.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot write '" + path.string() + "'");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("failed while writing '" + path.string() + "'");
}

struct Cli {
    std::string command;
    std::optional<std::filesystem::path> input;
    std::vector<std::string> cer;
    std::optional<std::filesystem::path> output;
    std::vector<std::string> grants;
    bool sandbox{false};
    bool deterministic{false};
    std::optional<std::uint64_t> seed;
    std::optional<std::int64_t> clock_ms;
};

void usage() {
    std::cerr
        << "Emojineer 0.20\n"
        << "usage:\n"
        << "  emojineer repl [--cer registry.json ...] [execution-policy]\n"
        << "  emojineer stdlib\n"
        << "  emojineer debug <source-or-project> [--cer registry.json ...] [execution-policy]\n"
        << "  emojineer run <file.emoji> [--cer registry.json ...] [execution-policy]\n"
        << "  emojineer <check|explain|dump|lint> <file.emoji> [--cer registry.json ...]\n"
        << "  emojineer fmt <file.emoji> [-o file.emoji] [--cer registry.json ...]\n"
        << "  emojineer compile <file.emoji> [-o file.emjbc] [--cer registry.json ...]\n"
        << "  emojineer exec <file.emjbc> [execution-policy]\n"
        << "  emojineer disasm <file.emjbc>\n"
        << "  emojineer capabilities <file.emoji|file.emjbc> [--cer registry.json ...]\n"
        << "execution-policy:\n"
        << "  --grant <filesystem|network|process|clock|random|host|all>  repeatable\n"
        << "  --sandbox                 hard zero-host-authority mode\n"
        << "  --deterministic           virtual clock/random only; real host grants rejected\n"
        << "  --seed <u64>              deterministic random seed\n"
        << "  --clock-ms <i64>          deterministic logical clock start\n";
}

std::uint64_t parse_u64(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    if (!text.empty() && text.front() == '-') throw std::runtime_error(option + " requires an unsigned integer");
    const auto value = std::stoull(text, &consumed, 10);
    if (consumed != text.size()) throw std::runtime_error(option + " requires an unsigned integer");
    return static_cast<std::uint64_t>(value);
}

std::int64_t parse_i64(const std::string& text, const std::string& option) {
    std::size_t consumed = 0;
    const auto value = std::stoll(text, &consumed, 10);
    if (consumed != text.size()) throw std::runtime_error(option + " requires an integer");
    return static_cast<std::int64_t>(value);
}

Cli parse_cli(int argc, char** argv) {
    if (argc < 2) {
        usage();
        throw std::runtime_error("missing command");
    }

    Cli cli;
    cli.command = argv[1];

    int i = 2;
    if (cli.command != "repl" && cli.command != "stdlib") {
        if (i >= argc) {
            usage();
            throw std::runtime_error("missing input");
        }
        cli.input = std::filesystem::path(argv[i++]);
    }

    for (; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--cer") {
            if (++i >= argc) throw std::runtime_error("--cer requires a registry path");
            cli.cer.push_back(argv[i]);
        } else if (arg == "-o") {
            if (++i >= argc) throw std::runtime_error("-o requires an output path");
            cli.output = std::filesystem::path(argv[i]);
        } else if (arg == "--grant") {
            if (++i >= argc) throw std::runtime_error("--grant requires a capability name");
            cli.grants.push_back(argv[i]);
        } else if (arg == "--sandbox") {
            cli.sandbox = true;
        } else if (arg == "--deterministic") {
            cli.deterministic = true;
        } else if (arg == "--seed") {
            if (++i >= argc) throw std::runtime_error("--seed requires a value");
            cli.seed = parse_u64(argv[i], "--seed");
        } else if (arg == "--clock-ms") {
            if (++i >= argc) throw std::runtime_error("--clock-ms requires a value");
            cli.clock_ms = parse_i64(argv[i], "--clock-ms");
        } else {
            throw std::runtime_error("unknown option '" + arg + "'");
        }
    }

    return cli;
}

bool has_execution_policy_options(const Cli& cli) {
    return !cli.grants.empty() || cli.sandbox || cli.deterministic || cli.seed.has_value() || cli.clock_ms.has_value();
}

void reject_execution_policy_options(const Cli& cli, const std::string& command) {
    if (has_execution_policy_options(cli))
        throw std::runtime_error(command + " does not accept execution policy options");
}

emojineer::ExecutionPolicy execution_policy_for(const Cli& cli) {
    if (cli.sandbox && cli.deterministic)
        throw std::runtime_error("--sandbox and --deterministic are mutually exclusive");
    if ((cli.seed || cli.clock_ms) && !cli.deterministic)
        throw std::runtime_error("--seed and --clock-ms require --deterministic");

    emojineer::ExecutionPolicy policy;
    policy.mode = cli.sandbox ? emojineer::ExecutionMode::Sandbox
                              : (cli.deterministic ? emojineer::ExecutionMode::Deterministic
                                                   : emojineer::ExecutionMode::Normal);
    for (const auto& grant : cli.grants) {
        if (grant == "all") {
            policy.grants |= emojineer::all_capabilities_mask();
            continue;
        }
        const auto capability = emojineer::parse_capability(grant);
        if (!capability) throw std::runtime_error("unknown capability '" + grant + "'");
        policy.grants |= emojineer::capability_mask(*capability);
    }
    if (cli.seed) policy.deterministic_seed = *cli.seed;
    if (cli.clock_ms) policy.deterministic_clock_ms = *cli.clock_ms;
    emojineer::validate_execution_policy(policy);
    return policy;
}

emojineer::CustomEmojiRegistry registry_for(const Cli& cli) {
    emojineer::CustomEmojiRegistry registry;
    for (const auto& path : cli.cer) registry.load_file(path);
    return registry;
}

emojineer::Chunk read_chunk(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return emojineer::read_bytecode(input);
}

} // namespace

int main(int argc, char** argv) {
    try {
        Cli cli = parse_cli(argc, argv);

        if (cli.command == "repl") {
            if (cli.output) throw std::runtime_error("repl does not accept -o");
            return emojineer::run_repl(std::cin, std::cout, std::cerr, registry_for(cli), execution_policy_for(cli));
        }

        if (cli.command == "stdlib") {
            reject_execution_policy_options(cli, "stdlib");
            if (cli.output || !cli.cer.empty()) {
                throw std::runtime_error("stdlib does not accept source CER or -o options");
            }
            for (const auto& module : emojineer::standard_modules()) {
                std::cout << module.specifier << "  " << module.description << '\n';
            }
            return 0;
        }

        if (!cli.input) throw std::runtime_error("missing input");

        if (cli.command == "exec" || cli.command == "disasm") {
            if (!cli.cer.empty() || cli.output) {
                throw std::runtime_error(cli.command + " does not use source CER or -o options");
            }
            auto chunk = read_chunk(*cli.input);
            if (cli.command == "disasm") {
                reject_execution_policy_options(cli, "disasm");
                emojineer::verify_bytecode(chunk);
                emojineer::disassemble(chunk, std::cout);
                return 0;
            }
            emojineer::VM vm(std::cin, std::cout, 1'000'000, execution_policy_for(cli));
            vm.execute(chunk);
            return 0;
        }

        auto registry = registry_for(cli);

        if (cli.command == "capabilities") {
            reject_execution_policy_options(cli, "capabilities");
            if (cli.output) throw std::runtime_error("capabilities does not accept -o");
            emojineer::Chunk chunk;
            if (cli.input->extension() == ".emjbc") {
                if (!cli.cer.empty()) throw std::runtime_error("bytecode capabilities does not use source CER");
                chunk = read_chunk(*cli.input);
            } else {
                chunk = emojineer::compile_file(*cli.input, std::move(registry));
            }
            std::cout << "required capabilities: "
                      << emojineer::capability_mask_string(chunk.required_capabilities) << '\n';
            return 0;
        }

        if (cli.command == "fmt") {
            reject_execution_policy_options(cli, "fmt");
            const std::string formatted =
                emojineer::format_source(read_text(*cli.input), std::move(registry));
            if (cli.output) write_text(*cli.output, formatted);
            else std::cout << formatted;
            return 0;
        }

        if (cli.command == "lint") {
            reject_execution_policy_options(cli, "lint");
            if (cli.output) throw std::runtime_error("lint does not accept -o");
            const auto diagnostics =
                emojineer::diagnose_source_style(read_text(*cli.input), std::move(registry));
            if (diagnostics.empty()) {
                std::cout << "✅ " << cli.input->string() << " is canonically formatted\n";
                return 0;
            }
            for (const auto& diagnostic : diagnostics) {
                std::cout << cli.input->string() << ':' << diagnostic.line
                          << ": " << diagnostic.message << '\n';
            }
            return 1;
        }

        if (cli.command == "explain") {
            reject_execution_policy_options(cli, "explain");
            if (cli.output) throw std::runtime_error("explain does not accept -o");
            std::cout << emojineer::Lexer(read_text(*cli.input), std::move(registry)).explain();
            return 0;
        }

        if (cli.command == "check") {
            reject_execution_policy_options(cli, "check");
            if (cli.output) throw std::runtime_error("check does not accept -o");
            (void)emojineer::compile_file(*cli.input, std::move(registry));
            std::cout << "✅ " << cli.input->string() << " is valid Emojineer source\n";
            return 0;
        }

        if (cli.command == "run") {
            if (cli.output) throw std::runtime_error("run does not accept -o");
            auto chunk = emojineer::compile_file(*cli.input, std::move(registry));
            emojineer::VM vm(std::cin, std::cout, 1'000'000, execution_policy_for(cli));
            vm.execute(chunk);
            return 0;
        }

        if (cli.command == "dump") {
            reject_execution_policy_options(cli, "dump");
            if (cli.output) throw std::runtime_error("dump does not accept -o");
            auto chunk = emojineer::compile_file(*cli.input, std::move(registry));
            emojineer::disassemble(chunk, std::cout);
            return 0;
        }

        if (cli.command == "compile") {
            reject_execution_policy_options(cli, "compile");
            auto chunk = emojineer::compile_file(*cli.input, std::move(registry));
            auto output_path = cli.output.value_or(*cli.input);
            if (!cli.output) output_path.replace_extension(".emjbc");
            std::ofstream output(output_path, std::ios::binary);
            if (!output) throw std::runtime_error("cannot write '" + output_path.string() + "'");
            emojineer::write_bytecode(chunk, output);
            std::cout << "✅ wrote " << output_path.string() << '\n';
            return 0;
        }

        if (cli.command == "debug") {
            if (cli.output) throw std::runtime_error("debug does not accept -o");
            return emojineer::run_debug_session(*cli.input, std::cin, std::cout, std::cerr,
                                                registry_for(cli), execution_policy_for(cli));
        }

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "emojineer: " << error.what() << '\n';
        return 1;
    }
}
