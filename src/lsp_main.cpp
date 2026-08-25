#include "emojineer/lsp.hpp"

#include <iostream>

int main(int argc, char** argv) {
    emojineer::lsp::LanguageServer server;
    return server.run();
}
