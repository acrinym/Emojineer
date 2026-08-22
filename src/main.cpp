#include "emojineer/bytecode.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace {std::string read_text(const std::filesystem::path&p){std::ifstream i(p,std::ios::binary);if(!i)throw std::runtime_error("cannot open '"+p.string()+"'");return{std::istreambuf_iterator<char>(i),std::istreambuf_iterator<char>()};}emojineer::Chunk compile_source(const std::string&s){emojineer::Lexer l(s);emojineer::Parser p(l.tokenize());emojineer::Compiler c;return c.compile(p.parse());}void usage(){std::cerr<<"Emojineer 0.2\nusage:\n  emojineer run <file.emoji>\n  emojineer compile <file.emoji> [-o file.emjbc]\n  emojineer exec <file.emjbc>\n  emojineer check <file.emoji>\n  emojineer explain <file.emoji>\n";}}
int main(int argc,char**argv){try{if(argc<3){usage();return 2;}std::string cmd=argv[1];std::filesystem::path p=argv[2];if(cmd=="explain"){std::cout<<emojineer::Lexer(read_text(p)).explain();return 0;}if(cmd=="check"){(void)compile_source(read_text(p));std::cout<<"✅ "<<p.string()<<" is valid Emojineer source\n";return 0;}if(cmd=="run"){auto c=compile_source(read_text(p));emojineer::VM vm(std::cin,std::cout);vm.execute(c);return 0;}if(cmd=="compile"){auto c=compile_source(read_text(p));std::filesystem::path o=p;o.replace_extension(".emjbc");if(argc==5){if(std::string(argv[3])!="-o")throw std::runtime_error("expected -o before output path");o=argv[4];}else if(argc!=3)throw std::runtime_error("compile accepts only optional '-o <file.emjbc>'");std::ofstream out(o,std::ios::binary);if(!out)throw std::runtime_error("cannot write '"+o.string()+"'");emojineer::write_bytecode(c,out);std::cout<<"✅ wrote "<<o.string()<<'\n';return 0;}if(cmd=="exec"){std::ifstream in(p,std::ios::binary);if(!in)throw std::runtime_error("cannot open '"+p.string()+"'");auto c=emojineer::read_bytecode(in);emojineer::VM vm(std::cin,std::cout);vm.execute(c);return 0;}usage();return 2;}catch(const std::exception&e){std::cerr<<"emojineer: "<<e.what()<<'\n';return 1;}}
