from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


def replace_between(text: str, start: str, end: str, replacement: str, label: str) -> str:
    a = text.find(start)
    if a < 0:
        raise SystemExit(f'{label}: start anchor missing')
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f'{label}: end anchor missing')
    return text[:a] + replacement + text[b:]

# -----------------------------------------------------------------------------
# EMJBC v7: deterministic per-source SHA-256 provenance and hard debug metadata
# validation while retaining degraded v1-v6 reads.
# -----------------------------------------------------------------------------
bh = Path('include/emojineer/bytecode.hpp')
text = bh.read_text(encoding='utf-8')
text = replace_once(text, '#include <variant>\n#include <vector>', '#include <variant>\n#include <unordered_map>\n#include <vector>', 'bytecode unordered_map include')
text = replace_once(
    text,
    '    std::vector<SourceLocation> source_map;  // Indexed by instruction pointer\n    std::int32_t add_constant(Value value);',
    '    std::vector<SourceLocation> source_map;  // Indexed by instruction pointer\n'
    '    // EMJBC v7: source identity -> SHA-256 of the exact source text used to compile.\n'
    '    // This is one digest per source, not one digest per instruction.\n'
    '    std::unordered_map<std::string, std::string> source_hashes;\n'
    '    std::int32_t add_constant(Value value);',
    'bytecode source hashes')
bh.write_text(text, encoding='utf-8')

bc = Path('src/bytecode.cpp')
text = bc.read_text(encoding='utf-8')
text = replace_once(text, '#include <cstdint>\n#include <iomanip>', '#include <cstdint>\n#include <cctype>\n#include <filesystem>\n#include <iomanip>', 'bytecode validation includes')
text = text.replace('constexpr std::uint16_t CurrentVersion=6;', 'constexpr std::uint16_t CurrentVersion=7;', 1)
text = replace_once(
    text,
    "constexpr std::uint32_t MaxConstants=1'000'000,MaxFunctions=100'000,MaxInstructions=10'000'000,MaxStringBytes=64*1024*1024;",
    "constexpr std::uint32_t MaxConstants=1'000'000,MaxFunctions=100'000,MaxInstructions=10'000'000,MaxStringBytes=64*1024*1024,MaxSourceFiles=1'000'000;",
    'bytecode source limit')
helper = r'''bool portable_source_identity(const std::string& identity) {
    if (identity.empty()) return true;
    if (std::filesystem::path(identity).is_absolute()) return false;
    if (identity.size() >= 3 && std::isalpha(static_cast<unsigned char>(identity[0])) &&
        identity[1] == ':' && (identity[2] == '/' || identity[2] == '\\')) return false;
    return true;
}
bool valid_sha256(const std::string& hash) {
    if (hash.size() != 64) return false;
    return std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
'''
anchor = 'std::string array_to_string(const ArrayPtr&a)'
if helper.strip() not in text:
    pos = text.find(anchor)
    if pos < 0: raise SystemExit('bytecode helper anchor missing')
    text = text[:pos] + helper + text[pos:]

