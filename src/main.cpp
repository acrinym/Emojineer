#include "emojineer/bytecode.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/cer.hpp"
#include "emojineer/debugger.hpp"
#include "emojineer/disassembler.hpp"
#include "emojineer/easm.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/interop.hpp"
#include "emojineer/repl.hpp"
#include "emojineer/source_tools.hpp"
#include "emojineer/stdlib.hpp"
#include "emojineer/vm.hpp"
#include "emojineer/version.hpp"
#include "emojineer/web.hpp"

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
    std::vector<std::string> program_arguments;
};

void usage() {
    std::cerr
        << "Emojineer " << emojineer::version << "\n"
        << "usage:\n"
        << "  emojineer repl [--cer registry.json ...] [execution-policy]\n"
        << "  emojineer stdlib\n"
        << "  emojineer debug <source-or-project> [--cer registry.json ...] [execution-policy]\n"
        << "  emojineer run <file.emoji> [--cer registry.json ...] [execution-policy] [-- arg ...]\n"
        << "  emojineer <check|explain|dump|lint> <file.emoji> [--cer registry.json ...]\n"
        << "  emojineer fmt <file.emoji> [-o file.emoji] [--cer registry.json ...]\n"
        << "  emojineer compile <file.emoji> [-o file.emjbc] [--cer registry.json ...]\n"
        << "  emojineer exec <file.emjbc> [execution-policy] [-- arg ...]\n"
        << "  emojineer disasm <file.emjbc>\n"
        << "  emojineer capabilities <file.emoji|file.emjbc> [--cer registry.json ...]\n"
        << "  emojineer interop <file.emoji|file.emjbc> [--cer registry.json ...]\n"
        << "  emojineer <easm-check|easm-dump|easm-info> <file.easm>\n"
        << "  emojineer easm-run <file.easm> [execution-policy]\n"
        << "  emojineer <web-check|web-dump|web-bindings> <file.emjweb>\n"
        << "  emojineer web-build <file.emjweb> [-o page.html]\n"
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
        } else if (arg == "--") {
            for (++i; i < argc; ++i) cli.program_arguments.emplace_back(argv[i]);
            break;
        } else {
            throw std::runtime_error("unknown option '" + arg + "'");
        }
    }

    if (!cli.program_arguments.empty() && cli.command != "run" && cli.command != "exec")
        throw std::runtime_error("program arguments after -- are accepted only by run or exec");
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

std::string render_interop_signature(const emojineer::InteropSignature& signature) {
    std::string out = "(";
    for (std::size_t i = 0; i < signature.parameters.size(); ++i) {
        if (i) out += ", ";
        out += emojineer::interop_type_name(signature.parameters[i]);
    }
    out += ") -> ";
    out += emojineer::interop_type_name(signature.result);
    return out;
}

void print_interop_contract(const emojineer::Chunk& chunk) {
    std::cout << "interop imports: " << chunk.interop_imports.size() << '\n';
    for (const auto& import : chunk.interop_imports) {
        std::cout << "  " << import.internal_name << " -> " << import.external_name << ' '
                  << render_interop_signature(import.signature) << " capabilities="
                  << emojineer::capability_mask_string(import.required_capabilities) << '\n';
    }
    std::cout << "interop exports: " << chunk.interop_exports.size() << '\n';
    for (const auto& export_info : chunk.interop_exports) {
        std::cout << "  " << export_info.external_name << " <- "
                  << chunk.functions.at(export_info.function_index).name << ' '
                  << render_interop_signature(export_info.signature) << '\n';
    }
}

std::string render_easm_signature(const std::vector<emojineer::EasmScalarType>& parameters,
                                  emojineer::EasmScalarType result) {
    std::string out = "(";
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i) out += ", ";
        out += emojineer::easm_scalar_type_name(parameters[i]);
    }
    out += ") -> ";
    out += emojineer::easm_scalar_type_name(result);
    return out;
}

