# Emojineer Documentation

Emojineer is a sovereign emoji-native programming language. Its source is parsed into Emojineer's AST, linked across project-local, dependency-package, or built-in standard modules, compiled to Emojineer's `EMJBC` bytecode, and executed by Emojineer's VM. It is not emoji syntax translated into Python, JavaScript, C++, or another host language.

## Start here

- [Language reference](LANGUAGE.md) - source model, tokens, grammar, values, functions, collections, control flow, and module imports.
- [CLI and toolchain](CLI.md) - language, project, package, registry, discovery, LSP, and debugger entry points.
- [EMJBC bytecode](BYTECODE.md) - bytecode format, verifier limits, source metadata, and VM execution model.
- [Modules and imports](MODULES.md) - local, `pkg:`, and `std:` imports plus package ownership and visibility.
- [`emji` projects](PROJECTS.md) - manifests, local/registry dependencies, locking, materialization, and project validation.
- [Package artifacts and registry transport](REGISTRY.md) - immutable artifacts, verified exchange, publication, dependencies, and discovery.
- [Authenticated publication](AUTHENTICATED_REGISTRY_PUBLICATION.md) - `emjpub1` HTTPS write protocol and receipts.
- [Remote dependencies](REMOTE_DEPENDENCIES.md) - reproducible registry dependency resolution/materialization.
- [Language server](LANGUAGE_SERVER.md) - native C++ LSP behavior and offline editor boundaries.
- [Source debugger](DEBUGGER.md) - source breakpoints, stepping, frames, values, and provenance.
- [Package discovery](PACKAGE_DISCOVERY.md) - deterministic search, package metadata, release filtering, and reverse dependencies.
- [Product roadmap](ROADMAP.md) - landed and future product trains.

## Focused references

- [Functions](FUNCTIONS.md)
- [Collections](COLLECTIONS.md)
- [Custom Emoji Registry](CER.md)
- [Standard library](STDLIB.md)
- [Formatter and lint](SOURCE_TOOLING.md)
- [REPL and bytecode tooling](TOOLING.md)
- [Design provenance](PROVENANCE.md)

## Current implemented product level

The compiler/toolchain reports Emojineer **0.19**. Product Trains **1 through 19** are implemented on this train.

The product now includes the sovereign language/compiler/VM core; functions and collections; CER; REPL/source tooling; project workflow; modules; native standard modules; local and remote package dependency graphs; immutable `.emjpkg` artifacts; verified file/HTTPS registry reads; reproducible materialization and lock v3; authenticated HTTPS publication; native C++ LSP/editor integration; the source-level debugger; and deterministic package search/discovery with stable/prerelease filtering and reverse-dependency queries.

The documentation describes implemented behavior unless a section is explicitly labeled as future work or as a historical train contract.

## File, module, package, and registry forms

- `.emoji` - Emojineer UTF-8 source.
- `.emjbc` - serialized Emojineer bytecode with deterministic debug metadata where present.
- `.emjpkg` - deterministic immutable package source artifact.
- `emojineer.toml` - strict package/project manifest with local/path and registry dependency declarations.
- `emojineer.lock` - deterministic lock v3 provenance for path and registry dependencies.
- `.emojineer/packages/...` - project-owned verified materialized remote package store.
- CER `.json` - optional custom emoji token packs.
- `relative/path.emoji` - source module inside the importing package's owned root.
- `pkg:<dependency>/<path>.emoji` - source module inside a declared direct dependency package.
- `std:<name>` - deterministic built-in standard module.
- `EMJREGISTRY1` - registry identity descriptor.
- `EMJREGPKG1` - immutable per-package version index.
- `EMJREGDISC1` - deterministic registry discovery index for package search metadata.
- `emojineer.registry-search.v1`, `emojineer.registry-package-info.v1`, `emojineer.registry-dependents.v1` - deterministic discovery JSON schemas.

Ordinary source compilation, execution, LSP requests, and debugger operation do not contact registries. Registry networking remains explicit `emji` package-manager authority. Package discovery does not weaken immutable fetch/materialization verification or make transitive dependencies ambient imports.
