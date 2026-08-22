#include "emojineer/repl.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

int main() {
    try {
        const std::string script =
            "🐍 👤 🔤 🟰 📥\n"
            "📝 👤\n"
            ":run\n"
            "Ada\n"
            ":quit\n";
        std::istringstream input(script);
        std::ostringstream output;
        std::ostringstream errors;

        const int result = emojineer::run_repl(input, output, errors);
        if (result != 0) throw std::runtime_error("REPL returned nonzero status");
        if (!errors.str().empty()) throw std::runtime_error("REPL emitted error: " + errors.str());
        if (output.str().find("Ada\n") == std::string::npos) {
            throw std::runtime_error("shared REPL/VM stream did not deliver the line after :run to 📥");
        }

        std::cout << "✅ REPL shared-stream input test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
