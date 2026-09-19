# Emojineer Language Reference - v0.23

Emojineer is a ground-up emoji-native programming language. The implementation is currently written in C++20, but Emojineer source is **not** translated into C++, Python, JavaScript, or another language. The toolchain owns its lexer, AST, package-aware module linker, bytecode, VM, low-level EASM representation, standard library, package workflow, and semantic evolution.

```text
UTF-8 .emoji
  -> grapheme-aware lexer
  -> parser
  -> Emojineer AST
  -> package-aware module linker when needed
  -> Emojineer compiler
  -> EMJBC bytecode
  -> Emojineer VM
```

This page describes the implemented product through Product Train 22. Package, registry, LSP, debugger, capability, host/WASM interop, and low-level EASM details are split into focused references where appropriate.

## 1. Source files and Unicode

Source files use UTF-8 and the `.emoji` extension.

Emojineer tokenization operates on Unicode extended grapheme clusters through ICU rather than assuming one Unicode code point equals one visible emoji.

Canonical token identity:

- NFC normalization is applied;
- U+FE0E and U+FE0F presentation selectors are ignored for token identity;
- meaningful ZWJ sequences are preserved;
- emoji modifiers such as skin tones are preserved;
- canonical-equivalent registered tokens cannot silently coexist.

This lets visually equivalent presentation forms such as `✖` and `✖️` resolve to the same core token while still allowing meaningful multi-code-point emoji graphemes to remain distinct identifiers.

Canonical source style uses LF line endings. The formatter normalizes CR/CRLF to LF; the linter reports noncanonical CR/CRLF input.

## 2. Lexical model

Outside strings and numeric literals, program syntax is emoji-native.

- ASCII spaces and tabs separate tokens.
- Newlines terminate simple statements.
- `💭` starts a line comment outside strings.
- `📜...📜` delimits UTF-8 text and may span lines.
- ASCII digits form numeric literals; one decimal point is permitted.
- Any non-reserved extended emoji grapheme can be an identifier.

Examples of valid identifiers include `🍎`, `👤`, `🌍`, and a single ZWJ emoji grapheme.

## 3. Core token map

| Glyph | Core meaning |
| --- | --- |
| `🐍` | variable declaration |
| `✏️` | assignment |
| `📝` | print |
| `🤔` | if |
| `🙅` | else |
| `🔁` | while |
| `🏁` | end block |
| `📥` | read one input line |
| `✅` / `❌` | boolean true / false |
| `🔢` | number declaration type |
| `🔤` | text declaration type |
| `🎯` | boolean declaration type |
| `📚` | array declaration type or array literal |
| `🛠️` | function declaration |
| `📦` | return |
| `🧩` | module declaration |
| `🔗` | module import |
| `📤` | module export |
| `🔌` | typed host/WASM adapter import |
| `📡` | typed host/WASM function export |
| `➕` | addition or text concatenation |
| `➖` | subtraction or unary negation |
| `✖️` | multiplication |
| `➗` | division |
| `🪄` | remainder/modulo |
| `🟰` | declaration/assignment separator or equality comparison |
| `🔽` / `🔼` | less-than / greater-than |
| `🚫` | boolean NOT |
| `🫴` / `🤲` | grouped expression, parameter list, or argument list |
| `🔎` | array index |
| `📏` | array/text length |
| `📎` | value-style array append |
| `🧷` | value-style array element replacement |
| `📜...📜` | text literal fence |
| `💭` | line comment |

The [Custom Emoji Registry](CER.md) can register additional emoji sequences that lower into these existing semantic token kinds.

Train 20 also reserves six otherwise identifier-shaped emoji for native host facilities: `🗂️` filesystem read, `🌐` HTTPS GET, `⚙️` process execution, `🕰️` clock, `🎲` random integer, and `🖥️` host environment lookup. They use ordinary `IDENTIFIER 🫴 ... 🤲` call grammar but cannot be redefined as user functions. Compilation records their required capabilities and VM execution is denied by default unless the host explicitly grants them. See [Capability Model and Native Facilities](CAPABILITIES.md).

