# Emojineer Product Roadmap

This roadmap tracks usable language/toolchain capabilities. Landed trains are implementation history; future trains should add working product behavior with tests/examples rather than placeholder surfaces.

## Landed

### Train 1 — Sovereign language core

C++20 + ICU, grapheme-aware UTF-8 lexing, parser/AST, Emojineer-owned EMJBC bytecode and VM, variables/types, arithmetic/comparison, control flow, input/output, run/check/explain/compile/exec, execution fuel, and bounded bytecode parsing.

### Train 2 — Functions, recursion, and call frames

User functions, parameters/returns, VM call frames, locals, recursion, forward references, arity checks, and EMJBC v2 with v1 reader compatibility.

### Train 3 — Custom Emoji Registry

JSON CER packs, multi-grapheme semantic tokens, longest-match lexing, aliases/descriptions, deterministic semantic IDs, core-semantic lowering, collision rejection, and CER-aware Explain Mode.

### Train 4 — First-class collections

Arrays/nested arrays, structural equality, indexing, grapheme-aware length, value-style append/replacement, typed arrays, bounds checking, and EMJBC v3 with v1/v2 reader compatibility.

### Train 5 — REPL and bytecode tooling

Buffered REPL, interactive commands, CER support, bytecode dump/disassembly, and the intentional shared REPL/VM input stream for `📥`.

### Train 6 — Canonical source formatting and diagnostics

`fmt`, `lint`, canonical indentation/LF form, comment preservation, multiline-string protection, CER-aware structural formatting, and CR/CRLF diagnostics.

### Train 7 — `emji` project workflow

`init`, `check`, `lock`, `show`, strict manifests, semantic-version-shaped package versions, validated entry paths, deterministic lock metadata, canonical-manifest drift identity, stale-lock detection, and starter source generation.

### Train 8 — Modules and imports

`🧩` modules, local/relative `🔗` imports, explicit `📤` exports, deterministic module identity, module-local globals/functions, export-only visibility, once-only dependency initialization, graph diagnostics, module-aware CLI/project checks, and no VM opcode/EMJBC bump.

### Train 9 — Native standard-library foundation

Deterministic `std:<module>` imports, `std:math`, `std:arrays`, `std:text`, standard modules authored in Emojineer source and compiled by the normal pipeline, `emojineer stdlib`, ordinary export/collision semantics, deterministic bytecode, and no ambient host capabilities.

### Train 10 — Real local dependency management

- strict `[dependencies]` path declarations in `emojineer.toml`;
- `emji add` and `emji remove`;
- recursive local/path package graph resolution;
- package-name/root uniqueness and cycle diagnostics;
- deterministic lockfile v2 with transitive dependency records;
- SHA-256 package content identity over canonical manifest plus package-owned `.emoji` source;
- dependency source trees excluded from ancestor package hashes, including transitive nested roots;
- stale-lock detection on dependency source/manifest/graph changes;
- dependency entry/source-graph validation;
- checkout-portable relative lock paths;
- no remote registry fiction.

## Next product train

### Train 11 — Cross-package module imports

Connect the now-real package graph to the source linker with explicit package coordinates rather than filesystem root escapes:

- sovereign import syntax for a module inside a declared dependency package;
- package graph supplied to module resolution by project-aware compilation;
- dependency package modules remain confined to their own package root;
- explicit exports continue to control cross-package visibility;
- deterministic package-qualified module identities in linked bytecode;
- duplicate/collision/cycle diagnostics across package and source-module graphs;
- file CLI and `emji check` exercise the same package-aware linker path;
- stdlib imports remain separate `std:` identities.

Do not encode package access as `../` source imports that bypass Train 8's package-root boundary.

## Later product trains

### Registry protocol and package distribution

Define a real remote registry API/protocol, version selection, package retrieval/cache, reproducible resolution, artifact verification, and only then `emji publish` and remote `emji add` behavior.

### Language server and editor integration

LSP diagnostics, completion, hover/explain, go-to-definition/references across modules and dependencies, formatter integration, syntax support, and accessible textual token views.

### Debugging

Source breakpoints, stepping, stack/call-frame inspection, local/global value inspection, and source-position mappings across module graphs.

### Capability model and native facilities

Explicit grants for filesystem/network/process/clocks/randomness/host resources, deterministic/sandboxed execution modes, and no ambient host authority by default.

### WASM / Host Interop Layer

A defined interop ABI, C ABI and/or WASM boundary, and capability-controlled adapters only after the ABI is stable.

### Low-level Emojineer / EASM

Typed buffers/memory model, low-level instruction representation, verifier/sandbox boundaries, and a defined high-level/low-level ABI.

### Semantic compression and metaprogramming research

Carefully specified macro/semantic-compression mechanisms inspired by earlier SCL ideas. Expansion should produce ordinary Emojineer semantics and remain inspectable by tooling.

### Native backend

Explicit IR boundary, LLVM/native compilation, equivalence testing against the VM, and preservation of language-visible behavior rather than a second dialect.

## Ongoing language evolution

Future language additions can include records/user structures, richer error values, pattern matching, interfaces/protocols, more collection types, richer standard modules, and accessibility-oriented textual aliases. Each should extend the sovereign language model rather than route source through another programming language.
