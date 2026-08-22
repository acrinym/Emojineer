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

namespace {
std::string read_text(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot open '"+path.string()+"'");return{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
emojineer::Chunk compile_source(const std::string& source){emojineer::Lexer lexer(source);emojineer::Parser parser(lexer.tokenize());emojineer::Compiler compiler;return compiler.compile(parser.parse());}
void usage(){std::cerr<<"Emojineer 0.1\nusage:\n  emojineer run <file.emoji>\n  emojineer compile <file.emoji> [-o file.emjbc]\n  emojineer exec <file.emjbc>\n  emojineer check <file.emoji>\n  emojineer explain <file.emoji>\n";}
}
int main(int argc,char**argv){try{if(argc<3){usage();return 2;}const std::string command=argv[1];const std::filesystem::path input_path=argv[2];
if(command=="explain"){std::cout<<emojineer::Lexer(read_text(input_path)).explain();return 0;}
if(command=="check"){(void)compile_source(read_text(input_path));std::cout<<"✅ "<<input_path.string()<<" is valid Emojineer source\n";return 0;}
if(command=="run"){auto chunk=compile_source(read_text(input_path));emojineer::VM vm(std::cin,std::cout);vm.execute(chunk);return 0;}
if(command=="compile"){auto chunk=compile_source(read_text(input_path));std::filesystem::path output_path=input_path;output_path.replace_extension(".emjbc");if(argc==5){if(std::string(argv[3])!="-o")throw std::runtime_error("expected -o before output path");output_path=argv[4];}else if(argc!=3)throw std::runtime_error("compile accepts only optional '-o <file.emjbc>'");std::ofstream output(output_path,std::ios::binary);if(!output)throw std::runtime_error("cannot write '"+output_path.string()+"'");emojineer::write_bytecode(chunk,output);std::cout<<"✅ wrote "<<output_path.string()<<'\n';return 0;}
if(command=="exec"){std::ifstream input(input_path,std::ios::binary);if(!input)throw std::runtime_error("cannot open '"+input_path.string()+"'");auto chunk=emojineer::read_bytecode(input);emojineer::VM vm(std::cin,std::cout);vm.execute(chunk);return 0;}
usage();return 2;}catch(const std::exception&error){std::cerr<<"emojineer: "<<error.what()<<'\n';return 1;}}