## 4. Current grammar

The grammar below is descriptive EBNF for the implemented parser. `LINE_END` means newline or EOF where the parser accepts it.

```text
program       := (NEWLINE | statement)* EOF

statement     := module_decl
               | import_stmt
               | export_stmt
               | interop_import_decl
               | interop_export_decl
               | var_decl
               | assignment
               | print_stmt
               | return_stmt
               | function_decl
               | if_stmt
               | while_stmt

module_decl   := 🧩 IDENTIFIER LINE_END
import_stmt   := 🔗 STRING LINE_END
export_stmt   := 📤 IDENTIFIER LINE_END
interop_import_decl := 🔌 IDENTIFIER STRING STRING type 🫴 type* 🤲 LINE_END
interop_export_decl := 📡 IDENTIFIER STRING type 🫴 type* 🤲 LINE_END

var_decl      := 🐍 IDENTIFIER type? 🟰 expression LINE_END
assignment    := ✏️ IDENTIFIER 🟰 expression LINE_END
print_stmt    := 📝 expression LINE_END
return_stmt   := 📦 expression LINE_END

type          := 🔢 | 🔤 | 🎯 | 📚

function_decl := 🛠️ IDENTIFIER 🫴 IDENTIFIER* 🤲 LINE_END
                 block
                 🏁 LINE_END

if_stmt       := 🤔 expression LINE_END
                 block
                 (🙅 LINE_END block)?
                 🏁 LINE_END

while_stmt    := 🔁 expression LINE_END
                 block
                 🏁 LINE_END

block         := statement*

expression    := equality
equality      := comparison (🟰 comparison)*
comparison    := term ((🔽 | 🔼) term)*
term          := factor ((➕ | ➖) factor)*
factor        := unary ((✖️ | ➗ | 🪄) unary)*
unary         := (➖ | 🚫 | 📏) unary | primary

primary       := NUMBER
               | STRING
               | ✅
               | ❌
               | 📥
               | IDENTIFIER
               | IDENTIFIER 🫴 expression* 🤲
               | 📚 🫴 expression* 🤲
               | 🔎 🫴 expression expression 🤲
               | 📎 🫴 expression expression 🤲
               | 🧷 🫴 expression expression expression 🤲
               | 🫴 expression 🤲
```

Argument and array-element lists currently use token/expression boundaries rather than comma punctuation.

The parser treats every `🔗` operand as a text specifier. Local, package, and standard import semantics are selected by the linker, not by separate parser syntax.

## 5. Values and types

Source programs currently work with:

- numbers;
- booleans;
- UTF-8 text;
- arrays containing arbitrary Emojineer values.

Declarations can optionally assert a runtime type:

```emoji
🐍 🍎 🔢 🟰 10
🐍 👤 🔤 🟰 📜Ada📜
🐍 🚦 🎯 🟰 ✅
🐍 🧺 📚 🟰 📚 🫴 1 2 3 🤲
```

An untyped declaration accepts the value produced by its initializer.

Typed declarations emit runtime type assertions. Later assignments preserve the declaration's known type when that declaration is visible to the compiler.

Conditions are strict booleans; arbitrary values are not implicitly truthy or falsy.

## 6. Variables and assignment

Declare a variable:

```emoji
🐍 🍎 🔢 🟰 3
```

Assign a new value:

```emoji
✏️ 🍎 🟰 🍎 ➕ 1
```

At top level, variables are VM globals. Inside functions, declared variables and parameters use function-local slots.

Function-local slot discovery covers declarations nested inside that function's `🤔` and `🔁` blocks. The current implementation therefore treats those declared names as function locals rather than introducing a separate lexical block-scope object.

Functions can read globals.

## 7. Operators

Precedence, highest to lowest:

1. primary/grouping and collection forms;
2. unary `➖`, `🚫`, `📏`;
3. `✖️`, `➗`, `🪄`;
4. `➕`, `➖`;
5. `🔽`, `🔼`;
6. `🟰` equality.

