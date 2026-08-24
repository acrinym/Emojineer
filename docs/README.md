# Emojineer Documentation

Emojineer is a sovereign emoji-native programming language. Its source is parsed into Emojineer's AST, linked across project-local, dependency-package, or built-in standard modules, compiled to Emojineer's `EMJBC` bytecode, and executed by Emojineer's VM. It is not emoji syntax translated into Python, JavaScript, C++, or another host language.

## Start here

- [Language reference](LANGUAGE.md) - source model, tokens, grammar, values, functions, collections, control flow, and all module import forms.
- [Standard library](STDLIB.md) - built-in `std:math`, `std:arrays`, and `std:text` APIs and architecture.
- [CLI and toolchain](CLI.md) - build, run, check, format, lint, explain, REPL, compile, execute, package graph, package artifact, and registry commands.
- [EMJBC bytecode](BYTECODE.md) - file format, compatibility versions, constants, function metadata, instructions, verifier limits, and VM execution model.
- [Modules and imports](MODULES.md) - project-local, `pkg:`, and `std:` imports, visibility, deterministic identity, package ownership, loading order, and cycles.
- [`emji` projects](PROJECTS.md) - manifest, local/path dependency graph, deterministic lockfile, SHA-256 package identity, package-qualified imports, graph inspection, initialization, and project validation.
- [Package artifacts and registry transport](REGISTRY.md) - immutable `.emjpkg` format, registry identity/index protocol, publishing/fetching, content-addressed caching, HTTPS reads, and trust boundaries.

## Focused references

- [Functions](FUNCTIONS.md)
- [Collections](COLLECTIONS.md)
- [Custom Emoji Registry](CER.md)
- [Formatter and lint](SOURCE_TOOLING.md)
- [REPL and bytecode tooling](TOOLING.md)
- [Design provenance](PROVENANCE.md)
- [Product roadmap](ROADMAP.md)

## Current implemented product level

The compiler executable reports Emojineer **0.14**. Product Trains 1 through 14 are implemented on this train:

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
11. explicit cross-package module imports through the resolved package graph;
12. deterministic package graph inspection for humans and tooling;
13. deterministic immutable `.emjpkg` package artifacts, strict verification, content-addressed artifact identity, and full SemVer requirement/selection primitives;
14. registry identity/discovery, immutable local publication, package-version indexes, verified fetch/cache behavior, and optional HTTPS registry reads through libcurl.

The documentation describes implemented behavior unless a section is explicitly labeled as future work.

## File, module, package, and registry forms

- `.emoji` - Emojineer UTF-8 source.
- `.emjbc` - serialized Emojineer bytecode.
- `.emjpkg` - deterministic immutable package source artifact.
- `emojineer.toml` - strict project/package manifest, currently including local/path `[dependencies]` only.
- `emojineer.lock` - deterministic local project/package graph lock metadata.
- CER `.json` files - optional custom emoji token packs that lower into existing Emojineer semantic token kinds.
- `relative/path.emoji` - source module inside the importing package's owned root.
- `pkg:<dependency>/<path>.emoji` - source module inside a declared direct dependency package.
- `std:<name>` - deterministic built-in standard module specifier resolved by the module linker.
- `emojineer.package-graph.v1` - deterministic JSON graph-inspection schema emitted by `emji tree --json`.
- `EMJREGISTRY1` - registry discovery/identity descriptor.
- `EMJREGPKG1` - immutable package-version index binding versions to package content and exact artifact SHA-256 identities.

Local dependency packages remain resolvable, lockable, explicitly importable, inspectable, packable, and verifiable. v0.14 additionally exchanges immutable artifacts through real file registries and optionally HTTPS read endpoints. Registry-fetched artifacts do not yet become project dependencies until remote manifest syntax, recursive materialization, and lockfile provenance land together.
