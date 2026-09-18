# Emojineer Documentation

Emojineer is a sovereign emoji-native programming language. Its source is parsed into Emojineer's AST, linked across project-local, dependency-package, or built-in standard modules, compiled to Emojineer's `EMJBC` bytecode, and executed by Emojineer's VM. It is not emoji syntax translated into Python, JavaScript, C++, or another host language.

## Start here

- [Install Emojineer](INSTALL.md) - release artifacts, installed layout, and clean install/uninstall paths.
- [Five-minute tour](FIVE_MINUTE_TOUR.md) - first project, templates, CLI/data/package examples, capabilities, CER, interop, and EASM.
- [Browser playground](PLAYGROUND.md) - static WebAssembly UI using the production compiler/VM in sandbox mode.
- [Editor experience](EDITOR.md) - VS Code/Cursor extension, production LSP, and semantic emoji palette.
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
- [Capability model and native facilities](CAPABILITIES.md) - default-deny host authority, grants, EMJBC binding, sandbox, and deterministic execution.
- [Practical 1.0 runtime](PRACTICAL_RUNTIME.md) - records, explicit results, bytes/codecs, argv, write/directory, and richer HTTPS request facilities.
- [Host/WASM interop](INTEROP.md) - typed imports/exports, deterministic EMJABI1 marshaling, adapter contracts, and VM export invocation.
- [Low-level Emojineer / EASM](EASM.md) - EASM1 syntax, typed registers/buffers, verifier/runtime bounds, and Train 21 ABI composition.
- [Emojineer 1.0 product contract](PRODUCT_1_0_CONTRACT.md) - install-to-first-program goals, compatibility promises, 1.0 domains, and explicit non-goals.
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

The compiler/toolchain reports Emojineer **0.23**. Product Trains **1 through 22** are landed, and the 1.0 productization arc is in progress.

The product now includes the sovereign language/compiler/VM core; functions and collections; CER; REPL/source tooling; project workflow; modules; native standard modules; local and remote package dependency graphs; immutable `.emjpkg` artifacts; verified file/HTTPS registry reads; reproducible materialization and lock v3; authenticated HTTPS publication; native C++ LSP/editor integration; the source-level debugger; deterministic package search/discovery with stable/prerelease filtering and reverse-dependency queries; the default-deny capability/native-facility model with EMJBC v8 verifier/runtime enforcement; typed host/WASM interop with verifier-visible EMJBC v9 imports/exports plus deterministic EMJABI1 marshaling; and low-level `EASM1` with typed registers, bounded typed buffers, verifier-enforced control flow, fuel-bounded execution, and explicit composition through Train 21 adapters.

The documentation describes implemented behavior unless a section is explicitly labeled as future work or as a historical train contract.

## File, module, package, and registry forms

- `.emoji` - Emojineer UTF-8 source.
- `.easm` - verified low-level `EASM1` text with typed registers, buffers, imports, functions, and exports.
- `.emjbc` - serialized Emojineer bytecode; current writer v10 includes deterministic debug/provenance metadata, the exact verifier-bound authority mask, v9 typed interop metadata, and verifier-visible v10 intrinsic/facility operands.
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

Ordinary source compilation, LSP requests, debugger source inspection, and EASM inspection do not contact registries. Registry networking remains explicit `emji` package-manager authority. Program execution begins with zero Train 20 host grants; native facilities, Train 21 interop adapters, and Train 22 EASM imports remain behind explicit execution policy and preflight. EASM exports cross into high-level Emojineer only through Train 21 `EMJABI1` bindings. REPL/debugger execution uses the same production VM policy. Package discovery does not weaken immutable fetch/materialization verification or make transitive dependencies ambient imports.