`➕` accepts either two numbers or two text values. Text plus number is not implicitly converted.

`➗` and `🪄` reject zero divisors.

## 8. Input and output

`📥` reads one line from the VM input stream and produces text:

```emoji
🐍 👤 🔤 🟰 📥
📝 👤
```

`📝` evaluates one expression and writes its rendered value followed by a newline.

The REPL deliberately shares its input stream with the VM. A line following `:run` can therefore be consumed by `📥`.

## 9. Control flow

### If / else

```emoji
🤔 🍎 🔼 0
    📝 📜positive📜
🙅
    📝 📜not positive📜
🏁
```

The condition must evaluate to `✅` or `❌`.

### While

```emoji
🔁 🍎 🔼 0
    📝 🍎
    ✏️ 🍎 🟰 🍎 ➖ 1
🏁
```

The VM has an execution-fuel budget so a runaway loop eventually fails instead of consuming the host indefinitely.

## 10. Functions

Define a function:

```emoji
🛠️ 🚀 🫴 🍎 🍐 🤲
    📦 🍎 ➕ 🍐
🏁
```

Call it:

```emoji
📝 🚀 🫴 2 3 🤲
```

Implemented behavior includes parameters, local slots, return values, recursion, forward references to functions in the same linked program, arity checking, global reads from functions, and bounded VM call depth.

Nested function declarations are not supported. If execution reaches the end of a function without `📦`, the compiler emits an implicit `❌` return value.

See [FUNCTIONS.md](FUNCTIONS.md).

### 10.1 Host/WASM interop declarations

Train 21 adds explicit typed interop declarations without introducing a second interpreter. `🔌` declares an adapter-backed callable and `📡` exposes an Emojineer function to a host or WASM embedding.

```emoji
🔌 🧮 📜fixture.double📜 📜none📜 🔢 🫴 🔢 🤲
📡 🚀 📜fixture.echo📜 🔢 🫴 🔢 🤲
```

For `🔌`, the first string is the external adapter name and the second is a capability specification such as `none`, `network`, `filesystem,network`, or `all`. The type before `🫴` is the result type; types inside `🫴 ... 🤲` are positional parameters. `📡` uses the same result/parameter signature but names an Emojineer function instead of an adapter. Supported crossing types are the current number, text, boolean, and array value families.

Interop calls lower to verifier-visible `InteropCall` instructions. Their declared capability requirements contribute to the same whole-program mask as Train 20 native facilities. The production VM preflights required grants and exact adapter bindings before instruction zero. Values cross the host/WASM boundary using the bounded deterministic `EMJABI1` binary envelope. See [INTEROP.md](INTEROP.md).

## 11. Arrays and collections

Create an array:

```emoji
🐍 🧺 📚 🟰 📚 🫴 2 4 6 🤲
```

Index, length, append, and value-style element replacement use `🔎`, `📏`, `📎`, and `🧷` respectively. Arrays can be nested. Equality is structural and recursive. Indexes must be whole, nonnegative, in-range numbers.

`📏` also measures text by Unicode extended grapheme clusters rather than bytes or Unicode scalar count.

See [COLLECTIONS.md](COLLECTIONS.md).

## 12. Modules and imports

A multi-file source unit begins with a module declaration:

```emoji
🧩 🚀
```

Import another source file in the current package:

```emoji
🔗 📜lib/math.emoji📜
```

Import a source module from a declared direct dependency package:

```emoji
🔗 📜pkg:mathkit/src/main.emoji📜
```

Import a built-in standard module:

```emoji
🔗 📜std:math📜
```

Expose a module-owned function or declared global:

```emoji
📤 🧠
📤 🌟
```

Current module rules:

- `🧩` must be the first statement in a multi-file source unit;
- one module declaration is allowed per source unit;
- imports appear before executable/declaration statements;
- imported modules expose only explicit `📤` symbols;
- imports do not automatically re-export their imports;
- dependency initialization is depth-first, dependency-first, and once per linked module identity;
- source-module cycles are rejected with a `cyclic module import` chain;
- module names declared with `🧩` must be unique across the complete loaded graph;
- all linked names are rewritten to deterministic internal identities before ordinary bytecode compilation.

