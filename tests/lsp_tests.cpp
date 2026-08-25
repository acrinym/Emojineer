#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/source_tools.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>

using namespace emojineer;

// Test basic document parsing
void test_document_parsing() {
    std::cout << "Testing document parsing..." << std::endl;
    
    // Test parsing valid Emojineer source
    std::string source = "🧑‍💻 x = 42\n📝 x";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    assert(!tokens.empty());
    
    Parser parser(std::move(tokens));
    auto program = parser.parse();
    assert(!program.statements.empty());
    
    std::cout << "Document parsing tests passed." << std::endl;
}

// Test source formatting
void test_formatting() {
    std::cout << "Testing formatting..." << std::endl;
    
    // Test formatting Emojineer source
    std::string source = "🧑‍💻    x    =   42";
    std::string formatted = format_source(source);
    
    // The formatter should produce valid output
    assert(!formatted.empty());
    
    std::cout << "Formatting tests passed." << std::endl;
}

// Test lexer explain functionality (for hover)
void test_hover_info() {
    std::cout << "Testing hover info..." << std::endl;
    
    // Test explaining tokens
    std::string source = "🧑‍💻 x = 42";
    Lexer lexer(source);
    std::string explanation = lexer.explain();
    
    // The explain output should contain token information
    assert(!explanation.empty());
    
    std::cout << "Hover info tests passed." << std::endl;
}

// Test document symbols extraction
void test_document_symbols() {
    std::cout << "Testing document symbols..." << std::endl;
    
    std::string source = "🧑‍💻 x = 42\n🛠️ foo() 📦 1";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto program = parser.parse();
    
    // Should have at least one statement
    assert(!program.statements.empty());
    
    std::cout << "Document symbols tests passed." << std::endl;
}

int main() {
    std::cout << "=== Emojineer LSP Support Tests ===" << std::endl;
    
    test_document_parsing();
    test_formatting();
    test_hover_info();
    test_document_symbols();
    
    std::cout << "=== All LSP support tests passed! ===" << std::endl;
    return 0;
}
