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

`fmt`, `lint`, canonical indentation/LF form, comment preservation, multiline-string protection, CER-aware structural formatting, CR/CRLF diagnostics.

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

### Train 11 — Cross-package module imports

- explicit `pkg:<dependency>/<module>.emoji` source imports;
- project/package context supplied to the module linker through the real `PackageGraph`;
- declared-direct-dependency enforcement so transitive packages do not become ambient imports;
- root-relative imports remain confined to the importing package's owned source tree;
- deepest-root ownership checks prevent local or `pkg:` paths from tunneling through one package into a separately resolved nested package;
- deterministic `pkg:<package>/<path>` linked module identities that do not encode checkout roots;
- ordinary `📤` export/collision behavior across project, package, and standard modules;
- distinct package-cycle and source-module-cycle diagnostics;
- file CLI commands and `emji check` use the same package-aware linker path;
- package-qualified import regression coverage for direct, transitive, nested, missing, duplicate-name, cycle, and checkout-portability behavior;
- no remote registry and no VM opcode/EMJBC format bump.

### Train 12 — Package workflow maturation

- `emji tree` over the real resolved `PackageGraph`;
- explicit `root`, `direct`, and `transitive` package relation presentation;
- checkout-relative package paths and entry-source presentation;
- optional full SHA-256 package content identities with `--hashes`;
- deterministic `emojineer.package-graph.v1` JSON output with full hashes and sorted dependency names;
- shared dependency DAGs shown with repeated edges but bounded recursive expansion via `(shared)` markers;
- graph output excludes absolute checkout paths and timestamps, so equivalent checkouts render identically;
- package graph failures receive explicit CLI diagnostic context;
- graph-report logic is a presentation layer over the existing resolver rather than a second package model;
- no fake registry and no audit/report subsystem.

### Train 13 — Immutable package artifacts and registry substrate

- deterministic binary `.emjpkg` format with `EMJPKG1` framing;
- canonical package metadata plus sorted package-owned `.emoji` source records;
- per-source SHA-256 verification;
- package `content-sha256` tied directly to the existing `PackageGraph` package-content identity;
- separate whole-artifact `artifact-sha256` for immutable transport/cache identity;
- checkout-portable artifact bytes with dependency-owned source excluded from parent artifacts;
- bounded parser with format, path, ordering, checksum, content-identity, and trailing-byte validation;
- `emji pack`, `emji artifact`, and `emji verify-artifact`;
- content-addressed cache path contract `<cache>/<package>/<version>/<artifact-sha256>.emjpkg`;
- full SemVer 2.0 numeric precision plus deterministic `*`, exact, caret, and tilde requirement selection primitives;
- explicit prerelease/default-stable behavior and deterministic build-metadata tie breaking.

### Train 14 — Registry transport and verified artifact exchange

- canonical local/file and HTTPS registry endpoint model;
- `EMJREGISTRY1` registry identity/discovery descriptor;
- deterministic `EMJREGPKG1` package-version indexes;
- every indexed version binds package content SHA-256 and exact artifact SHA-256;
- `emji registry-init`, `registry-info`, and `versions`;
- immutable local-registry `emji publish` with idempotent exact republishing and same-version conflict rejection;
- `emji fetch <package> <requirement>` using Train 13 SemVer selection;
- exact package/version/content/artifact identity verification before cache admission;
- registry-scoped content-addressed cache with verification on cache hits and automatic corrupt-entry repair;
- bounded registry descriptors, indexes, artifact reads, and HTTPS responses;
- plain HTTP rejection;
- optional HTTPS read transport through libcurl with TLS peer/host verification, no redirects, and request timeouts;
- local registry behavior remains fully available without libcurl;
- HTTPS publication intentionally withheld until an authenticated immutable-upload protocol exists;
- registry artifacts intentionally remain separate from project manifests until remote dependency resolution and lock provenance land together;
- no ambient network authority in the language runtime and no audit machinery.

## Next product train

### Train 15 — Remote dependency integration and reproducible materialization

Turn verified registry artifacts into real project dependencies without weakening local/path packages:

- manifest dependency source model that distinguishes local paths from registry requirements;
- canonical registry endpoint/identity plus requested SemVer requirement in manifest semantics;
- recursive remote dependency resolution from immutable artifacts;
- verified materialization into a deterministic package store instead of arbitrary extraction paths;
- dependency graph ownership and package-aware linking across local and materialized registry packages;
- deterministic lock format carrying source kind, registry identity, requirement, selected version, content SHA-256, artifact SHA-256, and dependency edges;
- stale-lock detection when registry requirements or selected immutable identities change;
- offline reproducibility from a complete verified cache/store;
- real remote `emji add` only after the manifest, resolver, materializer, and lock path agree;
- no implicit network access during ordinary language execution.

### Train 16 — Authenticated registry publication

After remote resolution is reproducible, define a real write trust boundary:

- authenticated HTTPS upload/session contract;
- server-side immutable package/version conflict rules;
- authorization and namespace ownership;
- bounded upload behavior and artifact checksum confirmation;
- explicit credentials handling outside manifests and source code;
- publication receipt/provenance suitable for tooling;
- no generic unauthenticated PUT masquerading as package publication.

## Later product trains

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