verify = r'''void verify_bytecode(const Chunk& c) {
    if (c.constants.size() > MaxConstants || c.functions.size() > MaxFunctions || c.code.size() > MaxInstructions)
        throw std::runtime_error("bytecode exceeds safety limit");
    for (const auto& value : c.constants)
        if (std::holds_alternative<ArrayPtr>(value))
            throw std::runtime_error("array found in bytecode constant pool");

    std::unordered_set<std::string> names;
    for (const auto& f : c.functions) {
        if (f.entry >= c.code.size()) throw std::runtime_error("function entry is outside bytecode");
        if (f.arity > f.local_count) throw std::runtime_error("function arity exceeds local count");
        if (!f.parameter_names.empty() && f.parameter_names.size() != f.arity)
            throw std::runtime_error("function parameter-name metadata does not match arity");
        if (!f.local_names.empty() && f.local_names.size() != f.local_count)
            throw std::runtime_error("function local-name metadata does not match local count");
        if (!names.insert(f.name).second) throw std::runtime_error("duplicate function metadata");
    }

    for (const auto& ins : c.code) {
        auto valid_const = [&] { return ins.operand >= 0 && static_cast<std::size_t>(ins.operand) < c.constants.size(); };
        switch (ins.op) {
            case OpCode::Constant:
                if (!valid_const()) throw std::runtime_error("invalid constant operand");
                break;
            case OpCode::LoadGlobal:
            case OpCode::StoreGlobal:
                if (!valid_const() || !std::holds_alternative<std::string>(c.constants[static_cast<std::size_t>(ins.operand)]))
                    throw std::runtime_error("global operand must reference a string constant");
                break;
            case OpCode::LoadLocal:
            case OpCode::StoreLocal:
                if (ins.operand < 0) throw std::runtime_error("negative local slot");
                break;
            case OpCode::Jump:
            case OpCode::JumpIfFalse:
                if (ins.operand < 0 || static_cast<std::size_t>(ins.operand) > c.code.size())
                    throw std::runtime_error("invalid jump target");
                break;
            case OpCode::Call:
                if (ins.operand < 0 || static_cast<std::size_t>(ins.operand) >= c.functions.size())
                    throw std::runtime_error("invalid function index");
                break;
            case OpCode::MakeArray:
                if (ins.operand < 0) throw std::runtime_error("negative array element count");
                break;
            default:
                break;
        }
    }

    if (!c.source_map.empty()) {
        if (c.source_map.size() != c.code.size())
            throw std::runtime_error("source map cardinality does not match instruction stream");
        for (const auto& src : c.source_map) {
            if (!portable_source_identity(src.source_path))
                throw std::runtime_error("source map contains an absolute checkout-specific identity");
            if (src.line == 0 || src.column == 0 || src.end_line == 0 || src.end_column == 0)
                throw std::runtime_error("source map positions must be 1-based");
            if (src.end_line < src.line || (src.end_line == src.line && src.end_column < src.column))
                throw std::runtime_error("source map contains a reversed source range");
        }
    }

    if (c.source_hashes.size() > MaxSourceFiles)
        throw std::runtime_error("source hash table exceeds safety limit");
    for (const auto& [identity, hash] : c.source_hashes) {
        if (identity.empty() || !portable_source_identity(identity))
            throw std::runtime_error("source hash table contains an invalid deterministic identity");
        if (!valid_sha256(hash)) throw std::runtime_error("source hash table contains an invalid SHA-256 digest");
        if (!c.source_map.empty()) {
            const bool mapped = std::any_of(c.source_map.begin(), c.source_map.end(), [&](const SourceLocation& src) {
                return src.source_path == identity;
            });
            if (!mapped) throw std::runtime_error("source hash identity is absent from the source map");
        }
    }
}
'''
text = replace_between(text, 'void verify_bytecode(', 'void write_bytecode(', verify, 'verify_bytecode rewrite')

writer = r'''void write_bytecode(const Chunk& c, std::ostream& o) {
    verify_bytecode(c);
    o.write(Magic, sizeof(Magic));
    if (!o) throw std::runtime_error("failed to write bytecode header");
    write_u16(o, CurrentVersion);
    write_u32(o, static_cast<std::uint32_t>(c.constants.size()));
    for (const auto& v : c.constants) {
        if (auto* n = std::get_if<double>(&v)) { write_u8(o, 1); write_u64(o, std::bit_cast<std::uint64_t>(*n)); }
        else if (auto* integer = std::get_if<std::int64_t>(&v)) { write_u8(o, 4); write_u64(o, static_cast<std::uint64_t>(*integer)); }
        else if (auto* b = std::get_if<bool>(&v)) { write_u8(o, 2); write_u8(o, *b ? 1 : 0); }
        else if (auto* s = std::get_if<std::string>(&v)) { write_u8(o, 3); write_string(o, *s); }
        else throw std::runtime_error("unsupported constant value in bytecode");
    }
    write_u32(o, static_cast<std::uint32_t>(c.functions.size()));
    for (const auto& f : c.functions) {
        write_string(o, f.name); write_u32(o, f.entry); write_u32(o, f.arity); write_u32(o, f.local_count);
        write_u32(o, static_cast<std::uint32_t>(f.parameter_names.size()));
        for (const auto& name : f.parameter_names) write_string(o, name);
        write_u32(o, static_cast<std::uint32_t>(f.local_names.size()));
        for (const auto& name : f.local_names) write_string(o, name);
    }
    write_u32(o, static_cast<std::uint32_t>(c.code.size()));
    for (const auto& ins : c.code) {
        write_u8(o, static_cast<std::uint8_t>(ins.op));
        write_u32(o, std::bit_cast<std::uint32_t>(ins.operand));
        write_u32(o, ins.line);
    }
    // v6+ source map: deterministic source identity, exact range, and function context.
    write_u32(o, static_cast<std::uint32_t>(c.source_map.size()));
    for (const auto& src : c.source_map) {
        write_string(o, src.source_path); write_u32(o, src.line); write_u32(o, src.column);
        write_u32(o, src.end_line); write_u32(o, src.end_column); write_string(o, src.function_name);
    }
    // v7 provenance table. Sort identities so bytecode output is deterministic.
    std::vector<std::pair<std::string, std::string>> hashes(c.source_hashes.begin(), c.source_hashes.end());
    std::sort(hashes.begin(), hashes.end());
    write_u32(o, static_cast<std::uint32_t>(hashes.size()));
    for (const auto& [identity, hash] : hashes) { write_string(o, identity); write_string(o, hash); }
}
'''
text = replace_between(text, 'void write_bytecode(', 'Chunk read_bytecode(', writer, 'write_bytecode rewrite')