void print_easm_info(const emojineer::EasmProgram& program) {
    std::cout << "required capabilities: " << emojineer::capability_mask_string(program.required_capabilities) << '\n';
    std::cout << "buffers: " << program.buffers.size() << '\n';
    for (const auto& buffer : program.buffers)
        std::cout << "  " << buffer.name << ' ' << emojineer::easm_buffer_type_name(buffer.type) << '[' << buffer.elements << "]\n";
    std::cout << "imports: " << program.imports.size() << '\n';
    for (const auto& import : program.imports)
        std::cout << "  " << import.internal_name << " -> " << import.external_name << ' ' << render_easm_signature(import.parameters, import.result) << " capabilities=" << emojineer::capability_mask_string(import.required_capabilities) << '\n';
    std::cout << "exports: " << program.exports.size() << '\n';
    for (const auto& export_info : program.exports) {
        const auto& function = program.functions.at(export_info.function_index);
        std::cout << "  " << export_info.external_name << " <- " << function.name << ' ' << render_easm_signature(function.parameters, function.result) << " capabilities=" << emojineer::capability_mask_string(emojineer::easm_export_capabilities(program, export_info.external_name)) << '\n';
    }
}

std::string easm_scalar_string(const emojineer::EasmScalar& scalar) {
    return std::visit([](const auto& value) { return emojineer::value_to_string(emojineer::Value{value}); }, scalar);
}

emojineer::Chunk read_chunk(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open '" + path.string() + "'");
    return emojineer::read_bytecode(input);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << "emojineer " << emojineer::version << '\n';
            return 0;
        }
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

        if (cli.command == "web-check" || cli.command == "web-dump" ||
            cli.command == "web-bindings" || cli.command == "web-build") {
            reject_execution_policy_options(cli, cli.command);
            if (!cli.cer.empty())
                throw std::runtime_error(cli.command + " does not accept source CER");
            if (cli.command != "web-build" && cli.output)
                throw std::runtime_error(cli.command + " does not accept -o");
            const auto document = emojineer::parse_web_document(read_text(*cli.input));
            if (cli.command == "web-check") {
                std::cout << "✅ " << cli.input->string() << " is valid Emojineer web markup\n";
                return 0;
            }
            if (cli.command == "web-dump") {
                std::cout << emojineer::render_web_document_ir(document);
                return 0;
            }
            if (cli.command == "web-bindings") {
                std::cout << emojineer::render_web_bindings_json(document) << '\n';
                return 0;
            }
            const auto html = emojineer::render_web_document_html(document);
            if (cli.output) write_text(*cli.output, html);
            else std::cout << html;
            return 0;
        }

        if (cli.command == "easm-check" || cli.command == "easm-dump" ||
            cli.command == "easm-info" || cli.command == "easm-run") {
            if (!cli.cer.empty() || cli.output)
                throw std::runtime_error(cli.command + " does not accept source CER or -o options");
            auto program = emojineer::parse_easm(read_text(*cli.input));
            if (cli.command == "easm-check") {
                reject_execution_policy_options(cli, "easm-check");
                std::cout << "✅ " << cli.input->string() << " is valid EASM1\n";
                return 0;
            }
            if (cli.command == "easm-dump") {
                reject_execution_policy_options(cli, "easm-dump");
                std::cout << emojineer::render_easm(program);
                return 0;
            }
            if (cli.command == "easm-info") {
                reject_execution_policy_options(cli, "easm-info");
                print_easm_info(program);
                return 0;
            }
            emojineer::EasmVM vm(execution_policy_for(cli));
            std::cout << easm_scalar_string(vm.invoke_export(program, "main", {})) << '\n';
            return 0;
        }

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
            emojineer::VM vm(std::cin, std::cout, 1'000'000, execution_policy_for(cli), nullptr,
                               cli.program_arguments);
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

        if (cli.command == "interop") {
            reject_execution_policy_options(cli, "interop");
            if (cli.output) throw std::runtime_error("interop does not accept -o");
            emojineer::Chunk chunk;
            if (cli.input->extension() == ".emjbc") {
                if (!cli.cer.empty()) throw std::runtime_error("bytecode interop inspection does not use source CER");
                chunk = read_chunk(*cli.input);
            } else {
                chunk = emojineer::compile_file(*cli.input, std::move(registry));
            }
            print_interop_contract(chunk);
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
            emojineer::VM vm(std::cin, std::cout, 1'000'000, execution_policy_for(cli), nullptr,
                               cli.program_arguments);
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
