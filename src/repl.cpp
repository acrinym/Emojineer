#include "emojineer/repl.hpp"

#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

namespace emojineer {
namespace {

Chunk compile_session(const std::string& source, const CustomEmojiRegistry& registry) {
    Lexer lexer(source, registry);
    Parser parser(lexer.tokenize());
    Compiler compiler;
    return compiler.compile(parser.parse());
}

void help(std::ostream& out) {
    out << ":run      compile and execute the current session\n"
        << ":show     print the current session source\n"
        << ":explain  explain the current session tokens\n"
        << ":clear    clear the session\n"
        << ":help     show REPL commands\n"
        << ":quit     exit the REPL\n";
}

} // namespace

int run_repl(std::istream& input, std::ostream& output, std::ostream& errors,
             CustomEmojiRegistry registry) {
    std::string session;
    std::string line;

    output << "Emojineer 0.14 REPL\n"
           << "Enter Emojineer source, then :run. Type :help for commands.\n";

    while (true) {
        output << (session.empty() ? "emoji> " : "...> ");
        output.flush();

        if (!std::getline(input, line)) {
            output << '\n';
            return 0;
        }

        if (line == ":quit") return 0;
        if (line == ":help") {
            help(output);
            continue;
        }
        if (line == ":clear") {
            session.clear();
            output << "✅ session cleared\n";
            continue;
        }
        if (line == ":show") {
            output << (session.empty() ? "(empty)\n" : session);
            continue;
        }
        if (line == ":explain") {
            if (session.empty()) {
                output << "(empty)\n";
                continue;
            }
            try {
                output << Lexer(session, registry).explain();
            } catch (const std::exception& error) {
                errors << "emojineer: " << error.what() << '\n';
            }
            continue;
        }
        if (line == ":run") {
            if (session.empty()) {
                output << "(empty)\n";
                continue;
            }
            try {
                Chunk chunk = compile_session(session, registry);
                VM vm(input, output);
                vm.execute(chunk);
            } catch (const std::exception& error) {
                errors << "emojineer: " << error.what() << '\n';
            }
            continue;
        }

        session += line;
        session.push_back('\n');
    }
}

} // namespace emojineer