reader = r'''Chunk read_bytecode(std::istream& i) {
    char magic[sizeof(Magic)]{};
    i.read(magic, sizeof(magic));
    if (!i || !std::equal(std::begin(magic), std::end(magic), std::begin(Magic)))
        throw std::runtime_error("not an Emojineer bytecode file");
    const auto version = read_u16(i);
    if (version < 1 || version > CurrentVersion)
        throw std::runtime_error("unsupported Emojineer bytecode version " + std::to_string(version));

    Chunk c;
    const auto nc = read_u32(i);
    if (nc > MaxConstants) throw std::runtime_error("bytecode constant pool exceeds safety limit");
    for (std::uint32_t k = 0; k < nc; ++k) {
        switch (read_u8(i)) {
            case 1: c.constants.emplace_back(std::bit_cast<double>(read_u64(i))); break;
            case 2: { auto b = read_u8(i); if (b > 1) throw std::runtime_error("invalid boolean constant in bytecode"); c.constants.emplace_back(b != 0); break; }
            case 3: c.constants.emplace_back(read_string(i)); break;
            case 4: c.constants.emplace_back(static_cast<std::int64_t>(read_u64(i))); break;
            default: throw std::runtime_error("invalid constant tag in bytecode");
        }
    }

    if (version >= 2) {
        const auto nf = read_u32(i);
        if (nf > MaxFunctions) throw std::runtime_error("bytecode function table exceeds safety limit");
        for (std::uint32_t k = 0; k < nf; ++k) {
            FunctionInfo f;
            f.name = read_string(i); f.entry = read_u32(i); f.arity = read_u32(i); f.local_count = read_u32(i);
            if (f.arity > f.local_count) throw std::runtime_error("function arity exceeds local count");
            if (version >= 5) {
                const auto np = read_u32(i);
                if (np > f.arity) throw std::runtime_error("parameter-name metadata exceeds function arity");
                for (std::uint32_t p = 0; p < np; ++p) f.parameter_names.push_back(read_string(i));
                const auto nl = read_u32(i);
                if (nl > f.local_count) throw std::runtime_error("local-name metadata exceeds function local count");
                for (std::uint32_t l = 0; l < nl; ++l) f.local_names.push_back(read_string(i));
            }
            c.functions.push_back(std::move(f));
        }
    }

    const auto ni = read_u32(i);
    if (ni > MaxInstructions) throw std::runtime_error("bytecode instruction stream exceeds safety limit");
    for (std::uint32_t k = 0; k < ni; ++k) {
        const auto raw = read_u8(i);
        OpCode op;
        if (version == 1) op = decode_v1(raw);
        else {
            const auto max = version == 2 ? static_cast<std::uint8_t>(OpCode::Halt) : static_cast<std::uint8_t>(OpCode::SetIndex);
            if (raw > max) throw std::runtime_error("invalid opcode in bytecode");
            op = static_cast<OpCode>(raw);
        }
        c.code.push_back({op, std::bit_cast<std::int32_t>(read_u32(i)), read_u32(i)});
    }

    // v6 has exact ranges/context; v4-v5 degrade deterministically to point locations.
    if (version >= 6) {
        const auto ns = read_u32(i);
        if (ns > MaxInstructions || ns != ni) throw std::runtime_error("source map count does not match instruction stream");
        for (std::uint32_t k = 0; k < ns; ++k) {
            SourceLocation src;
            src.source_path = read_string(i); src.line = read_u32(i); src.column = read_u32(i);
            src.end_line = read_u32(i); src.end_column = read_u32(i); src.function_name = read_string(i);
            c.source_map.push_back(std::move(src));
        }
    } else if (version >= 4) {
        const auto ns = read_u32(i);
        if (ns > MaxInstructions || ns != ni) throw std::runtime_error("source map count does not match instruction stream");
        for (std::uint32_t k = 0; k < ns; ++k) {
            SourceLocation src;
            src.source_path = read_string(i); src.line = read_u32(i); src.column = read_u32(i);
            src.end_line = src.line; src.end_column = src.column;
            c.source_map.push_back(std::move(src));
        }
    }

    if (version >= 7) {
        const auto nh = read_u32(i);
        if (nh > MaxSourceFiles) throw std::runtime_error("source hash table exceeds safety limit");
        for (std::uint32_t k = 0; k < nh; ++k) {
            auto identity = read_string(i);
            auto hash = read_string(i);
            if (!c.source_hashes.emplace(std::move(identity), std::move(hash)).second)
                throw std::runtime_error("duplicate source hash identity in bytecode");
        }
    }

    verify_bytecode(c);
    return c;
}
'''
text = replace_between(text, 'Chunk read_bytecode(', 'std::string opcode_name(', reader, 'read_bytecode rewrite')
bc.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# Compilation provenance: capture exactly one hash for every deterministic source
# identity, including linked local/path/registry/stdlib modules and overlays.
# -----------------------------------------------------------------------------
module = Path('src/module.cpp')
text = module.read_text(encoding='utf-8')
text = replace_once(text, '#include "emojineer/compiler.hpp"\n', '#include "emojineer/compiler.hpp"\n#include "emojineer/hash.hpp"\n', 'module hash include')
text = replace_once(text, '    std::string identity;\n    std::string module_name;', '    std::string identity;\n    std::string source_hash;\n    std::string module_name;', 'module source hash member')

