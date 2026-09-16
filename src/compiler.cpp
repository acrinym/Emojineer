#include "emojineer/compiler.hpp"
#include "emojineer/capability.hpp"
#include "emojineer/interop.hpp"
#include "emojineer/source_diagnostic.hpp"
#include <cstdint>
#include <stdexcept>
namespace emojineer {
namespace {
InteropType interop_type_for(ast::DeclaredType type) {
    switch (type) {
        case ast::DeclaredType::Number: return InteropType::Number;
        case ast::DeclaredType::String: return InteropType::String;
        case ast::DeclaredType::Bool: return InteropType::Bool;
        case ast::DeclaredType::Array: return InteropType::Array;
    }
    throw std::runtime_error("invalid interop source type");
}
InteropSignature interop_signature_for(const std::vector<ast::DeclaredType>& parameters, ast::DeclaredType result) {
    InteropSignature signature; signature.result = interop_type_for(result);
    for (const auto type : parameters) signature.parameters.push_back(interop_type_for(type));
    return signature;
}
} // namespace

Chunk Compiler::compile(const ast::Program& program){
    chunk_={};
    chunk_.source_map.clear();
    declared_types_.clear();
    function_indices_.clear();
    interop_import_indices_.clear();
    for(const auto&s:program.statements){
        if(auto*f=dynamic_cast<const ast::FunctionDecl*>(s.get())){
            if(auto native=native_facility_from_identifier(f->name))
                throw SourceLocationException("reserved native facility emoji '"+native_facility_glyph(*native)+"' cannot be defined as a function",{},f->line,1,f->name);
            if(function_indices_.find(f->name) != function_indices_.end())throw SourceLocationException("duplicate function '"+f->name+"'",{},f->line,1,f->name);
            std::size_t idx=chunk_.functions.size(); function_indices_[f->name]=idx;
            FunctionInfo fi; fi.name=f->name; fi.arity=static_cast<std::uint32_t>(f->parameters.size()); fi.local_count=fi.arity; fi.parameter_names=f->parameters;
            chunk_.functions.push_back(std::move(fi));
        }
    }
    for(const auto&s:program.statements){
        if(auto*decl=dynamic_cast<const ast::InteropImportDecl*>(s.get())){
            if(native_facility_from_identifier(decl->name))throw SourceLocationException("interop import cannot shadow a reserved native facility",{},decl->line,1,decl->name);
            if(function_indices_.contains(decl->name))throw SourceLocationException("interop import collides with function '"+decl->name+"'",{},decl->line,1,decl->name);
            if(interop_import_indices_.contains(decl->name))throw SourceLocationException("duplicate interop import '"+decl->name+"'",{},decl->line,1,decl->name);
            validate_interop_external_name(decl->external_name);
            for(const auto&existing:chunk_.interop_imports)if(existing.external_name==decl->external_name)
                throw SourceLocationException("duplicate interop import external name '"+decl->external_name+"' in a single compilation unit",{},decl->line,1,decl->name);
            InteropImportInfo info; info.internal_name=decl->name; info.external_name=decl->external_name;
            info.required_capabilities=parse_interop_capability_spec(decl->capability_spec);
            info.signature=interop_signature_for(decl->parameter_types,decl->result_type);
            const auto index=chunk_.interop_imports.size(); chunk_.interop_imports.push_back(std::move(info)); interop_import_indices_[decl->name]=index;
        }
    }
    for(const auto&s:program.statements){
        if(auto*decl=dynamic_cast<const ast::InteropExportDecl*>(s.get())){
            validate_interop_external_name(decl->external_name);
            const auto function=function_indices_.find(decl->function_name);
            if(function==function_indices_.end())throw SourceLocationException("interop export references undefined function '"+decl->function_name+"'",{},decl->line,1,decl->function_name);
            auto signature=interop_signature_for(decl->parameter_types,decl->result_type);
            if(signature.parameters.size()!=chunk_.functions[function->second].arity)throw SourceLocationException("interop export signature arity does not match function",{},decl->line,1,decl->function_name);
            chunk_.interop_exports.push_back({decl->external_name,static_cast<std::uint32_t>(function->second),std::move(signature)});
        }
    }
    for(const auto&s:program.statements){
        if(dynamic_cast<const ast::FunctionDecl*>(s.get())||dynamic_cast<const ast::InteropImportDecl*>(s.get())||dynamic_cast<const ast::InteropExportDecl*>(s.get()))continue;
        compile_stmt(*s);
    }
    emit(OpCode::Halt,0,static_cast<std::size_t>(1));
    for(const auto&s:program.statements)if(auto*f=dynamic_cast<const ast::FunctionDecl*>(s.get()))compile_function(*f,function_indices_.at(f->name));
    verify_bytecode(chunk_); return chunk_;
}
void Compiler::collect_locals(const std::vector<ast::StmtPtr>& block){for(const auto&s:block){if(auto*v=dynamic_cast<const ast::VarDecl*>(s.get())){if(locals_.find(v->name) != locals_.end())throw SourceLocationException("duplicate function-local variable '"+v->name+"'",{},v->line,1,v->name);auto slot=static_cast<std::int32_t>(locals_.size());locals_[v->name]=slot;local_types_[v->name]=v->declared_type;}else if(auto*i=dynamic_cast<const ast::IfStmt*>(s.get())){collect_locals(i->then_branch);collect_locals(i->else_branch);}else if(auto*w=dynamic_cast<const ast::WhileStmt*>(s.get()))collect_locals(w->body);else if(dynamic_cast<const ast::FunctionDecl*>(s.get()))throw SourceLocationException("nested functions are not supported",{},s->line,1);}}
void Compiler::compile_function(const ast::FunctionDecl& fn,std::size_t index){in_function_=true;current_function_name_=fn.name;locals_.clear();local_types_.clear();for(const auto&p:fn.parameters){if(locals_.find(p) != locals_.end())throw SourceLocationException("duplicate function parameter '"+p+"'",{},fn.line,1,p);locals_[p]=static_cast<std::int32_t>(locals_.size());local_types_[p]=std::nullopt;}collect_locals(fn.body);chunk_.functions[index].entry=static_cast<std::uint32_t>(chunk_.code.size());chunk_.functions[index].local_count=static_cast<std::uint32_t>(locals_.size());chunk_.functions[index].local_names.resize(locals_.size());for(const auto&[name,slot]:locals_){chunk_.functions[index].local_names[static_cast<std::size_t>(slot)]=name;}compile_block(fn.body);emit(OpCode::Constant,chunk_.add_constant(false),&fn.source);emit(OpCode::Return,0,&fn.source);locals_.clear();local_types_.clear();in_function_=false;current_function_name_.clear();}
void Compiler::compile_block(const std::vector<ast::StmtPtr>& block){for(const auto&s:block)compile_stmt(*s);}
std::optional<std::int32_t> Compiler::local_slot(const std::string&name)const{if(!in_function_)return std::nullopt;auto it=locals_.find(name);if(it==locals_.end())return std::nullopt;return it->second;}
void Compiler::compile_stmt(const ast::Stmt& stmt){if(dynamic_cast<const ast::InteropImportDecl*>(&stmt)||dynamic_cast<const ast::InteropExportDecl*>(&stmt))throw SourceLocationException(TopLevelOnlyMessage,{},stmt.line,1);if(dynamic_cast<const ast::ModuleDecl*>(&stmt)||dynamic_cast<const ast::ImportStmt*>(&stmt)||dynamic_cast<const ast::ExportStmt*>(&stmt))throw SourceLocationException("🧩/🔗/📤 module syntax requires file-based compilation",{},stmt.line,1);if(const auto*v=dynamic_cast<const ast::VarDecl*>(&stmt)){compile_expr(*v->initializer);emit_type_assert(v->declared_type,v->line);if(auto slot=local_slot(v->name))emit(OpCode::StoreLocal,*slot,&v->source);else{if(declared_types_.find(v->name) != declared_types_.end())throw SourceLocationException("duplicate global variable '"+v->name+"'",{},v->line,1,v->name);emit(OpCode::StoreGlobal,name_constant(v->name),&v->source);declared_types_[v->name]=v->declared_type;}return;}if(const auto*a=dynamic_cast<const ast::Assignment*>(&stmt)){compile_expr(*a->value);if(auto slot=local_slot(a->name)){emit_type_assert(local_types_.at(a->name),a->line);emit(OpCode::StoreLocal,*slot,&a->source);}else{auto it=declared_types_.find(a->name);if(it!=declared_types_.end())emit_type_assert(it->second,a->line);emit(OpCode::StoreGlobal,name_constant(a->name),&a->source);}return;}if(const auto*p=dynamic_cast<const ast::PrintStmt*>(&stmt)){compile_expr(*p->expression);emit(OpCode::Print,0,&p->source);return;}if(const auto*r=dynamic_cast<const ast::ReturnStmt*>(&stmt)){if(!in_function_)throw SourceLocationException("📦 return is only valid inside 🛠️ functions",{},r->line,1);compile_expr(*r->expression);emit(OpCode::Return,0,&r->source);return;}if(dynamic_cast<const ast::FunctionDecl*>(&stmt))throw SourceLocationException("nested functions are not supported",{},stmt.line,1);if(const auto*f=dynamic_cast<const ast::IfStmt*>(&stmt)){compile_expr(*f->condition);std::size_t fj=chunk_.code.size();emit(OpCode::JumpIfFalse,0,&f->source);compile_block(f->then_branch);if(!f->else_branch.empty()){std::size_t ej=chunk_.code.size();emit(OpCode::Jump,0,&f->source);chunk_.code[fj].operand=static_cast<std::int32_t>(chunk_.code.size());compile_block(f->else_branch);chunk_.code[ej].operand=static_cast<std::int32_t>(chunk_.code.size());}else chunk_.code[fj].operand=static_cast<std::int32_t>(chunk_.code.size());return;}if(const auto*w=dynamic_cast<const ast::WhileStmt*>(&stmt)){std::size_t start=chunk_.code.size();compile_expr(*w->condition);std::size_t exit=chunk_.code.size();emit(OpCode::JumpIfFalse,0,&w->source);compile_block(w->body);emit(OpCode::Jump,static_cast<std::int32_t>(start),&w->source);chunk_.code[exit].operand=static_cast<std::int32_t>(chunk_.code.size());return;}throw SourceLocationException("compiler encountered unknown statement node",{},stmt.line,1);}
void Compiler::compile_expr(const ast::Expr& expr){if(const auto*l=dynamic_cast<const ast::LiteralExpr*>(&expr)){std::visit([&](const auto&v){emit(OpCode::Constant,chunk_.add_constant(Value{v}),&expr.source);},l->value);return;}if(const auto*v=dynamic_cast<const ast::VariableExpr*>(&expr)){if(auto slot=local_slot(v->name))emit(OpCode::LoadLocal,*slot,&expr.source);else emit(OpCode::LoadGlobal,name_constant(v->name),&expr.source);return;}if(dynamic_cast<const ast::InputExpr*>(&expr)){emit(OpCode::ReadLine,0,&expr.source);return;}if(const auto*a=dynamic_cast<const ast::ArrayExpr*>(&expr)){if(a->elements.size()>static_cast<std::size_t>(INT32_MAX))throw SourceLocationException("array literal has too many elements",{},expr.line,1);for(const auto&e:a->elements)compile_expr(*e);emit(OpCode::MakeArray,static_cast<std::int32_t>(a->elements.size()),&expr.source);return;}if(const auto*i=dynamic_cast<const ast::IndexExpr*>(&expr)){compile_expr(*i->collection);compile_expr(*i->index);emit(OpCode::Index,0,&expr.source);return;}if(const auto*l=dynamic_cast<const ast::LengthExpr*>(&expr)){compile_expr(*l->value);emit(OpCode::Length,0,&expr.source);return;}if(const auto*a=dynamic_cast<const ast::AppendExpr*>(&expr)){compile_expr(*a->collection);compile_expr(*a->value);emit(OpCode::Append,0,&expr.source);return;}if(const auto*s=dynamic_cast<const ast::SetIndexExpr*>(&expr)){compile_expr(*s->collection);compile_expr(*s->index);compile_expr(*s->value);emit(OpCode::SetIndex,0,&expr.source);return;}if(const auto*c=dynamic_cast<const ast::CallExpr*>(&expr)){if(auto native=native_facility_from_identifier(c->callee)){const auto expected=native_facility_arity(*native);if(c->arguments.size()!=expected)throw SourceLocationException("native facility '"+native_facility_name(*native)+"' expects "+std::to_string(expected)+" arguments, got "+std::to_string(c->arguments.size()),{},expr.line,1,c->callee);for(const auto&a:c->arguments)compile_expr(*a);chunk_.required_capabilities|=capability_mask(native_facility_capability(*native));emit(OpCode::HostCall,static_cast<std::int32_t>(*native),&expr.source);return;}if(auto imported=interop_import_indices_.find(c->callee);imported!=interop_import_indices_.end()){const auto&meta=chunk_.interop_imports[imported->second];if(c->arguments.size()!=meta.signature.parameters.size())throw SourceLocationException("interop import '"+meta.external_name+"' expects "+std::to_string(meta.signature.parameters.size())+" arguments, got "+std::to_string(c->arguments.size()),{},expr.line,1,c->callee);for(const auto&a:c->arguments)compile_expr(*a);chunk_.required_capabilities|=meta.required_capabilities;emit(OpCode::InteropCall,static_cast<std::int32_t>(imported->second),&expr.source);return;}auto it=function_indices_.find(c->callee);if(it==function_indices_.end())throw SourceLocationException("undefined function '"+c->callee+"'",{},expr.line,1,c->callee);const auto&meta=chunk_.functions[it->second];if(c->arguments.size()!=meta.arity)throw SourceLocationException("function '"+c->callee+"' expects "+std::to_string(meta.arity)+" arguments, got "+std::to_string(c->arguments.size()),{},expr.line,1,c->callee);for(const auto&a:c->arguments)compile_expr(*a);emit(OpCode::Call,static_cast<std::int32_t>(it->second),&expr.source);return;}if(const auto*u=dynamic_cast<const ast::UnaryExpr*>(&expr)){compile_expr(*u->right);if(u->op==TokenKind::Subtract)emit(OpCode::Negate,0,&expr.source);else if(u->op==TokenKind::Not)emit(OpCode::Not,0,&expr.source);else throw SourceLocationException("unsupported unary operator",{},expr.line,1);return;}if(const auto*b=dynamic_cast<const ast::BinaryExpr*>(&expr)){compile_expr(*b->left);compile_expr(*b->right);switch(b->op){case TokenKind::Add:emit(OpCode::Add,0,&expr.source);break;case TokenKind::Subtract:emit(OpCode::Subtract,0,&expr.source);break;case TokenKind::Multiply:emit(OpCode::Multiply,0,&expr.source);break;case TokenKind::Divide:emit(OpCode::Divide,0,&expr.source);break;case TokenKind::Modulo:emit(OpCode::Modulo,0,&expr.source);break;case TokenKind::Equal:emit(OpCode::Equal,0,&expr.source);break;case TokenKind::Less:emit(OpCode::Less,0,&expr.source);break;case TokenKind::Greater:emit(OpCode::Greater,0,&expr.source);break;default:throw SourceLocationException("unsupported binary operator",{},expr.line,1);}return;}throw SourceLocationException("compiler encountered unknown expression node",{},expr.line,1);}
void Compiler::emit(OpCode op,std::int32_t operand,std::size_t line,std::size_t column){
    chunk_.code.push_back({op,operand,static_cast<std::uint32_t>(line)});
    chunk_.source_map.push_back({source_path_, static_cast<std::uint32_t>(line), static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(line), static_cast<std::uint32_t>(column), current_function_name_});
}
void Compiler::emit(OpCode op, std::int32_t operand, const ast::SourceRange* source) {
    std::uint32_t line = source ? static_cast<std::uint32_t>(source->line) : 1;
    std::uint32_t column = source ? static_cast<std::uint32_t>(source->column) : 1;
    std::uint32_t end_line = source ? static_cast<std::uint32_t>(source->end_line) : line;
    std::uint32_t end_column = source ? static_cast<std::uint32_t>(source->end_column) : column;
    std::string module_path = source && !source->module_identity.empty() ? source->module_identity : source_path_;
    chunk_.code.push_back({op, operand, line});
    chunk_.source_map.push_back({module_path, line, column, end_line, end_column, current_function_name_});
}
std::int32_t Compiler::name_constant(const std::string&name){return chunk_.add_constant(name);}
void Compiler::emit_type_assert(std::optional<ast::DeclaredType>type,std::size_t line){if(!type)return;switch(*type){case ast::DeclaredType::Number:emit(OpCode::AssertNumber,0,line);break;case ast::DeclaredType::String:emit(OpCode::AssertString,0,line);break;case ast::DeclaredType::Bool:emit(OpCode::AssertBool,0,line);break;case ast::DeclaredType::Array:emit(OpCode::AssertArray,0,line);break;}}
}
