# Emojineer Product Roadmap

This roadmap tracks usable language/toolchain capabilities. Landed trains are implementation history; future trains should add working product behavior with tests/examples rather than placeholder surfaces.

## Landed

### Train 1 — Sovereign language core

- C++20 + ICU implementation;
- grapheme-aware UTF-8 lexer and canonical token identity;
- parser and AST;
- Emojineer-owned EMJBC bytecode and stack VM;
- variables, optional declaration types, arithmetic, comparisons, booleans;
- `🤔` / `🙅` control flow and `🔁` loops;
- `📥` input and `📝` output;
- run/check/explain/compile/exec;
- execution fuel and bounded bytecode parsing.

### Train 2 — Functions, recursion, and call frames

- `🛠️` functions;
- parameters and `📦` return;
- function-local slots and VM call frames;
- recursion and forward function references;
- arity checking;
- global reads from functions;
- EMJBC v2 with v1 reader compatibility.

### Train 3 — Custom Emoji Registry

- JSON CER packs;
- multi-grapheme semantic tokens and longest-match lexing;
- aliases and descriptions;
- deterministic semantic token IDs;
- `maps_to` lowering into sovereign core semantics;
- canonical-equivalent collision rejection;
- CER-aware Explain Mode.

### Train 4 — First-class collections

- `📚` arrays and nested arrays;
- structural equality;
- `🔎` indexing;
- `📏` collection/text length;
- Unicode-grapheme-aware text length;
- `📎` value-style append;
- `🧷` value-style replacement;
- typed arrays and bounds checking;
- EMJBC v3 with v1/v2 reader compatibility.

### Train 5 — REPL and bytecode tooling

- buffered REPL;
- `:run`, `:show`, `:explain`, `:clear`, `:help`, `:quit`;
- CER-aware interactive source;
- dump/disassembly of constants, functions, and instructions;
- intentional shared REPL/VM input stream for `📥`.

### Train 6 — Canonical source formatting and diagnostics

- `emojineer fmt`;
- `emojineer lint`;
- canonical indentation and LF source form;
- comment-preserving formatting;
- multiline-string whitespace protection;
- CER-aware structural formatting;
- CR/CRLF diagnostics.

### Train 7 — `emji` project workflow

- `emji init`, `check`, `lock`, `show`;
- strict `emojineer.toml`;
- semantic-version-shaped package versions;
- validated relative `.emoji` entry paths;
- deterministic timestamp-free lock metadata;
- FNV-1a-64 canonical-manifest drift identity;
- stale-lock detection;
- generated starter source.

### Train 8 — Modules and imports

- `🧩` source modules;
- local/relative `🔗` imports;
- explicit `📤` exports;
- deterministic project-root-relative module identity;
- module-local globals/functions;
- export-only visibility and no implicit re-export;
- dependency-first once-only initialization;
- duplicate/cyclic/missing/root-escape diagnostics;
- module-aware file CLI and `emji check`;
- no VM opcode or EMJBC version bump required.

## Next product trains

### Train 9 — Native standard-library foundation

Build useful facilities owned by Emojineer rather than a fake host-language facade.

Initial target families:

- text/string utilities beyond core grapheme length;
- collection utilities beyond core index/length/append/replace;
- practical math utilities;
- a stable standard-library module/intrinsic boundary that keeps semantics owned by Emojineer;
- capability-gated filesystem/I/O only where a real safety model is defined.

Prefer a small working library with tests and examples over a huge list of unimplemented names.

### Train 10 — Real local dependency resolution

Extend `emji` with an actual dependency model:

- dependency declarations in `emojineer.toml`;
- `emji add` and `emji remove`;
- local/path dependencies first;
- deterministic graph resolution;
- lockfile dependency graph;
- package content hashes appropriate for artifact integrity;
- integration between dependency packages and source module resolution.

Do not invent remote package publication before a registry protocol exists.

## Later product trains

### Registry protocol and package distribution

- defined remote registry API/protocol;
- `emji publish`;
- package retrieval and cache;
- cryptographic package/artifact checksums;
- reproducible registry resolution and version selection.

### Language server and editor integration

- LSP diagnostics;
- completion and hover/explain;
- go-to-definition and references across modules/dependencies;
- formatter integration;
- editor syntax support and accessible textual token views.

### Debugging

- source breakpoints;
- stepping;
- stack/call-frame inspection;
- local/global value inspection;
- bytecode/source position mapping suitable for module graphs.

### Capability model and native facilities

- explicit capability grants for filesystem, network, process, clocks, randomness, and host resources;
- deterministic/sandboxed execution modes;
- no ambient host authority by default.

### WASM / Host Interop Layer

- defined interop ABI rather than semantic dependence on a host language;
- C ABI and/or WASM boundary;
- capability-controlled adapters for other ecosystems only after the ABI is stable.

### Low-level Emojineer / EASM

- typed buffers and memory model;
- low-level instruction representation;
- verifier and sandbox boundaries;
- defined ABI for high-level/low-level crossing.

### Semantic compression and metaprogramming research

- carefully specified macro/semantic-compression mechanisms inspired by earlier SCL ideas;
- expansion should produce normal Emojineer semantics and remain inspectable by tooling.

### Native backend

- explicit IR boundary from Emojineer semantics;
- LLVM/native compilation path;
- equivalence testing between VM and native backends;
- native code must preserve language-visible behavior rather than becoming a second dialect.

## Ongoing language evolution

Future language-level additions can include records/user structures, richer error values, pattern matching, interfaces/protocols, additional collection types, and accessibility-oriented textual aliases. Each should extend the sovereign language model rather than route source through another programming language.