load_helper = r'''std::string load_source_text(const std::filesystem::path& path, SourceProvider source_provider) {
    if (source_provider) {
        if (auto overlay = source_provider(path)) return *overlay;
    }
    return read_text(path);
}

'''
if load_helper.strip() not in text:
    marker = 'ast::Program parse_text('
    pos = text.find(marker)
    if pos < 0: raise SystemExit('module load source insertion anchor missing')
    text = text[:pos] + load_helper + text[pos:]

# Keep parse_source, but make it use the canonical loader.
old_parse = r'''ast::Program parse_source(const std::filesystem::path& path,
                          const CustomEmojiRegistry& registry,
                          const std::string& identity,
                          SourceProvider source_provider = {}) {
    // First check if the source provider has the content
    if (source_provider) {
        auto overlay = source_provider(path);
        if (overlay) {
            return parse_text(*overlay, registry, identity, path);
        }
    }
    // Fall back to reading from disk
    return parse_text(read_text(path), registry, identity, path);
}
'''
new_parse = r'''ast::Program parse_source(const std::filesystem::path& path,
                          const CustomEmojiRegistry& registry,
                          const std::string& identity,
                          SourceProvider source_provider = {}) {
    return parse_text(load_source_text(path, source_provider), registry, identity, path);
}
'''
text = replace_once(text, old_parse, new_parse, 'module canonical source loader')

text = replace_once(
    text,
    '        ModuleUnit unit;\n        unit.identity = identity;\n        unit.program = parse_text(std::string(*source), registry_, identity);',
    '        ModuleUnit unit;\n        unit.identity = identity;\n        const std::string source_text(*source);\n        unit.source_hash = sha256_hex(source_text);\n        unit.program = parse_text(source_text, registry_, identity);',
    'stdlib source hash')
