#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"
#include "emojineer/disassembler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/module.hpp"
#include "emojineer/repl.hpp"
#include "emojineer/source_tools.hpp"
#include "emojineer/stdlib.hpp"
#include "emojineer/vm.hpp"

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
};

void usage() {
    std::cerr
        << "Emojineer 0.12\n"
        << "usage:\n"
        << "  emojineer repl [--cer registry.json ...]\n"
        << "  emojineer stdlib\n"
        << "  emojineer <run|check|explain|dump|lint> <file.emoji> [--cer registry.json ...]\n"
        << "  emojineer fmt <file.emoji> [-o file.emoji] [--cer registry.json ...]\n"
        << "  emojineer compile <file.emoji> [-o file.emjbc] [--cer registry.json ...]\n"
        << "  emojineer <exec|disasm> <file.emjbc>\n";
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
        } else {
            throw std::runtime_error("unknown option '" + arg + "'");
        }
    }

    return cli;
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
            return emojineer::run_repl(std::cin, std::cout, std::cerr, registry_for(cli));
        }

        if (cli.command == "stdlib") {
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
                emojineer::verify_bytecode(chunk);
                emojineer::disassemble(chunk, std::cout);
                return 0;
            }
            emojineer::VM vm(std::cin, std::cout);
            vm.execute(chunk);
            return 0;
        }

        auto registry = registry_for(cli);

        if (cli.command == "fmt") {
            const std::string formatted =
                emojineer::format_source(read_text(*cli.input), std::move(registry));
            if (cli.output) write_text(*cli.output, formatted);
            else std::cout << formatted;
            return 0;
        }

        if (cli.command == "lint") {
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
            if (cli.output) throw std::runtime_error("explain does not accept -o");
            std::cout << emojineer::Lexer(read_text(*cli.input), std::move(registry)).explain();
            return 0;
        }

        if (cli.command == "check") {
            if (cli.output) throw std::runtime_error("check does not accept -o");
            (void)emojineer::compile_file(*cli.input, std::move(registry));
            std::cout << "✅ " << cli.input->string() << " is valid Emojineer source\n";
            return 0;
        }

        if (cli.command == "run") {
            if (cli.output) throw std::runtime_error("run does not accept -o");
            auto chunk = emojineer::compile_file(*cli.input, std::move(registry));
            emojineer::VM vm(std::cin, std::cout);
            vm.execute(chunk);
            return 0;
        }

        if (cli.command == "dump") {
            if (cli.output) throw std::runtime_error("dump does not accept -o");
            auto chunk = emojineer::compile_file(*cli.input, std::move(registry));
            emojineer::disassemble(chunk, std::cout);
            return 0;
        }

        if (cli.command == "compile") {
            auto chunk = emojineer::compile_file(*cli.input, std::move(registry));
            auto output_path = cli.output.value_or(*cli.input);
            if (!cli.output) output_path.replace_extension(".emjbc");
            std::ofstream output(output_path, std::ios::binary);
            if (!output) throw std::runtime_error("cannot write '" + output_path.string() + "'");
            emojineer::write_bytecode(chunk, output);
            std::cout << "✅ wrote " << output_path.string() << '\n';
            return 0;
        }

        usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "emojineer: " << error.what() << '\n';
        return 1;
    }
}
