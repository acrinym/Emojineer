#include "emojineer/bytecode.hpp"
#include "emojineer/cer.hpp"
#include "emojineer/compiler.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
std::string read_text(const std::filesystem::path&p){std::ifstream i(p,std::ios::binary);if(!i)throw std::runtime_error("cannot open '"+p.string()+"'");return{std::istreambuf_iterator<char>(i),{}};}
struct Cli{std::string command;std::filesystem::path input;std::vector<std::string> cer;std::optional<std::filesystem::path> output;};
void usage(){std::cerr<<"Emojineer 0.3\nusage:\n  emojineer <run|check|explain> <file.emoji> [--cer registry.json ...]\n  emojineer compile <file.emoji> [-o file.emjbc] [--cer registry.json ...]\n  emojineer exec <file.emjbc>\n";}
Cli parse_cli(int argc,char**argv){if(argc<3){usage();throw std::runtime_error("missing command or input");}Cli c{argv[1],argv[2],{},std::nullopt};for(int i=3;i<argc;++i){std::string a=argv[i];if(a=="--cer"){if(++i>=argc)throw std::runtime_error("--cer requires a registry path");c.cer.push_back(argv[i]);}else if(a=="-o"){if(++i>=argc)throw std::runtime_error("-o requires an output path");c.output=std::filesystem::path(argv[i]);}else throw std::runtime_error("unknown option '"+a+"'");}return c;}
emojineer::CustomEmojiRegistry registry_for(const Cli&c){emojineer::CustomEmojiRegistry r;for(const auto&p:c.cer)r.load_file(p);return r;}
emojineer::Chunk compile_source(const std::string&s,emojineer::CustomEmojiRegistry r){emojineer::Lexer l(s,std::move(r));emojineer::Parser p(l.tokenize());emojineer::Compiler c;return c.compile(p.parse());}
}
int main(int argc,char**argv){try{Cli c=parse_cli(argc,argv);if(c.command=="exec"){if(!c.cer.empty()||c.output)throw std::runtime_error("exec does not use source CER or -o options");std::ifstream in(c.input,std::ios::binary);if(!in)throw std::runtime_error("cannot open '"+c.input.string()+"'");auto chunk=emojineer::read_bytecode(in);emojineer::VM vm(std::cin,std::cout);vm.execute(chunk);return 0;}auto reg=registry_for(c);if(c.command=="explain"){std::cout<<emojineer::Lexer(read_text(c.input),std::move(reg)).explain();return 0;}if(c.command=="check"){(void)compile_source(read_text(c.input),std::move(reg));std::cout<<"✅ "<<c.input.string()<<" is valid Emojineer source\n";return 0;}if(c.command=="run"){auto chunk=compile_source(read_text(c.input),std::move(reg));emojineer::VM vm(std::cin,std::cout);vm.execute(chunk);return 0;}if(c.command=="compile"){auto chunk=compile_source(read_text(c.input),std::move(reg));auto outpath=c.output.value_or(c.input);if(!c.output)outpath.replace_extension(".emjbc");std::ofstream out(outpath,std::ios::binary);if(!out)throw std::runtime_error("cannot write '"+outpath.string()+"'");emojineer::write_bytecode(chunk,out);std::cout<<"✅ wrote "<<outpath.string()<<'\n';return 0;}usage();return 2;}catch(const std::exception&e){std::cerr<<"emojineer: "<<e.what()<<'\n';return 1;}}