text = replace_once(
    text,
    '        unit.package_name = package_name;\n        unit.identity = identity;\n        unit.program = parse_source(canonical, registry_, identity, source_provider_);',
    '        unit.package_name = package_name;\n        unit.identity = identity;\n        const std::string source_text = load_source_text(canonical, source_provider_);\n        unit.source_hash = sha256_hex(source_text);\n        unit.program = parse_text(source_text, registry_, identity, canonical);',
    'linked source hash')
text = replace_once(
    text,
    '        Compiler compiler;\n        compiler.set_source_path(entry_id);\n        return compiler.compile(linked);',
    '        Compiler compiler;\n        compiler.set_source_path(entry_id);\n        Chunk chunk = compiler.compile(linked);\n        for (const auto& [identity, unit] : units_) chunk.source_hashes[identity] = unit.source_hash;\n        return chunk;',
    'linked chunk source hashes')

# Plain-file compilation needs the same exact source hash, including SourceProvider overlays.
old_plain = r'''    const std::string identity = identity_for(root, entry);
    ast::Program entry_program = parse_source(entry, registry, identity, source_provider);
    if (!has_module_syntax(entry_program)) {
        Compiler compiler;
        compiler.set_source_path(identity);
        return compiler.compile(entry_program);
    }
'''
new_plain = r'''    const std::string identity = identity_for(root, entry);
    const std::string entry_source = load_source_text(entry, source_provider);
    ast::Program entry_program = parse_text(entry_source, registry, identity, entry);
    if (!has_module_syntax(entry_program)) {
        Compiler compiler;
        compiler.set_source_path(identity);
        Chunk chunk = compiler.compile(entry_program);
        chunk.source_hashes[identity] = sha256_hex(entry_source);
        return chunk;
    }
'''
text = replace_once(text, old_plain, new_plain, 'plain source hash')
module.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# Source resolver + debugger diagnostics + real .emjbc launch.
# -----------------------------------------------------------------------------
dh = Path('include/emojineer/debugger.hpp')
text = dh.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    std::optional<std::string> get_source_text(const std::string& identity,\n                                                std::uint32_t start_line,\n                                                std::uint32_t end_line) const;\n',
    '    std::optional<std::string> get_source_text(const std::string& identity,\n                                                std::uint32_t start_line,\n                                                std::uint32_t end_line) const;\n'
    '    std::optional<std::string> get_source_hash(const std::string& identity) const;\n',
    'source resolver hash API')
text = replace_once(
    text,
    '    void set_debug_callback(DebugCallback callback);\n    \n    // Execute with debugger',
    '    void set_debug_callback(DebugCallback callback);\n    void set_source_resolver(std::shared_ptr<SourceResolver> resolver);\n    \n    // Execute with debugger',
    'DebugVM source resolver API')
dh.write_text(text, encoding='utf-8')

dc = Path('src/debugger.cpp')
text = dc.read_text(encoding='utf-8')
text = replace_once(text, '#include "emojineer/compiler.hpp"\n', '#include "emojineer/compiler.hpp"\n#include "emojineer/hash.hpp"\n#include "emojineer/package.hpp"\n#include "emojineer/project.hpp"\n#include "emojineer/stdlib.hpp"\n', 'debugger completion includes')

hash_impl = r'''std::optional<std::string> SourceResolver::get_source_hash(const std::string& identity) const {
    auto registered = registered_sources_.find(identity);
    if (registered != registered_sources_.end()) return sha256_hex(registered->second);
    const auto path = resolve(identity);
    if (path.empty()) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    const std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return sha256_hex(content);
}

'''
marker = 'void SourceResolver::register_source('
if hash_impl.strip() not in text:
    pos = text.find(marker)
    if pos < 0: raise SystemExit('source hash impl anchor missing')
    text = text[:pos] + hash_impl + text[pos:]