### 12.1 Project-local imports

Ordinary relative `🔗` imports target `.emoji` files owned by the current package. Paths must be relative and use portable forward slashes. Canonical resolution cannot escape the package root or cross into another resolved package root nested beneath it.

Root-package module identities remain package-relative, for example:

```text
lib/math.emoji
```

### 12.2 Package-qualified imports

Package coordinates use:

```text
pkg:<dependency>/<module-path>.emoji
```

The importing package must declare `<dependency>` directly in its own `[dependencies]` manifest section. Transitive dependencies do not become ambient source imports.

The package graph resolves the dependency name to a canonical package root. The module path is then resolved inside that package root. Canonical/symlink escapes are rejected.

If another resolved package is physically nested beneath the named dependency root, a `pkg:` coordinate through the outer dependency cannot tunnel into that nested package. The nested package must be imported through its own coordinate and must itself be declared directly by the importer.

Dependency module identities are deterministic and checkout-portable:

```text
pkg:mathkit/src/main.emoji
```

### 12.3 Standard imports

`std:<name>` resolves source from the built-in standard-module catalog rather than the filesystem or package graph.

Standard identities are the specifiers themselves:

```text
std:math
std:arrays
std:text
```

Standard modules may import only other `std:` modules and do not gain ambient project/package access.

### 12.4 Package cycles versus source cycles

Package dependency cycles are rejected by the package resolver as `cyclic package dependency` before package-aware linking begins.

Source-module cycles are a distinct linker error, `cyclic module import`. Source identities inside dependency packages appear in those diagnostics with their `pkg:<package>/...` coordinates.

Modules, package-qualified imports, and the standard-library foundation do not add VM opcodes and do not require a new EMJBC version.

See [MODULES.md](MODULES.md), [PROJECTS.md](PROJECTS.md), and [STDLIB.md](STDLIB.md).

## 13. Native standard library

The built-in catalog contains three standard modules:

- `std:math` - `🧭` absolute value, `🤏` min, `👐` max, `🎚️` clamp;
- `std:arrays` - `🧲` membership, `🧮` numeric sum, `🔃` reverse;
- `std:text` - `🈳` empty test, `🪢` concatenate, `🔂` repeat.

These modules are authored as Emojineer source strings and pass through the same lexer, parser, linker, compiler, EMJBC writer, and VM as user code. They are not C++/Python/JavaScript callbacks disguised as library functions.

The stdlib intentionally has no ambient filesystem, network, process, clock, randomness, shell, or FFI authority.

See [STDLIB.md](STDLIB.md).

## 14. Projects and local package dependencies

`emojineer.toml` defines a strict root package with name, version, entry source, and optional local/path dependencies.

```toml
[package]
name = "signal_lab"
version = "0.1.0"
entry = "src/main.emoji"

[dependencies]
mathkit = "../mathkit"
```

The package resolver recursively validates dependency manifests, dependency-key/target-name agreement, package-name/root uniqueness, and package cycles.

`emojineer.lock` format 3 records deterministic path and registry dependency provenance, including selected remote versions and immutable identities. Package content identity is SHA-256 over the canonical manifest plus package-owned `.emoji` source. Source owned by resolved nested packages is excluded from ancestor hashes, including independently declared nested-package layouts.

`emji add` and `emji remove` modify direct dependencies after validating the candidate package graph. Registry requirements can be added explicitly, `emji sync` materializes verified remote packages, `emji lock` writes deterministic lock metadata, and `emji check` validates manifests, package graphs, package-aware source graphs, dependency entries, and lock drift. Registry/package-manager authority remains separate from program runtime authority.

See [PROJECTS.md](PROJECTS.md).

## 15. Comments and formatting

A comment begins with `💭` and runs to the newline outside a string.

