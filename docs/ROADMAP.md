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
- `EMJREGISTRY1` registry identity/discovery descriptor at `v1/registry.txt`;
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
- registry artifacts remain separate from project manifests until reproducible remote dependency provenance lands;
- no ambient network authority in the language runtime and no audit machinery.

### Train 15 — Remote dependency integration and reproducible materialization

- manifest dependency sources distinguish local paths from registry requirements;
- manifests bind registry aliases/endpoints and requested SemVer requirements;
- recursive remote dependency resolution consumes immutable verified artifacts;
- deterministic materialization into the package store instead of arbitrary extraction paths;
- package ownership and package-aware linking work across local and materialized registry packages;
- lock format 3 carries source kind, registry identity, requirement, selected version, content SHA-256, artifact SHA-256, and dependency edges;
- stale-lock detection covers registry requirement/selection drift;
- offline reproducibility works from complete verified cache/store state;
- real remote `emji add` updates manifest and lock transactionally;
- ordinary language execution still receives no implicit network access.

### Train 16 — Authenticated registry publication

- versioned `emjpub1` authenticated HTTPS write protocol;
- Train 14 `GET <endpoint>/v1/registry.txt` identity preflight MUST succeed before any bearer credential is sent;
- authenticated `POST <endpoint>/v1/publish` carries the actual immutable `.emjpkg` bytes using `application/vnd.emojineer.publish.v1+octet-stream`;
- request headers bind protocol version, namespace, package, exact SemVer, content SHA-256, and artifact SHA-256;
- credentials are indirect process authority: currently `EMOJINEER_TOKEN`; raw secret CLI arguments are not accepted;
- credential-in-URL and whitespace/control/header-injection forms are rejected;
- credentials never enter manifests, artifacts, lockfiles, source, receipts, or normal diagnostics;
- missing/invalid auth is `401`, namespace/package authority failure is `403`, malformed/checksum mismatch is `400`, immutable conflict is `409`, and server request-bound rejection is `413`;
- exact republishing of the same registry/package/version/content SHA-256/artifact SHA-256 tuple MUST succeed idempotently;
- differing immutable content under the same registry/package/version MUST fail without replacement;
- successful responses use `application/vnd.emojineer.publish-receipt.v1+json` and bind registry id, package, version, content/artifact SHA-256, protocol version, receipt id, and timestamp;
- receipt JSON is strict and canonical: duplicate/unknown/trailing fields and mismatched immutable identities are rejected before success;
- every successful authenticated publication atomically persists the verified receipt to `<package-root>/.emojineer-receipt.json`, with `--receipt <file>` as an alternate destination rather than an opt-out;
- TLS peer/host verification and HTTPS-only transport remain mandatory, redirects remain disabled, and failed response bodies are never echoed into normal diagnostics;
- concrete client bounds: 128 MiB upload, 16 KiB receipt, 10-second connect, 300-second upload, 30-second response-header wait after upload, and 300-second response-body deadline;
- local/file registry publication remains unchanged and credential-free;
- the dedicated interoperability fixture consumes the exact encoded request, enforces auth/ownership/immutability/checksums/bounds, and proves uploaded artifacts round-trip through the existing verified fetch path;
- no ambient network authority for Emojineer programs or the VM and no audit machinery.

### Train 17 — Language server and editor integration

- native C++ `emojineer-lsp` over the sovereign parser/compiler/package/module model;
- JSON-RPC/LSP stdio lifecycle and unsaved-buffer document state;
- UTF-16/LSP position translation over Emojineer's UTF-8/grapheme source model;
- diagnostics, completion, hover, definition/references, symbols, and formatter integration;
- local/path/materialized-registry package awareness without undeclared transitive imports;
- ordinary editor requests remain offline with respect to registries.

### Train 18 — Source-level debugger

- deterministic source mappings in EMJBC v7;
- source breakpoints, continue/pause, step into/over/out;
- stack/frame, parameter/local/global inspection and deterministic value rendering;
- source provenance, Stale vs SourceDrift diagnostics, and serialized-bytecode sessions;
- debugger observation does not mutate VM state or consume program input;
- no second interpreter or debugger-only language semantics.

### Train 19 — Package search and registry discovery

- deterministic `EMJREGDISC1` registry discovery index contract;
- existing file registries derive discovery from real package indexes plus verified immutable artifacts, with no migration or fake registry state;
- HTTPS registries expose bounded `v1/discovery.index` metadata bound to registry identity;
- `emji search` over package name, entry path, and direct dependency metadata;
- stable-by-default selection with explicit `--include-prerelease` behavior;
- `emji package-info` with selected release, all discoverable versions, entry, dependencies, and immutable identities;
- `emji dependents` direct reverse-dependency queries;
- deterministic human and JSON schemas for tooling;
- `emji discovery-index` canonical materialization for registry operators;
- discovery networking remains explicit package-manager authority and never becomes program/VM authority.

### Train 20 — Capability model and native facilities