bp_info = r'''std::vector<BreakpointInfo> DebugController::get_breakpoint_info() const {
    std::vector<BreakpointInfo> infos;
    const Chunk* chunk = vm_.current_chunk();
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        const auto& bp = breakpoints_[i];
        BreakpointInfo info{i, bp, BreakpointStatus::Unbound, std::nullopt, {}};
        if (!chunk) {
            info.diagnostics = "No chunk loaded";
            infos.push_back(std::move(info));
            continue;
        }

        const bool identity_present = std::any_of(chunk->source_map.begin(), chunk->source_map.end(), [&](const SourceLocation& src) {
            return src.source_path == bp.source_position.source_path;
        });
        if (!identity_present) {
            info.diagnostics = "Source identity is absent from current bytecode";
            infos.push_back(std::move(info));
            continue;
        }

        if (source_resolver_) {
            const auto live_hash = source_resolver_->get_source_hash(bp.source_position.source_path);
            if (!live_hash) {
                info.status = BreakpointStatus::SourceDrift;
                info.diagnostics = "Compiled source identity is no longer resolvable";
                infos.push_back(std::move(info));
                continue;
            }
            if (auto compiled = chunk->source_hashes.find(bp.source_position.source_path);
                compiled != chunk->source_hashes.end() && !compiled->second.empty() && compiled->second != *live_hash) {
                info.status = BreakpointStatus::Stale;
                info.diagnostics = "Source content differs from the content used to compile this bytecode";
                infos.push_back(std::move(info));
                continue;
            }
        }

        for (std::size_t ip = 0; ip < chunk->source_map.size(); ++ip) {
            const auto& src = chunk->source_map[ip];
            if (src.source_path == bp.source_position.source_path && src.line == bp.source_position.line) {
                info.status = BreakpointStatus::Bound;
                info.bound_ip = ip;
                info.diagnostics = "Bound to IP " + std::to_string(ip);
                break;
            }
        }
        if (!info.bound_ip && info.diagnostics.empty())
            info.diagnostics = "Source exists in current bytecode but requested line has no executable location";
        infos.push_back(std::move(info));
    }
    return infos;
}

'''
text = replace_between(text, 'std::vector<BreakpointInfo> DebugController::get_breakpoint_info()', 'void DebugController::notify_debug_event(', bp_info, 'breakpoint status rewrite')

text = replace_once(
    text,
    'void DebugVM::set_debug_callback(DebugCallback callback) {\n    controller_->set_debug_callback(std::move(callback));\n}\n',
    'void DebugVM::set_debug_callback(DebugCallback callback) {\n    controller_->set_debug_callback(std::move(callback));\n}\n\n'
    'void DebugVM::set_source_resolver(std::shared_ptr<SourceResolver> resolver) {\n    controller_->set_source_resolver(std::move(resolver));\n}\n',
    'DebugVM source resolver implementation')

# Helpers for deterministic/offline source registration used by both source and bytecode sessions.
resolver_helpers = r'''std::filesystem::path debugger_source_root(const std::filesystem::path& target) {
    std::filesystem::path dir = target.parent_path();
    while (!dir.empty()) {
        if (std::filesystem::is_regular_file(dir / "emojineer.toml")) return dir;
        const auto parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return target.parent_path();
}

std::optional<std::string> read_debug_source(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::shared_ptr<SourceResolver> build_source_resolver(const std::filesystem::path& target, const Chunk& chunk) {
    auto resolver = std::make_shared<SourceResolver>();
    const auto root = debugger_source_root(target);
    resolver->set_base_path(root);
    resolver->add_search_path(root);

    std::optional<PackageGraph> graph;
    try {
        if (std::filesystem::is_regular_file(root / "emojineer.toml")) {
            const auto manifest = load_project_manifest(root / "emojineer.toml");
            graph = resolve_package_graph(root, manifest, package_store_root(root), true);
        }
    } catch (...) {
        // Source listing/drift diagnostics must never create network authority. If the
        // already-materialized graph is unavailable, ordinary root/local resolution remains.
    }

    std::set<std::string> identities;
    for (const auto& src : chunk.source_map) if (!src.source_path.empty()) identities.insert(src.source_path);
    for (const auto& identity : identities) {
        if (identity.rfind("std:", 0) == 0) {
            if (auto source = standard_module_source(identity)) resolver->register_source(identity, std::string(*source));
            continue;
        }
        if (identity.rfind("pkg:", 0) == 0 && graph) {
            const auto coordinate = identity.substr(4);
            const auto slash = coordinate.find('/');
            if (slash != std::string::npos) {
                const auto package_name = coordinate.substr(0, slash);
                const auto module_path = coordinate.substr(slash + 1);
                if (const auto* package = graph->find(package_name)) {
                    if (auto source = read_debug_source(package->root / module_path))
                        resolver->register_source(identity, std::move(*source));
                }
            }
            continue;
        }
        if (auto source = read_debug_source(root / identity)) resolver->register_source(identity, std::move(*source));
    }
    return resolver;
}

'''
marker = 'int run_debug_session('
if resolver_helpers.strip() not in text:
    pos = text.find(marker)
    if pos < 0: raise SystemExit('debug resolver helper anchor missing')
    text = text[:pos] + resolver_helpers + text[pos:]