The formatter uses four spaces per block depth for `🤔`, `🙅`, `🔁`, and `🛠️` structures. It preserves comments and protects multiline-string whitespace.

Canonical files use LF line endings and should end with a newline.

See [SOURCE_TOOLING.md](SOURCE_TOOLING.md).

## 16. Custom Emoji Registry

CER packs can add custom emoji sequences that map to an existing semantic token kind. Longest-match lexing lets multi-grapheme sequences act as one token.

CER provides aliases, descriptions, deterministic semantic IDs, and canonical-collision rejection. It does not inject host-language callbacks or bypass the Emojineer compiler/VM.

See [CER.md](CER.md).

## 17. Errors and runtime boundaries

Current compile/runtime diagnostics cover malformed source, undefined symbols, arity mismatch, invalid module graphs, invalid package graphs, package-boundary violations, unknown standard modules, bytecode corruption, division/modulo by zero, invalid collection operations, input failure, stack/call-frame errors, fuel exhaustion, and type assertions.

The language does not expose implicit filesystem, network, process, shell, host interop, low-level host calls, or remote package-registry capabilities. Native facilities, Train 21 adapter calls, and Train 22 EASM imports are explicit and capability-preflighted; package-registry authority remains a separate `emji` tooling plane.

## 18. Bytecode and VM

The current compiler writes `EMJBC` version 10. The reader supports versions 1 through 10. Bytecode is verified before execution and bounded against oversized constants, strings, function tables, instruction streams, source metadata, capability metadata, Train 21 interop tables, v10 intrinsic operands, and version-gated native facility operands.

Modules, stdlib source, and package-qualified imports are linked before bytecode generation, so Product Trains 8, 9, and 11 do not require an EMJBC format bump. Train 22 `EASM1` is a separately verified low-level representation that composes through Train 21 `EMJABI1`; Train 22 itself did not alter v9. The v0.23 practical-runtime foundation advances ordinary Emojineer to v10 for verifier-visible intrinsics and appended host-facility operands.

See [BYTECODE.md](BYTECODE.md) for the serialized format and VM contract.

## 19. Toolchain

The `emojineer` executable provides run/check/explain/fmt/lint/repl/compile/exec/dump/disasm/capabilities/interop, the Train 22 `easm-check`/`easm-dump`/`easm-info`/`easm-run` commands, plus `stdlib` for listing built-in standard modules. File-based `.emoji` compilation commands are package aware when their discovered module root contains `emojineer.toml`.

`emji` provides project validation, local/registry dependency management, locking/materialization, immutable artifact operations, registry publication/fetch/discovery, and package graph inspection.

See [CLI.md](CLI.md) and [PROJECTS.md](PROJECTS.md).

## 20. Low-level Emojineer / EASM

Train 22 adds textual `EASM1` as a separately verified low-level representation with typed `i64`/`f64`/`bool` registers, bounded `u8`/`i64`/`f64` buffers, explicit branches/imports/exports, checked arithmetic and memory access, and instruction fuel. EASM imports use the existing Train 21 `InteropRegistry` and `EMJABI1` codec; EASM exports can be exposed back to high-level Emojineer only as ordinary Train 21 adapter bindings with derived capability/determinism contracts.

See [EASM.md](EASM.md).

## 21. Practical v0.23 runtime values

The 1.0 productization foundation adds named record values, explicit success/error result values, bounded bytes with UTF-8/hex/Base64 codecs, explicit program arguments, and additional capability-gated filesystem/network facilities. Records and bytes reuse ordinary `🔎`, `🧷`, `📏`, and (for bytes) `📎` collection behavior rather than creating a second expression model.

See [PRACTICAL_RUNTIME.md](PRACTICAL_RUNTIME.md).

## 22. Deliberate future work

Not yet implemented as of v0.23:

- interfaces/protocols, pattern matching, and additional collection abstractions;
- semantic-compression/macros with inspectable expansion into ordinary Emojineer semantics;
- a native/LLVM backend with equivalence testing against the production VM.

These are product extensions to the sovereign compiler/runtime rather than replacements for it.