- default VM execution grants no filesystem, network, process, clock, randomness, or host-environment authority;
- six reserved native facility identifiers lower through the sovereign compiler to EMJBC v8 `HostCall` instructions rather than a second interpreter or foreign-language bridge;
- the compiler records the exact capability union for the complete linked chunk, including imported local/package code;
- EMJBC v8 serializes that contract and the verifier independently recomputes it from host-call operands, rejecting forged/unknown masks;
- VM preflight checks all required grants before instruction zero, preventing partial program effects before a missing-authority failure;
- REPL and debugger execution use the same production VM policy and cannot become alternate authority paths;
- runtime grants are accepted only by execution commands; compile/check/LSP/package/registry tooling does not inherit them;
- `--sandbox` is hard zero-native-host-authority mode;
- deterministic mode admits only virtual clock/random grants with configurable logical clock and seeded VM-local PRNG state;
- filesystem reads and HTTPS GETs are bounded, network redirects are disabled/TLS verified, while broad `process` and host-environment grants are documented as intentionally powerful;
- `emojineer capabilities` reports source or bytecode authority requirements without execution;
- dedicated C++ and real-CLI acceptance tests prove verifier binding, preflight zero-effects denial, deterministic replay, filesystem gating, policy validation, and dependency authority propagation.

### Train 21 — WASM / Host Interop Layer

- explicit `🔌` typed adapter imports and `📡` typed Emojineer function exports;
- EMJBC v9 verifier-visible import/export tables and `InteropCall` instructions;
- deterministic bounded `EMJABI1` request/response envelopes for numbers, text, booleans, and nested arrays;
- adapter-declared capability masks participate in the Train 20 whole-program authority contract;
- exact adapter capability matching and binding availability are preflighted before instruction zero;
- deterministic execution accepts only adapters explicitly bound as deterministic;
- production-VM export invocation initializes ordinary module/global state and executes the same function bytecode used by normal programs;
- direct-value and ABI export invocation share one typed signature contract;
- `emojineer interop` inspects source or serialized bytecode without execution;
- dedicated C++ and real-CLI tests cover v9 round-trip, forged metadata, zero-effect denial, deterministic adapters, ABI bounds/failures, dependency authority propagation, and direct/ABI equivalence;
- no second Emojineer interpreter and no ambient host authority.

### Train 22 — Low-level Emojineer / EASM

- textual `EASM1` low-level representation with strict parsing and canonical reparsable rendering;
- statically typed `i64`, `f64`, and `bool` register files plus explicit typed function signatures;
- bounded zero-initialized `u8`, `i64`, and `f64` buffers with checked indexing and typed load/store instructions;
- checked signed-integer arithmetic, finite floating-point operations, typed comparisons, labels/branches, returns, and instruction fuel;
- verifier enforcement for table/memory/register/instruction bounds, register/opcode typing, jump targets, import signatures, export targets, and exact inferred capability metadata;
- explicit low-level imports through the existing Train 21 `InteropRegistry` and deterministic `EMJABI1` codec rather than direct host callbacks;
- per-export capability and deterministic eligibility derived from the imports actually called by that export;
- `bind_easm_export` exposes low-level exports back to high-level Emojineer as ordinary Train 21 adapters, preserving outer capability/binding preflight;
- strict locale-independent numeric parsing and exact binary64 canonical round-tripping;
- `emojineer easm-check`, `easm-dump`, `easm-info`, and `easm-run`, plus a real `.easm` example;
- dedicated C++ and real-CLI acceptance tests cover typed memory, control flow/fuel, verifier failures, zero-effect authority denial, determinism propagation, high-level→EASM ABI composition, capability-contract mismatch, numeric bounds, and direct/ABI equivalence;
- no EMJBC bump, ambient FFI, raw-pointer exposure, direct syscall surface, or second high-level Emojineer implementation.

## Active 1.0 productization arc

The next execution arc is intentionally product-facing rather than another isolated
language-research train. Its contract is [PRODUCT_1_0_CONTRACT.md](PRODUCT_1_0_CONTRACT.md).

The arc sequences:

1. the 1.0 product/domain contract;
2. installable Windows/Linux/macOS distributions;
3. packaged VS Code/Cursor editor UX and semantic emoji insertion;
4. practical structured-data and explicit error/result semantics;
5. practical capability-gated CLI/filesystem/network facilities;
6. first-class bytes and encoding utilities;
7. a five-minute tour, templates, and real example corpus;
8. a sandboxed browser playground running real Emojineer semantics;
9. Emojineer-native web markup over a typed document/DOM IR; and
10. a release-hardening compatibility/fuzz/cross-platform capstone.

Emoji Stories and playful physical-note secret-code schemes are explicitly
reserved for 2.0/post-1.0. Retro targets remain later domain work unless the
1.0 portability effort proves a prerequisite is necessary.

## Later product trains

### Semantic compression and metaprogramming research

Carefully specify macro/semantic-compression mechanisms inspired by earlier SCL
ideas. Expansion should produce ordinary Emojineer semantics and remain
inspectable by tooling.

### Native backend

Explicit IR boundary, LLVM/native compilation, equivalence testing against the
VM, and preservation of language-visible behavior rather than a second dialect.

### Retro targets and Emoji Stories

Use the sovereign VM/EASM/encoding stack for constrained or historical targets
without distorting the 1.0 core. Emoji Stories is a separate 2.0 design space
for human-passable emoji encodings/puzzles, not a 1.0 programming-language
requirement and not cryptography unless a future design actually provides it.

## Ongoing language evolution

Future additions can include pattern matching, interfaces/protocols, more
collection types, richer standard modules, package description/tag metadata,
and accessibility-oriented textual aliases. Each must extend the sovereign
language model rather than route source through another programming language.