# Real source-or-bytecode loader and source resolver wiring.
old_loader = r'''    // Compile the source file
    emojineer::Chunk chunk;
    try {
        chunk = emojineer::compile_file(source_file, registry);
    } catch (const std::exception& e) {
        error << "Debug session failed to compile: " << e.what() << "\n";
        return 1;
    }
    
    // Create DebugVM with the compiled chunk using pointer to allow recreation
    std::unique_ptr<emojineer::DebugVM> debug_vm = std::make_unique<emojineer::DebugVM>(input, output);
'''
new_loader = r'''    emojineer::Chunk chunk;
    try {
        if (source_file.extension() == ".emjbc") {
            std::ifstream bytecode(source_file, std::ios::binary);
            if (!bytecode) throw std::runtime_error("cannot open bytecode file");
            chunk = emojineer::read_bytecode(bytecode);
            if (chunk.source_map.empty())
                throw std::runtime_error("bytecode does not contain source debug metadata; recompile with Emojineer 0.18+");
        } else {
            chunk = emojineer::compile_file(source_file, registry);
        }
    } catch (const std::exception& e) {
        error << "Debug session failed to load: " << e.what() << "\n";
        return 1;
    }

    auto source_resolver = build_source_resolver(source_file, chunk);
    std::unique_ptr<emojineer::DebugVM> debug_vm = std::make_unique<emojineer::DebugVM>(input, output);
    debug_vm->set_source_resolver(source_resolver);
'''
text = replace_once(text, old_loader, new_loader, 'real bytecode debugger loader')
text = replace_once(
    text,
    '            debug_vm = std::make_unique<emojineer::DebugVM>(input, output);\n            debug_vm->set_debug_callback',
    '            debug_vm = std::make_unique<emojineer::DebugVM>(input, output);\n            debug_vm->set_source_resolver(source_resolver);\n            debug_vm->set_debug_callback',
    'restart source resolver')
dc.write_text(text, encoding='utf-8')

# -----------------------------------------------------------------------------
# Product acceptance: real Stale/SourceDrift, v7 provenance roundtrip/validation,
# and interactive debugging of serialized .emjbc rather than recompiling source.
# -----------------------------------------------------------------------------
tests = Path('tests/debugger_tests.cpp')
text = tests.read_text(encoding='utf-8')
if '#include "emojineer/hash.hpp"' not in text:
    text = text.replace('#include "emojineer/debugger.hpp"\n', '#include "emojineer/debugger.hpp"\n#include "emojineer/hash.hpp"\n', 1)

# Strengthen the existing round-trip test with a real source digest.
needle = '    auto original_chunk = compiler.compile(program);\n    \n    // Serialize to bytecode'
replacement = '    auto original_chunk = compiler.compile(program);\n    original_chunk.source_hashes["test.emoji"] = emojineer::sha256_hex(source);\n    \n    // Serialize to bytecode'
text = replace_once(text, needle, replacement, 'roundtrip source hash fixture')
needle = '    require(restored_chunk.source_map.size() == original_chunk.source_map.size(),\n        "source map size should survive roundtrip");'
replacement = '    require(restored_chunk.source_map.size() == original_chunk.source_map.size(),\n        "source map size should survive roundtrip");\n    require(restored_chunk.source_hashes == original_chunk.source_hashes,\n        "v7 source SHA-256 provenance must survive bytecode roundtrip");'
text = replace_once(text, needle, replacement, 'roundtrip source hash assertion')

