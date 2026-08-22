#include "emojineer/lexer.hpp"
#include "emojineer/unicode.hpp"
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace emojineer { namespace {
bool digit(const Grapheme& g){return g.display.size()==1&&std::isdigit(static_cast<unsigned char>(g.display[0]));}
bool space(const Grapheme& g){return g.display==" "||g.display=="\t"||g.display=="\r";}
std::string normalize_newlines(std::string s){std::string o;o.reserve(s.size());for(size_t i=0;i<s.size();++i){if(s[i]=='\r'){if(i+1<s.size()&&s[i+1]=='\n')++i;o+='\n';}else o+=s[i];}return o;}
std::string join_display(const std::vector<Grapheme>& gs,size_t i,size_t n){std::string s;for(size_t k=0;k<n;++k)s+=gs[i+k].display;return s;}
std::string join_canonical(const std::vector<Grapheme>& gs,size_t i,size_t n){std::string s;for(size_t k=0;k<n;++k)s+=gs[i+k].canonical;return s;}
} // namespace

std::string token_kind_name(TokenKind k){switch(k){
#define K(x) case TokenKind::x:return #x
K(Eof);K(Newline);K(Number);K(String);K(Identifier);K(Var);K(Assign);K(Print);K(If);K(Else);K(While);K(End);K(Input);K(True);K(False);K(TypeNumber);K(TypeString);K(TypeBool);K(Function);K(Return);K(Add);K(Subtract);K(Multiply);K(Divide);K(Modulo);K(Equal);K(Less);K(Greater);K(Not);K(GroupStart);K(GroupEnd);
#undef K
}return"Unknown";}

Lexer::Lexer(std::string source):source_(normalize_newlines(std::move(source))),registry_(){}
Lexer::Lexer(std::string source,CustomEmojiRegistry registry):source_(normalize_newlines(std::move(source))),registry_(std::move(registry)){}

std::vector<Token> Lexer::tokenize() const {
    const auto gs=segment_graphemes(source_);
    const std::string fence=canonicalize_token("📜"),comment=canonicalize_token("💭");
    std::vector<Token> out;size_t line=1,col=1;
    for(size_t i=0;i<gs.size();){const auto&g=gs[i];
        if(g.display=="\n"){out.push_back({TokenKind::Newline,"\n","\n","",line,col});++line;col=1;++i;continue;}
        if(space(g)){++col;++i;continue;}
        if(g.canonical==comment){while(i<gs.size()&&gs[i].display!="\n"){++i;++col;}continue;}
        if(g.canonical==fence){size_t sl=line,sc=col;std::string lit,lex=g.display;++i;++col;bool closed=false;while(i<gs.size()){const auto&p=gs[i];if(p.canonical==fence){lex+=p.display;++i;++col;closed=true;break;}if(p.display=="\n"){++line;col=1;}else ++col;lit+=p.display;lex+=p.display;++i;}if(!closed)throw std::runtime_error("line "+std::to_string(sl)+", column "+std::to_string(sc)+": unterminated 📜 string literal");out.push_back({TokenKind::String,lex,fence,lit,sl,sc});continue;}
        if(digit(g)){size_t sc=col;bool dot=false;std::string num;while(i<gs.size()){const auto&p=gs[i];if(digit(p)){num+=p.display;++i;++col;continue;}if(p.display=="."&&!dot){dot=true;num+='.';++i;++col;continue;}break;}if(!num.empty()&&num.back()=='.')throw std::runtime_error("line "+std::to_string(line)+": number cannot end with '.'");out.push_back({TokenKind::Number,num,num,num,line,sc});continue;}
        size_t consumed=0;const auto* def=registry_.match(gs,i,consumed);
        if(def){const size_t sc=col;std::string lex=join_display(gs,i,consumed),canon=join_canonical(gs,i,consumed);out.push_back({def->kind,lex,canon,"",line,sc});i+=consumed;col+=consumed;continue;}
        if(is_emoji_grapheme(g.canonical)){out.push_back({TokenKind::Identifier,g.display,g.canonical,g.canonical,line,col});++i;++col;continue;}
        std::ostringstream msg;msg<<"line "<<line<<", column "<<col<<": unexpected grapheme '"<<g.display<<"' ("<<codepoints_hex(g.display)<<")";throw std::runtime_error(msg.str());
    }
    out.push_back({TokenKind::Eof,"","","",line,col});return out;
}

std::string Lexer::explain() const {
    std::ostringstream out;
    for(const Token&t:tokenize()){
        if(t.kind==TokenKind::Eof||t.kind==TokenKind::Newline)continue;
        out<<t.line<<':'<<t.column<<"  "<<t.lexeme<<"  →  ";
        auto gs=segment_graphemes(t.lexeme);size_t n=0;const auto*d=registry_.match(gs,0,n);
        if(d&&n==gs.size()){
            out<<d->description;
            if(!d->alias.empty())out<<" ["<<d->alias<<"]";
            out<<" [id 0x"<<std::hex<<std::uppercase<<d->semantic_id<<std::dec<<']';
        }else if(t.kind==TokenKind::Identifier)out<<"emoji identifier [canonical "<<codepoints_hex(t.canonical)<<']';
        else if(t.kind==TokenKind::Number||t.kind==TokenKind::String)out<<(t.kind==TokenKind::Number?"numeric literal":"text literal")<<" = "<<t.literal;
        else out<<registry_.describe(t.kind);
        out<<'\n';
    }return out.str();
}
} // namespace emojineer
