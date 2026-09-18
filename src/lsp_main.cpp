#include "emojineer/lsp.hpp"
#include "emojineer/version.hpp"

#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "emojineer-lsp " << emojineer::version << '\n';
        return 0;
    }
    if (argc != 1) {
        std::cerr << "usage: emojineer-lsp [--version]\n";
        return 2;
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    emojineer::lsp::LanguageServer server;
    return server.run();
}