completion_tests = r'''
void test_breakpoint_stale_and_source_drift() {
    std::cout << "Test: breakpoint stale and source-drift diagnostics...\n";
    const std::string source = "📝 📜hello📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    chunk.source_hashes["test.emoji"] = emojineer::sha256_hex(source);
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 1;
    vm.add_breakpoint(bp);
    vm.execute(chunk);

    auto current = std::make_shared<emojineer::SourceResolver>();
    current->register_source("test.emoji", source);
    vm.set_source_resolver(current);
    auto infos = vm.get_breakpoint_info();
    require(infos.size() == 1 && infos[0].status == emojineer::BreakpointStatus::Bound,
            "matching current source must report Bound");

    auto changed = std::make_shared<emojineer::SourceResolver>();
    changed->register_source("test.emoji", "📝 📜changed📜\n");
    vm.set_source_resolver(changed);
    infos = vm.get_breakpoint_info();
    require(infos[0].status == emojineer::BreakpointStatus::Stale,
            "changed source content must report Stale");

    auto missing = std::make_shared<emojineer::SourceResolver>();
    vm.set_source_resolver(missing);
    infos = vm.get_breakpoint_info();
    require(infos[0].status == emojineer::BreakpointStatus::SourceDrift,
            "compiled identity with unavailable source must report SourceDrift");
    std::cout << "  ✅ Breakpoint stale/source-drift diagnostics work\n";
}

void test_debug_metadata_validation() {
    std::cout << "Test: debug metadata validation...\n";
    const std::string source = "📝 📜hello📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    chunk.source_hashes["test.emoji"] = emojineer::sha256_hex(source);

    auto bad_count = chunk;
    bad_count.source_map.pop_back();
    bool rejected = false;
    try { emojineer::verify_bytecode(bad_count); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "source-map cardinality mismatch must be rejected");

    auto bad_path = chunk;
    bad_path.source_map[0].source_path = "/tmp/checkout/test.emoji";
    rejected = false;
    try { emojineer::verify_bytecode(bad_path); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "absolute checkout-specific source identity must be rejected");

    auto bad_range = chunk;
    bad_range.source_map[0].end_column = 0;
    rejected = false;
    try { emojineer::verify_bytecode(bad_range); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "zero/reversed source range metadata must be rejected");

    auto bad_names = chunk;
    emojineer::FunctionInfo f;
    f.name = "🚀"; f.entry = 0; f.arity = 1; f.local_count = 1; f.parameter_names = {"🍎", "🍐"};
    bad_names.functions.push_back(f);
    rejected = false;
    try { emojineer::verify_bytecode(bad_names); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "parameter-name count inconsistent with arity must be rejected");
    std::cout << "  ✅ Debug metadata validation works\n";
}

void test_emjbc_debug_session() {
    std::cout << "Test: serialized .emjbc debug session...\n";
    const std::string source = "📝 📜bytecode-debug📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("fixture.emoji");
    auto chunk = compiler.compile(program);
    chunk.source_hashes["fixture.emoji"] = emojineer::sha256_hex(source);

    const auto path = std::filesystem::temp_directory_path() / "emojineer-train18-debug.emjbc";
    {
        std::ofstream out(path, std::ios::binary);
        emojineer::write_bytecode(chunk, out);
    }
    std::istringstream commands("continue\n");
    std::ostringstream output;
    std::ostringstream error;
    const int result = emojineer::run_debug_session(path, commands, output, error, {});
    std::filesystem::remove(path);
    require(result == 0, "debugging serialized .emjbc must succeed");
    require(error.str().empty(), "serialized bytecode debug session must not report loader errors");
    require(output.str().find("bytecode-debug") != std::string::npos,
            "serialized bytecode must execute through the debugger rather than being recompiled as source");
    require(output.str().find("Program finished") != std::string::npos,
            "serialized bytecode debugger must run to completion");
    std::cout << "  ✅ Serialized .emjbc debug session works\n";
}

'''
marker = '} // anonymous namespace\n\nint main()'
if completion_tests.strip() not in text:
    pos = text.find(marker)
    if pos < 0: raise SystemExit('debugger completion test insertion anchor missing')
    text = text[:pos] + completion_tests + text[pos:]

main_anchor = '        test_step_over_honors_inner_breakpoint();\n'
if main_anchor not in text:
    main_anchor = '        test_step_over_skips_inner_breakpoint();\n'
text = replace_once(
    text,
    main_anchor,
    main_anchor + '        test_breakpoint_stale_and_source_drift();\n        test_debug_metadata_validation();\n        test_emjbc_debug_session();\n',
    'completion tests main calls')
tests.write_text(text, encoding='utf-8')

print('Train 18 v7 provenance, metadata hardening, drift diagnostics, and .emjbc debugger completion applied.')
