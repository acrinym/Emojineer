# Emojineer Documentation

Emojineer is a sovereign emoji-native programming language. Its source is parsed into Emojineer's AST, linked across project-local, dependency-package, or built-in standard modules, compiled to Emojineer's `EMJBC` bytecode, and executed by Emojineer's VM. It is not emoji syntax translated into Python, JavaScript, C++, or another host language.

## Start here

- [Language reference](LANGUAGE.md) - source model, tokens, grammar, values, functions, collections, control flow, and all module import forms.
- [Standard library](STDLIB.md) - built-in `std:math`, `std:arrays`, and `std:text` APIs and architecture.
- [CLI and toolchain](CLI.md) - build, run, check, format, lint, explain, REPL, compile, execute, dump, disassemble, standard-library listing, and `emji` project/dependency commands.
- [EMJBC bytecode](BYTECODE.md) - file format, compatibility versions, constants, function metadata, instructions, verifier limits, and VM execution model.
- [Modules and imports](MODULES.md) - project-local, `pkg:`, and `std:` imports, visibility, deterministic identity, package ownership, loading order, and cycles.
- [`emji` projects](PROJECTS.md) - manifest, local/path dependency graph, deterministic lockfile, SHA-256 package identity, package-qualified imports, initialization, and project validation.

## Focused references

- [Functions](FUNCTIONS.md)
- [Collections](COLLECTIONS.md)
- [Custom Emoji Registry](CER.md)
- [Formatter and lint](SOURCE_TOOLING.md)
- [REPL and bytecode tooling](TOOLING.md)
- [Design provenance](PROVENANCE.md)
- [Product roadmap](ROADMAP.md)

## Current implemented product level

The compiler executable reports Emojineer **0.11**. Product Trains 1 through 11 are implemented on this train:

1. sovereign language core;
2. functions, recursion, and call frames;
3. Custom Emoji Registry;
4. first-class collections;
5. REPL and bytecode tooling;
6. canonical source formatting and diagnostics;
7. `emji` project workflow;
8. modules and imports;
9. native standard-library foundation;
10. real local/path dependency management with recursive locking and package content identity;
11. explicit cross-package module imports through the resolved package graph.

The documentation describes implemented behavior unless a section is explicitly labeled as future work.

## File, module, and package forms

- `.emoji` - Emojineer UTF-8 source.
- `.emjbc` - serialized Emojineer bytecode.
- `emojineer.toml` - strict project/package manifest, including optional local `[dependencies]`.
- `emojineer.lock` - deterministic project/package graph lock metadata.
- CER `.json` files - optional custom emoji token packs that lower into existing Emojineer semantic token kinds.
- `relative/path.emoji` - source module inside the importing package's owned root.
- `pkg:<dependency>/<path>.emoji` - source module inside a declared direct dependency package.
- `std:<name>` - deterministic built-in standard module specifier resolved by the module linker.

Local dependency packages are resolvable, lockable, and explicitly importable in v0.11. Package-qualified imports do not make transitive dependencies ambient, and ordinary relative `🔗` imports cannot cross package ownership boundaries.
