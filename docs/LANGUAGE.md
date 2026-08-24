# Emojineer Language Reference - v0.11

Emojineer is a ground-up emoji-native programming language. The implementation is currently written in C++20, but Emojineer source is **not** translated into C++, Python, JavaScript, or another language. The toolchain owns its lexer, AST, package-aware module linker, bytecode, VM, standard library, package workflow, and semantic evolution.

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

This page describes implemented behavior through Product Train 11.

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

## 4. Current grammar

The grammar below is descriptive EBNF for the implemented parser. `LINE_END` means newline or EOF where the parser accepts it.

```text
program       := (NEWLINE | statement)* EOF

statement     := module_decl
               | import_stmt
               | export_stmt
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

`emojineer.lock` format 2 records deterministic transitive package metadata. Package content identity is SHA-256 over the canonical manifest plus package-owned `.emoji` source. Source owned by resolved nested packages is excluded from ancestor hashes, including independently declared nested-package layouts.

`emji add` and `emji remove` modify direct local dependencies after validating the candidate package graph. `emji lock` writes deterministic lock metadata. `emji check` validates manifests, package graphs, package-aware source graphs, dependency entries, and lock drift.

No remote registry, publication protocol, download cache, or remote version solver exists in v0.11.

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

The language does not currently expose implicit filesystem, network, process, shell, host FFI, or remote package-registry capabilities.

## 18. Bytecode and VM

The current compiler writes `EMJBC` version 3. The reader supports v1, v2, and v3. Bytecode is verified before execution and bounded against oversized constants, strings, function tables, and instruction streams.

Modules, stdlib source, and package-qualified imports are linked before bytecode generation, so Product Trains 8, 9, and 11 do not require an EMJBC format bump.

See [BYTECODE.md](BYTECODE.md) for the serialized format and VM contract.

## 19. Toolchain

The `emojineer` executable provides run/check/explain/fmt/lint/repl/compile/exec/dump/disasm plus `stdlib` for listing built-in standard modules. File-based compile/check/run/dump commands are package aware when their discovered module root contains `emojineer.toml`.

`emji` provides init/check/lock/show/add/remove for local packages and dependencies.

See [CLI.md](CLI.md) and [PROJECTS.md](PROJECTS.md).

## 20. Deliberate future work

Not yet implemented as of v0.11:

- real remote registry/publication/cache/version-resolution protocol;
- richer package graph inspection such as `emji tree`;
- records/user-defined structures and interfaces;
- richer error values and pattern matching;
- language server and editor integration;
- source debugger;
- capability-gated filesystem/network/process standard-library APIs;
- C/WASM/host interop boundary;
- low-level EASM/ABI work;
- semantic-compression/macros with inspectable expansion;
- native/LLVM backend.

These are product extensions to the sovereign compiler/runtime rather than replacements for it.
